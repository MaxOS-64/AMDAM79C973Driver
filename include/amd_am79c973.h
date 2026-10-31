/**
 * @file amd_am79c973.h
 * @brief Driver for the AMD AM79C973 Ethernet Controller
 *
 * @date 22nd October 2022
 * @author Max Tyson
 */

#ifndef AMDAM79C973DRIVER_AMD_AM79C973_H
#define AMDAM79C973DRIVER_AMD_AM79C973_H

#include <cstdint>
#include <libcommon/macros.h>
#include <libdriver/generic/ethernet.h>
#include <libcommon/port.h>
#include <libdriver/device.h>
#include <libkpi/hardware/userinterrupts.h>
#include <libnet/address/mac.h>
#include <libcommon/mem.h>
#include <libkpi/processes/thread.h>
#include <libcommon/assert.h>

namespace AMDAM79C973Driver {

    enum DescriptorFlags : uint32_t {
        DESC_END_OF_PACKET      = (1u << 24),
        DESC_START_OF_PACKET    = (1u << 25),
        DESC_ERROR              = (1u << 30),
        DESC_OWNED_BY_HARDWARE  = (1u << 31),
    };



    constexpr uint32_t DESC_START_AND_END = DESC_START_OF_PACKET | DESC_END_OF_PACKET;
    constexpr uint32_t DESC_RESERVED        = 0xFu << 12;
    constexpr uint32_t DESC_SIZE_MASK       = (1u << 12) - 1;
    constexpr uint32_t DESC_SIZE            = 0x7FF;

    constexpr uint32_t desc_size(size_t size) {
        return (-(int) size) & DESC_SIZE_MASK;
    }

	/**
	 * @struct InitializationBlock
	 * @brief The initialization block for the AMD AM79C973 Ethernet Controller
	 *
	 * @typedef initialisation_block_t
	 * @brief Alias for InitializationBlock struct
	 */
	typedef struct PACKED InitializationBlock {

		uint16_t mode;                          ///< The operation mode
		unsigned reserved1 : 4;                 ///< Unused, must be zero
		unsigned num_send_buffers : 4;          ///< How many buffers are used for sending
		unsigned reserved2 : 4;                 ///< Unused, must be zero
		unsigned num_recv_buffers : 4;          ///< How many buffers are used for receiving
		uint64_t physical_address : 48;         ///< The physical (MAC) address of the device (Not 64 bits but will be treated like it is)
		uint16_t reserved3;                     ///< Unused, must be zero
		uint64_t logical_address;               ///< The logical address filter for the device to use when deciding whether to accept a packet (0 = no filtering)
		uint32_t recv_buffer_descr_address;     ///< Physical address of the first receive buffer descriptor
		uint32_t send_buffer_descr_address;     ///< Physical address of the first send buffer descriptor

	} initialisation_block_t;

	/**
	 * @struct BufferDescriptor
	 * @brief Defines the layout of a buffer descriptor for the AMD AM79C973 Ethernet Controller
	 *
	 * @typedef buffer_descriptor_t
	 * @brief Alias for BufferDescriptor struct
	 */
	typedef struct PACKED BufferDescriptor {

		uint64_t address;   ///< Physical address of the buffer
		uint32_t flags;     ///< Flags for the buffer
		uint32_t flags2;    ///< Additional flags for the buffer (@todo enum this)
		uint32_t avail;     ///< Indicates whether the buffer is available to be used

	} buffer_descriptor_t;

	/**
	 * @class AMD_AM79C973
	 * @brief Driver for the AMD AM79C973 Ethernet Controller
	 */
	class AMD_AM79C973 : public LibDriver::generic::Ethernet, public MaxOS::KPI::hardware::UserInterruptHandler {

		private:

			//Reading the media access control address (MAC address)
			MaxOS::common::Port16Bit mac_address_0_port;
			MaxOS::common::Port16Bit mac_address_2_port;
			MaxOS::common::Port16Bit mac_address_4_port;

			//Register ports
			MaxOS::common::Port16Bit register_data_port;
			MaxOS::common::Port16Bit register_address_port;
			MaxOS::common::Port16Bit bus_control_register_data_port;

			MaxOS::common::Port16Bit reset_port;

			initialisation_block_t init_block = {};

			buffer_descriptor_t* send_buffer_descr;
			uint8_t send_buffers[2 * 1024 + 15][8] = {};
			uint8_t current_send_buffer;

			buffer_descriptor_t* recv_buffer_descr;
			uint8_t recv_buffers[2 * 1024 + 15][8] { };
			uint8_t current_recv_buffer;

			LibNet::address::mac_t own_mac = {0};
			volatile bool active;
			volatile bool init_done;

			void fetch_data_received();
			void fetch_data_sent();

	        void initalise();
	        void activate();

		public:
			explicit AMD_AM79C973(LibDriver::device_identification_t& device_id, LibDriver::HardwareCommunication::hardware_mapping_t& hmap);
			~AMD_AM79C973();

			void handle_interrupt() final;

			void send(uint8_t* buffer, uint32_t size) final;
			uint64_t get_mac_address() final;
	};


}


#endif //AMDAM79C973DRIVER_AMD_AM79C973_H