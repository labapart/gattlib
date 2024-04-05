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

#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>

#include <glib.h>

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
#include <syslog.h>
#endif

#include "gattlib.h"

#define BLE_CONNECT_LOOP_COUNT  20
#define BLE_SCAN_TIMEOUT        180

static const char* adapter_name;
static const char* reference_mac_address;

// Declaration of thread condition variable
static struct {
	pthread_cond_t condition;
	pthread_mutex_t lock;
	bool value;
} m_connection_terminated;

static void on_device_connect(gattlib_adapter_t* adapter, const char *dst, gattlib_connection_t* connection, int error, void* user_data) {
	int ret;

	if (error != 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to connect to device '%s': Error %d", reference_mac_address, error);
		goto EXIT;
	}

	ret = gattlib_disconnect(connection, true /* wait_disconnection */);
	assert(ret == 0);

	GATTLIB_LOG(GATTLIB_DEBUG, "Bluetooth device '%s' should be disconnected.", reference_mac_address);

EXIT:
	pthread_mutex_lock(&m_connection_terminated.lock);
	m_connection_terminated.value = true;
	pthread_cond_signal(&m_connection_terminated.condition);
	pthread_mutex_unlock(&m_connection_terminated.lock);
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

	if (stricmp(addr, reference_mac_address) != 0) {
		return;
	}

	GATTLIB_LOG(GATTLIB_INFO, "Found bluetooth device '%s'", reference_mac_address);

	for (uintptr_t i = 0; i < BLE_CONNECT_LOOP_COUNT; i++) {
		GATTLIB_LOG(GATTLIB_INFO, "Connecting to the bluetooth device '%s' %d/%d", addr, i+1, BLE_CONNECT_LOOP_COUNT);

		memset(&m_connection_terminated, 0, sizeof(m_connection_terminated));

		// Try to connect while the connection is busy.
		while (true) {
			ret = gattlib_connect(adapter, addr, GATTLIB_CONNECTION_OPTIONS_NONE, on_device_connect, adapter);
			if (ret != GATTLIB_BUSY) {
				break;
			}

			GATTLIB_LOG(GATTLIB_DEBUG, "Failed to connect to the bluetooth device '%s' because busy. Try again", addr);
			g_usleep(100);
		}

		if (ret != GATTLIB_SUCCESS) {
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to connect to the bluetooth device '%s': %d", addr, ret);
			continue;
		}

		// Wait for the device to be connected
		pthread_mutex_lock(&m_connection_terminated.lock);
		while (!m_connection_terminated.value) {
			pthread_cond_wait(&m_connection_terminated.condition, &m_connection_terminated.lock);
		}
		pthread_mutex_unlock(&m_connection_terminated.lock);
	}
}

static void* ble_task(void* arg) {
	gattlib_adapter_t* adapter;
	int ret;

	ret = gattlib_adapter_open(adapter_name, &adapter);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to open adapter.");
		return NULL;
	}

	ret = gattlib_adapter_scan_enable(adapter, ble_discovered_device, BLE_SCAN_TIMEOUT, NULL /* user_data */);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to scan.");
		goto EXIT;
	}

	gattlib_adapter_scan_disable(adapter);

	GATTLIB_LOG(GATTLIB_INFO, "Scan completed");

EXIT:
	gattlib_adapter_close(adapter);
	return NULL;
}

int main(int argc, const char *argv[]) {
	int ret;

	if (argc == 1) {
		adapter_name = NULL;
	} else if (argc == 2) {
		reference_mac_address = argv[1];
	} else if (argc == 3) {
		adapter_name = argv[1];
		reference_mac_address = argv[2];
	} else {
		printf("%s [<bluetooth-adapter>] mac_address\n", argv[0]);
		return 1;
	}

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
	openlog("gattlib_ble_scan", LOG_CONS | LOG_NDELAY | LOG_PERROR, LOG_USER);
	setlogmask(LOG_UPTO(LOG_INFO));
#endif

	ret = gattlib_mainloop(ble_task, NULL);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create gattlib mainloop");
	}

	return ret;
}
