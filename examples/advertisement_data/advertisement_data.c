/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2021  Olivier Martin <olivier@labapart.org>
 *
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
#include <syslog.h>
#endif

#include "gattlib.h"

#define BLE_SCAN_TIMEOUT   60

static void ble_advertising_device(void *adapter, const char* addr, const char* name, void *user_data) {
	gattlib_advertisement_data_t *advertisement_data;
	size_t advertisement_data_count;
	uint16_t manufacturer_id;
	uint8_t *manufacturer_data;
	size_t manufacturer_data_size;
	int ret;

	ret = gattlib_get_advertisement_data_from_mac(adapter, addr,
			&advertisement_data, &advertisement_data_count,
			&manufacturer_id, &manufacturer_data, &manufacturer_data_size);
	if (ret != 0) {
		return;
	}

	if (name) {
		printf("Device %s - '%s': ", addr, name);
	} else {
		printf("Device %s: ", addr);
	}

	for (size_t i = 0; i < manufacturer_data_size; i++) {
		printf("%02x ", manufacturer_data[i]);
	}
	printf("\n");
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
		GATTLIB_LOG(GATTLIB_ERROR, "%s [<bluetooth-adapter>]", argv[0]);
		return 1;
	}

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
	openlog("gattlib_advertisement_dat", LOG_CONS | LOG_NDELAY | LOG_PERROR, LOG_USER);
	setlogmask(LOG_UPTO(LOG_INFO));
#endif

	ret = gattlib_adapter_open(adapter_name, &adapter);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to open adapter.");
		return 1;
	}

	ret = gattlib_adapter_scan_enable_with_filter(adapter,
			NULL, /* Do not filter on any specific Service UUID */
			0 /* RSSI Threshold */,
			GATTLIB_DISCOVER_FILTER_NOTIFY_CHANGE, /* Notify change of advertising data/RSSI */
			ble_advertising_device,
			0, /* timeout=0 means infinite loop */
			NULL /* user_data */);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to scan.");
		goto EXIT;
	}

	gattlib_adapter_scan_disable(adapter);

	puts("Scan completed");

EXIT:
	gattlib_adapter_close(adapter);
	return ret;
}
