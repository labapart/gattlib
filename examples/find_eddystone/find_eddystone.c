//#include <pthread.h>
//#include <stdio.h>
//#include <stdint.h>
//#include <stdlib.h>
//#include <sys/queue.h>

#include "gattlib.h"

#define BLE_SCAN_EDDYSTONE_TIMEOUT   20

/**
 * @brief Handler called on new discovered BLE device
 *
 * @param adapter is the adapter that has found the BLE device
 * @param addr is the MAC address of the BLE device
 * @param name is the name of BLE device if advertised
 * @param advertisement_data is an array of Service UUID and their respective data
 * @param advertisement_data_count is the number of elements in the advertisement_data array
 * @param manufacturer_id is the ID of the Manufacturer ID
 * @param manufacturer_data is the data following Manufacturer ID
 * @param manufacturer_data_size is the size of manufacturer_data
 * @param user_data  Data defined when calling `gattlib_register_on_disconnect()`
 */
void on_eddystone_found(void *adapter, const char* addr, const char* name,
		gattlib_advertisement_data_t *advertisement_data, size_t advertisement_data_count,
		uint16_t manufacturer_id, uint8_t *manufacturer_data, size_t manufacturer_data_size,
		void *user_data)
{
	puts("Found Eddystone device");

	// Look for Eddystone Common Data
	for (size_t i = 0; i < advertisement_data_count; i++) {
		gattlib_advertisement_data_t *advertisement_data_ptr = &advertisement_data[i];
		if (gattlib_uuid_cmp(&advertisement_data_ptr->uuid, &gattlib_eddystone_common_data_uuid) == GATTLIB_SUCCESS) {
			switch (advertisement_data_ptr->data[0]) {
			case EDDYSTONE_TYPE_UID:
				puts("\tEddystone UID");
				break;
			case EDDYSTONE_TYPE_URL:
				printf("\tEddystone URL %s%s (TX Power:%d)\n",
						gattlib_eddystone_url_scheme_prefix[advertisement_data_ptr->data[2]],
						advertisement_data_ptr->data + 3,
						advertisement_data_ptr->data[1]);
				break;
			case EDDYSTONE_TYPE_TLM:
				puts("\tEddystone TLM");
				break;
			case EDDYSTONE_TYPE_EID:
				puts("\tEddystone EID");
				break;
			default:
				fprintf(stderr, "\tEddystone ID %d not supported\n", advertisement_data_ptr->data[0]);
			}
		}
	}

	// We stop advertising on the first device
	gattlib_adapter_scan_disable(adapter);
}

int main(int argc, const char *argv[]) {
	const char* adapter_name;
	void* adapter;
	int ret;

	if (argc == 1) {
		adapter_name = NULL;
	} else if (argc == 2) {
		adapter_name = argv[1];
	} else {
		fprintf(stderr, "%s [<bluetooth-adapter>]\n", argv[0]);
		return 1;
	}

	ret = gattlib_adapter_open(adapter_name, &adapter);
	if (ret) {
		fprintf(stderr, "ERROR: Failed to open adapter.\n");
		return 1;
	}

	ret = gattlib_adapter_scan_eddystone(adapter,
			0, /* rssi_threshold. The value is not relevant as we do not pass GATTLIB_EDDYSTONE_LIMIT_RSSI */
			GATTLIB_EDDYSTONE_TYPE_URL,
			on_eddystone_found, BLE_SCAN_EDDYSTONE_TIMEOUT, NULL);
	if (ret) {
		fprintf(stderr, "ERROR: Failed to scan.\n");
		goto EXIT;
	}

	puts("Scan completed");

EXIT:
	gattlib_adapter_close(adapter);
	return ret;
}
