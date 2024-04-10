/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2021-2024  Olivier Martin <olivier@labapart.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
#include <syslog.h>
#endif

#include "gattlib.h"

#define BLE_SCAN_EDDYSTONE_TIMEOUT   20

const char* m_adapter_name;

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
void on_eddystone_found(gattlib_adapter_t* adapter, const char* addr, const char* name,
		gattlib_advertisement_data_t *advertisement_data, size_t advertisement_data_count,
		gattlib_manufacturer_data_t* manufacturer_data, size_t manufacturer_data_count,
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
				printf("\tEddystone ID %d not supported\n", advertisement_data_ptr->data[0]);
			}
		}
	}

	// We stop advertising on the first device
	gattlib_adapter_scan_disable(adapter);
}

static void* ble_task(void* arg) {
	gattlib_adapter_t* adapter = NULL;
	int ret;

	ret = gattlib_adapter_open(m_adapter_name, &adapter);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to open adapter.");
		return NULL;
	}

	ret = gattlib_adapter_scan_eddystone(adapter,
			0, /* rssi_threshold. The value is not relevant as we do not pass GATTLIB_EDDYSTONE_LIMIT_RSSI */
			GATTLIB_EDDYSTONE_TYPE_URL,
			on_eddystone_found, BLE_SCAN_EDDYSTONE_TIMEOUT, NULL);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to scan.");
		goto EXIT;
	}

	puts("Scan completed");

EXIT:
	gattlib_adapter_close(adapter);
	return NULL;
}

int main(int argc, const char *argv[]) {
	int ret;

	if (argc == 1) {
		m_adapter_name = NULL;
	} else if (argc == 2) {
		m_adapter_name = argv[1];
	} else {
		printf("%s [<bluetooth-adapter>]\n", argv[0]);
		return 1;
	}

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
	openlog("gattlib_find_eddystone", LOG_CONS | LOG_NDELAY | LOG_PERROR, LOG_USER);
	setlogmask(LOG_UPTO(LOG_INFO));
#endif

	ret = gattlib_mainloop(ble_task, NULL);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create gattlib mainloop");
	}

	return ret;
}
