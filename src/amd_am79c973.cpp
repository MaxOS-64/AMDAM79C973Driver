/**
 * @file amd_am79c973.cpp
 * @brief Implementation of the AMD AM79C973 Ethernet Driver
 *
 * @date 22nd October 2022
 * @author Max Tyson
 */

#include <amd_am79c973.h>

using namespace MaxOS;
using namespace MaxOS::common;
using namespace AMDAM79C973Driver;
using namespace LibNet::address;
using namespace KPI;
using namespace KPI::processes;



/**
 * @brief Constructs a new AMD_AM79C973 Ethernet driver, reads the MAC address and sets up the initialisation block and buffer descriptors
 * @param dev The PCI device descriptor for this device
 */
AMD_AM79C973::AMD_AM79C973(LibDriver::device_identification_t& device_id, LibDriver::HardwareCommunication::hardware_mapping_t& hmap)
: UserInterruptHandler(hmap.irq),
  mac_address_0_port(hmap.ranges[0].address_base),
  mac_address_2_port(hmap.ranges[0].address_base + 0x02),
  mac_address_4_port(hmap.ranges[0].address_base + 0x04),
  register_data_port(hmap.ranges[0].address_base + 0x10),
  register_address_port(hmap.ranges[0].address_base + 0x12),
  bus_control_register_data_port(hmap.ranges[0].address_base + 0x16),
  reset_port(hmap.ranges[0].address_base + 0x14)
{

    // Reset device
    reset_port.read();
    reset_port.write(0);
    sleep(10);

    initalise();
    activate();

}

AMD_AM79C973::~AMD_AM79C973() = default;

void AMD_AM79C973::initalise() {

	// No active buffer at the start
	current_send_buffer = 0;
	current_recv_buffer = 0;

	//Not active or initialised
	active = false;
	init_done = false;

	// Get the MAC addresses (split up in little endian order)
	uint64_t mac_0 = mac_address_0_port.read() % 256;
	uint64_t mac_1 = mac_address_0_port.read() / 256;
	uint64_t mac_2 = mac_address_2_port.read() % 256;
	uint64_t mac_3 = mac_address_2_port.read() / 256;
	uint64_t mac_4 = mac_address_4_port.read() % 256;
	uint64_t mac_5 = mac_address_4_port.read() / 256;

	// Combine MAC addresses into one 48 bit number
	own_mac = MediaAccessControlAddress(mac_0, mac_1, mac_2, mac_3, mac_4, mac_5);

    // Set the device to 32 bit mode
	register_address_port.write(20);
	bus_control_register_data_port.write(0x102);

	// Reset the stop bit (tell device it's not supposed to be reset now)
	register_address_port.write(0);
	register_data_port.write(0x04);

	// Set the initialization block so that its not in promiscuous mode (ie only listen to packets address to this device)
	init_block.mode = 0x0000;
	init_block.reserved1 = 0;
	init_block.num_send_buffers = 3;
	init_block.reserved2 = 0;
	init_block.num_recv_buffers = 3;
	init_block.physical_address = own_mac.address();
	init_block.reserved3 = 0;
	init_block.logical_address = 0;

	// Set Buffer descriptors memory (aligned to 16 btyes)
    auto send_descr_raw  = new uint8_t[sizeof(buffer_descriptor_t) * 8 + 15];
    send_buffer_descr = (buffer_descriptor_t*)(((uintptr_t)send_descr_raw + 15) & ~(uintptr_t)0xF);
	init_block.send_buffer_descr_address = (uint64_t) send_buffer_descr;

    auto recv_descr_raw  = new uint8_t[sizeof(buffer_descriptor_t) * 8 + 15];
    recv_buffer_descr = (buffer_descriptor_t*)(((uintptr_t)recv_descr_raw + 15) & ~(uintptr_t)0xF);
	init_block.recv_buffer_descr_address = (uint64_t) recv_buffer_descr;

	for(uint8_t i = 0; i < 8; i++) {

	    // Send (owned by the driver until a packet is queued)
	    send_buffer_descr[i].address = (((uint64_t) &send_buffers[i]) + 15) & ~(uint64_t) 0xF;
	    send_buffer_descr[i].flags   = DESC_RESERVED | DESC_SIZE;
	    send_buffer_descr[i].flags2  = 0;
	    send_buffer_descr[i].avail   = 0;

	    // Receive (owned by the card so it can fill them)
	    recv_buffer_descr[i].address = (((uint64_t) &recv_buffers[i]) + 15) & ~(uint64_t) 0xF;
	    recv_buffer_descr[i].flags   = DESC_OWNED_BY_HARDWARE | DESC_RESERVED | DESC_SIZE;
	    recv_buffer_descr[i].flags2  = 0;
	    recv_buffer_descr[i].avail   = 0;
	}

	// Move initialization block into device (high and low portions)
	register_address_port.write(1);
	register_data_port.write((uint64_t) (&init_block) & 0xFFFF);
	register_address_port.write(2);
	register_data_port.write(((uint64_t) (&init_block) >> 16) & 0xFFFF);

}



/**
 * @brief Activates the device and starts it
 */
void AMD_AM79C973::activate() {

    // Tell the device to start and wait for it to complete
	init_done = false;
	register_address_port.write(0);
	register_data_port.write(0x41);
	while(!init_done) {};

    // Get current config
	register_address_port.write(4);
	uint32_t temp = register_data_port.read();

    // Enable auto padding and auto stripping (ie let hardware handle mmu sizes and ensure only payload reaches the driver)
	register_address_port.write(4);
	register_data_port.write(temp | 0xC00);

    // Mark setup as complete
	register_address_port.write(0);
	register_data_port.write(0x42);
	active = true;
}

