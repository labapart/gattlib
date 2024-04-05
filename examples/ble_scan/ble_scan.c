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

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
#include <syslog.h>
#endif

#include "gattlib.h"

#define BLE_SCAN_TIMEOUT   10

static const char* adapter_name;

typedef void (*ble_discovered_device_t)(const char* addr, const char* name);

// We use a mutex to make the BLE connections synchronous
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

LIST_HEAD(listhead, connection_t) g_ble_connections;
struct connection_t {
	pthread_t thread;
	gattlib_adapter_t* adapter;
	char* addr;
	LIST_ENTRY(connection_t) entries;
};

static void on_device_connect(gattlib_adapter_t* adapter, const char *dst, gattlib_connection_t* connection, int error, void* user_data) {
	gattlib_primary_service_t* services;
	gattlib_characteristic_t* characteristics;
	int services_count, characteristics_count;
	char uuid_str[MAX_LEN_UUID_STR + 1];
	int ret, i;

	ret = gattlib_discover_primary(connection, &services, &services_count);
	if (ret != 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Fail to discover primary services.");
		goto disconnect_exit;
	}

	for (i = 0; i < services_count; i++) {
		gattlib_uuid_to_string(&services[i].uuid, uuid_str, sizeof(uuid_str));

		printf("service[%d] start_handle:%02x end_handle:%02x uuid:%s\n", i,
				services[i].attr_handle_start, services[i].attr_handle_end,
				uuid_str);
	}
	free(services);

	ret = gattlib_discover_char(connection, &characteristics, &characteristics_count);
	if (ret != 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Fail to discover characteristics.");
		goto disconnect_exit;
	}
	for (i = 0; i < characteristics_count; i++) {
		gattlib_uuid_to_string(&characteristics[i].uuid, uuid_str, sizeof(uuid_str));

		printf("characteristic[%d] properties:%02x value_handle:%04x uuid:%s\n", i,
				characteristics[i].properties, characteristics[i].value_handle,
				uuid_str);
	}
	free(characteristics);

disconnect_exit:
	gattlib_disconnect(connection, false /* wait_disconnection */);
}

static void *ble_connect_device(void *arg) {
	struct connection_t *connection = arg;
	char* addr = connection->addr;
	int ret;

	pthread_mutex_lock(&g_mutex);
	printf("------------START %s ---------------\n", addr);

	ret = gattlib_connect(connection->adapter, connection->addr, GATTLIB_CONNECTION_OPTIONS_NONE, on_device_connect, NULL);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to connect to the bluetooth device '%s'", connection->addr);
	}

	printf("------------DONE %s ---------------\n", addr);
	pthread_mutex_unlock(&g_mutex);
	return NULL;
}

static void ble_discovered_device(gattlib_adapter_t* adapter, const char* addr, const char* name, void *user_data) {
	struct connection_t *connection;
	int ret;

	if (name) {
		printf("Discovered %s - '%s'\n", addr, name);
	} else {
		printf("Discovered %s\n", addr);
	}

	connection = calloc(sizeof(struct connection_t), 1);
	if (connection == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failt to allocate connection.");
		return;
	}
	connection->addr = strdup(addr);
	connection->adapter = adapter;

	ret = pthread_create(&connection->thread, NULL,	ble_connect_device, connection);
	if (ret != 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failt to create BLE connection thread.");
		free(connection);
		return;
	}
	LIST_INSERT_HEAD(&g_ble_connections, connection, entries);
}

static void* ble_task(void* arg) {
	gattlib_adapter_t* adapter;
	int ret;

	ret = gattlib_adapter_open(adapter_name, &adapter);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to open adapter.");
		return NULL;
	}

	pthread_mutex_lock(&g_mutex);
	ret = gattlib_adapter_scan_enable(adapter, ble_discovered_device, BLE_SCAN_TIMEOUT, NULL /* user_data */);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to scan.");
		goto EXIT;
	}

	gattlib_adapter_scan_disable(adapter);

	puts("Scan completed");
	pthread_mutex_unlock(&g_mutex);

	// Wait for the thread to complete
	while (g_ble_connections.lh_first != NULL) {
		struct connection_t* connection = g_ble_connections.lh_first;
		pthread_join(connection->thread, NULL);
		LIST_REMOVE(g_ble_connections.lh_first, entries);
		free(connection->addr);
		free(connection);
	}

EXIT:
	gattlib_adapter_close(adapter);
	return NULL;
}

int main(int argc, const char *argv[]) {
	int ret;

	if (argc == 1) {
		adapter_name = NULL;
	} else if (argc == 2) {
		adapter_name = argv[1];
	} else {
		printf("%s [<bluetooth-adapter>]\n", argv[0]);
		return 1;
	}

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
	openlog("gattlib_ble_scan", LOG_CONS | LOG_NDELAY | LOG_PERROR, LOG_USER);
	setlogmask(LOG_UPTO(LOG_INFO));
#endif

	LIST_INIT(&g_ble_connections);

	ret = gattlib_mainloop(ble_task, NULL);
	if (ret != GATTLIB_SUCCESS) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create gattlib mainloop");
	}

	return ret;
}
