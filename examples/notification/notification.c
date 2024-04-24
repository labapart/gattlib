/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2024  Olivier Martin <olivier@labapart.org>
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

#include <assert.h>
#include <ctype.h>
#include <glib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
#include <syslog.h>
#endif

#include "gattlib.h"

#define BLE_SCAN_TIMEOUT   10

static struct {
	char *adapter_name;
	char* mac_address;
	uuid_t gatt_notification_uuid;
	uuid_t gatt_write_uuid;
	long int gatt_write_data;
} m_argument;

// Declaration of thread condition variable
static pthread_cond_t m_connection_terminated = PTHREAD_COND_INITIALIZER;

// declaring mutex
static pthread_mutex_t m_connection_terminated_lock = PTHREAD_MUTEX_INITIALIZER;

static void usage(char *argv[]) {
	printf("%s <device_address> <notification_characteristic_uuid> [<write_characteristic_uuid> <write_characteristic_data>]\n", argv[0]);
}

static void notification_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	uintptr_t i;

	printf("Notification Handler: ");

	for (i = 0; i < data_length; i++) {
		printf("%02x ", data[i]);
	}
	printf("\n");
}

static void on_device_connect(gattlib_adapter_t* adapter, const char *dst, gattlib_connection_t* connection, int error, void* user_data) {
	int ret;

	if (m_argument.gatt_write_data != 0) {
		ret = gattlib_write_char_by_uuid(connection, &m_argument.gatt_write_uuid, &m_argument.gatt_write_data, 1);
		if (ret != GATTLIB_SUCCESS) {
		}

	}

	ret = gattlib_register_notification(connection, notification_handler, NULL);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Fail to register notification callback.");
		goto EXIT;
	}

	ret = gattlib_notification_start(connection, &m_argument.gatt_notification_uuid);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Fail to start notification.");
		goto EXIT;
	}

	GATTLIB_LOG(GATTLIB_INFO, "Wait for notification for 20 seconds...");
	g_usleep(20 * G_USEC_PER_SEC);

EXIT:
	gattlib_disconnect(connection, false /* wait_disconnection */);

	pthread_mutex_lock(&m_connection_terminated_lock);
	pthread_cond_signal(&m_connection_terminated);
	pthread_mutex_unlock(&m_connection_terminated_lock);
}

static int stricmp(char const *a, char const *b) {
    for (;; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0 || !*a)
            return d;
    }
}

static void ble_discovered_device(gattlib_adapter_t* adapter, const char* addr, const char* name, void *user_data) {
	int ret;
	int16_t rssi;

	if (stricmp(addr, m_argument.mac_address) != 0) {
		return;
	}

	ret = gattlib_get_rssi_from_mac(adapter, addr, &rssi);
	if (ret == 0) {
		GATTLIB_LOG(GATTLIB_INFO, "Found bluetooth device '%s' with RSSI:%d", m_argument.mac_address, rssi);
	} else {
		GATTLIB_LOG(GATTLIB_INFO, "Found bluetooth device '%s'", m_argument.mac_address);
	}

	ret = gattlib_connect(adapter, addr, GATTLIB_CONNECTION_OPTIONS_NONE, on_device_connect, NULL);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to connect to the bluetooth device '%s'", addr);
	}
}

static void* ble_task(void* arg) {
	char* addr = arg;
	gattlib_adapter_t* adapter;
	int ret;

	ret = gattlib_adapter_open(m_argument.adapter_name, &adapter);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to open adapter.");
		return NULL;
	}

	ret = gattlib_adapter_scan_enable(adapter, ble_discovered_device, BLE_SCAN_TIMEOUT, addr);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to scan.");
		return NULL;
	}

	// Wait for the device to be connected
	pthread_mutex_lock(&m_connection_terminated_lock);
	pthread_cond_wait(&m_connection_terminated, &m_connection_terminated_lock);
	pthread_mutex_unlock(&m_connection_terminated_lock);

	return NULL;
}

int main(int argc, char *argv[]) {
	int ret;

	if (argc < 3) {
		usage(argv);
		return 1;
	}

	if (gattlib_string_to_uuid(argv[2], strlen(argv[2]) + 1, &m_argument.gatt_notification_uuid) < 0) {
		usage(argv);
		return 1;
	}

	if (argc == 5) {
		if (gattlib_string_to_uuid(argv[3], strlen(argv[3]) + 1, &m_argument.gatt_write_uuid) < 0) {
			usage(argv);
			return 1;
		}

		sscanf(argv[4], "%ld", &m_argument.gatt_write_data);
	}

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
	openlog("gattlib_notification", LOG_CONS | LOG_NDELAY | LOG_PERROR, LOG_USER);
	setlogmask(LOG_UPTO(LOG_INFO));
#endif

	m_argument.adapter_name = NULL;
	m_argument.mac_address = argv[1];

	ret = gattlib_mainloop(ble_task, NULL);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create gattlib mainloop");
	}

	return 0;
}