/**
 * @brief Handle a interrupt fired by the device
 *
 * @note Note to future self, may want to nicley put into a switch case. Dont as multiple errors can occur at the same time
*/
void AMD_AM79C973::handle_interrupt() {

	// Read status of the interrupt
	register_address_port.write(0);
	uint32_t temp = register_data_port.read();

	// Errors
	if((temp & 0x8000) == 0x8000)
		klog("device error: ");
	if((temp & 0x2000) == 0x2000)
		klog("%hCOLLISION ERROR\n");
	if((temp & 0x1000) == 0x1000)
		klog("%MISSED FRAME\n");
	if((temp & 0x0800) == 0x0800)
		klog("%MEMORY ERROR\n");

	// Responses
	if((temp & 0x0400) == 0x0400)
	    fetch_data_received();

	if((temp & 0x0200) == 0x0200)
	    fetch_data_sent();

	if((temp & 0x0100) == 0x0100)
	    init_done = true;

	// Mark the interrupt as handled
	register_address_port.write(0);
	register_data_port.write(temp);
}



// Sending a package :
// "Flags2" shows whether an error occurred while sending and should therefore be set to 0 by the driver.
//  The OWN bit must now be set in the “flags” field (0x80000000) in order to “transfer” the descriptor to the card.
//  Furthermore, STP (Start of Packet, 0x02000000) and ENP (End of Packet, 0x01000000) should be set - this indicates that the data is not split up, but that it is a single Ethernet packet.
//  Furthermore, bits 12-15 must be set (0x0000F000, are probably reserved) and bits 0-11 are negative Size of the package.

/**
 * @brief This function sends a package
 *
 * @param buffer The buffer to send
 * @param size The size of the buffer
 *
 * @todo take a buffer like how files doe
 */
void AMD_AM79C973::send(uint8_t* buffer, uint32_t size) {

    // Driver should not have marked device as active before it is ready
    ASSERT(active, "Attempt to send data before device has finished activating");
    ASSERT(size <= 1518, "Attempt to send payload lager than 1518 bytes");

    // Get the next available buffer (@todo check free?)
	int send_descriptor = current_send_buffer;
	current_send_buffer = (current_send_buffer + 1) % 8;

	// Copy the data into the send buffer and mark that its now in use
    memcpy((void*) send_buffer_descr[send_descriptor].address, buffer, size);
    send_buffer_descr[send_descriptor].avail = 0;

    // Clear any errors and mark the packet as not split (start & end in this buffer) and denote the size of the packet
	send_buffer_descr[send_descriptor].flags2 = 0;
	send_buffer_descr[send_descriptor].flags = DESC_OWNED_BY_HARDWARE | DESC_START_AND_END | DESC_RESERVED | desc_size(size);

    // Tell the device to send the contents of the buffer
	register_address_port.write(0);
	register_data_port.write(0x48);
}

void AMD_AM79C973::fetch_data_received() {

    // Loop through all the buffers in the ring that have been handed back to the driver
    for (; (recv_buffer_descr[current_recv_buffer].flags & DESC_OWNED_BY_HARDWARE) == 0;
       current_recv_buffer = (current_recv_buffer + 1) % 8) {


        auto& descriptor = recv_buffer_descr[current_recv_buffer];

        // Only handle packets without errors
        if (!(descriptor.flags & DESC_ERROR) && (descriptor.flags & DESC_START_AND_END) == DESC_START_AND_END) {

            // If it is an ethernet 2 packet then remove the checksum
            uint32_t size = descriptor.flags2 & DESC_SIZE_MASK;
            if (size > 64)
                size -= 4;

            auto* buffer = (uint8_t*) descriptor.address;
            //@todo: fire_data_received(buffer, size); (also copy so can hand back right away?

        }

        // Hand the descriptor back to the card
        descriptor.flags2 = 0;
        descriptor.flags  = DESC_OWNED_BY_HARDWARE | DESC_RESERVED | DESC_SIZE;
	}
}

void AMD_AM79C973::fetch_data_sent() {

    // Process all descriptors that have been handed back to the driver
    for (; (recv_buffer_descr[current_recv_buffer].flags & DESC_OWNED_BY_HARDWARE) == 0;
           current_recv_buffer = (current_recv_buffer + 1) % 8) {

        auto& descriptor = recv_buffer_descr[current_recv_buffer];

        // Only handle packets without errors that are complete (start and end bits both set)
        if (!(descriptor.flags & DESC_ERROR) &&
            (descriptor.flags & DESC_START_AND_END) == DESC_START_AND_END) {

            uint32_t size = descriptor.flags2 & DESC_SIZE_MASK;

            // Remove the Ethernet CRC checksum
            if (size > 64)
                size -= 4;

            auto* buffer = (uint8_t*)descriptor.address;
            //@todo return it
        }

        // Hand the descriptor back to the card
        descriptor.flags2 = 0;
        descriptor.flags  = DESC_OWNED_BY_HARDWARE | DESC_RESERVED | DESC_SIZE;
    }
}



/**
 * @brief This function gets the MAC address
 *
 * @return The MAC address
 */
uint64_t AMD_AM79C973::get_mac_address() {

    ASSERT(own_mac.address() != 0, "attempt to read mac address before init done");
	return own_mac.address();
}

