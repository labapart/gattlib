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
	enum { READ, WRITE } operation;
	uuid_t uuid;
	long int value_data;
} m_argument;

// Declaration of thread condition variable
static pthread_cond_t m_connection_terminated = PTHREAD_COND_INITIALIZER;

// declaring mutex
static pthread_mutex_t m_connection_terminated_lock = PTHREAD_MUTEX_INITIALIZER;

static void usage(char *argv[]) {
	printf("%s <device_address> <read|write> <uuid> [<hex-value-to-write>]\n", argv[0]);
}

static void on_device_connect(void *adapter, const char *dst, gatt_connection_t* connection, int error, void* user_data) {
	int ret;
	size_t len;

	if (m_argument.operation == READ) {
		uint8_t *buffer = NULL;

		ret = gattlib_read_char_by_uuid(connection, &m_argument.uuid, (void **)&buffer, &len);
		if (ret != GATTLIB_SUCCESS) {
			char uuid_str[MAX_LEN_UUID_STR + 1];

			gattlib_uuid_to_string(&m_argument.uuid, uuid_str, sizeof(uuid_str));

			if (ret == GATTLIB_NOT_FOUND) {
				GATTLIB_LOG(GATTLIB_ERROR, "Could not find GATT Characteristic with UUID %s. "
					"You might call the program with '--gatt-discovery'.", uuid_str);
			} else {
				GATTLIB_LOG(GATTLIB_ERROR, "Error while reading GATT Characteristic with UUID %s (ret:%d)", uuid_str, ret);
			}
			goto EXIT;
		}

		printf("Read UUID completed: ");
		for (uintptr_t i = 0; i < len; i++) {
			printf("%02x ", buffer[i]);
		}
		printf("\n");

		gattlib_characteristic_free_value(buffer);
	} else {
		ret = gattlib_write_char_by_uuid(connection, &m_argument.uuid, &m_argument.value_data, sizeof(m_argument.value_data));
		if (ret != GATTLIB_SUCCESS) {
			char uuid_str[MAX_LEN_UUID_STR + 1];

			gattlib_uuid_to_string(&m_argument.uuid, uuid_str, sizeof(uuid_str));

			if (ret == GATTLIB_NOT_FOUND) {
				GATTLIB_LOG(GATTLIB_ERROR, "Could not find GATT Characteristic with UUID %s. "
					"You might call the program with '--gatt-discovery'.", uuid_str);
			} else {
				GATTLIB_LOG(GATTLIB_ERROR, "Error while writing GATT Characteristic with UUID %s (ret:%d)",
					uuid_str, ret);
			}
			goto EXIT;
		}
	}

EXIT:
	gattlib_disconnect(connection);

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

static void ble_discovered_device(void *adapter, const char* addr, const char* name, void *user_data) {
	int ret;

	if (stricmp(addr, m_argument.mac_address) != 0) {
		return;
	}

	GATTLIB_LOG(GATTLIB_INFO, "Found bluetooth device '%s'", m_argument.mac_address);

	ret = gattlib_connect(adapter, addr, GATTLIB_CONNECTION_OPTIONS_NONE, on_device_connect, NULL);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to connect to the bluetooth device '%s'", addr);
	}
}

static void* ble_task(void* arg) {
	char* addr = arg;
	void* adapter;
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

	if ((argc != 4) && (argc != 5)) {
		usage(argv);
		return 1;
	}

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
	openlog("gattlib_read_write", LOG_CONS | LOG_NDELAY | LOG_PERROR, LOG_USER);
	setlogmask(LOG_UPTO(LOG_INFO));
#endif

	m_argument.adapter_name = NULL;
	m_argument.mac_address = argv[1];

	if (strcmp(argv[2], "read") == 0) {
		m_argument.operation = READ;
	} else if ((strcmp(argv[2], "write") == 0) && (argc == 5)) {
		m_argument.operation = WRITE;

		if ((strlen(argv[4]) >= 2) && (argv[4][0] == '0') && ((argv[4][1] == 'x') || (argv[4][1] == 'X'))) {
			m_argument.value_data = strtol(argv[4], NULL, 16);
		} else {
			m_argument.value_data = strtol(argv[4], NULL, 0);
		}
		printf("Value to write: 0x%lx\n", m_argument.value_data);
	} else {
		usage(argv);
		return 1;
	}

	if (gattlib_string_to_uuid(argv[3], strlen(argv[3]) + 1, &m_argument.uuid) < 0) {
		usage(argv);
		return 1;
	}

	ret = gattlib_mainloop(ble_task, NULL);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create gattlib mainloop");
	}

	return 0;
}
