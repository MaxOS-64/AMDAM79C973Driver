//
// Created by Max Tyson on 14/04/2026.
//

#include <libdriver/device.h>
#include <libdriver/server/ethernet_server.h>
#include <libdriver/server/drivermanager_client.h>

#include <amd_am79c973.h>

using namespace AMDAM79C973Driver;
using namespace MaxOS::KPI;
using namespace MaxOS::KPI::hardware;
using namespace MaxOS::KPI::processes;
using namespace MaxOS;
using namespace LibDriver;
using namespace LibDriver::generic;
using namespace LibDriver::HardwareCommunication;

//@todo speed up start up

extern "C" void _start(int argc, char* argv[])
{
	// Fetch the device id
	if (argc <= 1)
		return;
	string id = argv[1];

	// Initialise the device
	hardware_mapping_t hmap = get_hardware_mapping(id);
	device_identification_t device = get_device_identification(id);
	AMD_AM79C973 am79c973(device, hmap);

    // Start servers
	EthernetServer driver_server(&am79c973);
    UserInterruptServer irq_server(&am79c973);
    driver_server.open(id);

	// Wait for events
	driver_ready(id);
    while (true) {

        // Process any server events
        bool did_work = driver_server.process_next();
        did_work |= irq_server.poll();

        // Dont busy wait
        if (!did_work)
            yield();
    }
}