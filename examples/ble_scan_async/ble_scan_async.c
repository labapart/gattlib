/*
 * ble_scan_async
 *
 * Copyright (C) 2017 Pat Deegan, psychogenic.com
 *
 *
 * Look 'ma, no threads!
 *
 * This is an async version of the ble_scan example, using relevant
 * async calls to scan/connect, and callbacks to implement the logic.
 *
 * The point of this sample is that threads could be used, 
 * for instance to drive the process_async() calls, but aren't 
 * required in order to get on with other business while the BLE 
 * stuff is happening in the background.
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

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <unistd.h>
#include "gattlib.h"


#define BLE_SCAN_TIMEOUT   5
#define MAINLOOP_SLEEP_US 10000

typedef void (*ble_discovered_device_t)(const char* addr, const char* name);


LIST_HEAD(listhead, connection_t) g_ble_connections;
struct connection_t {
	pthread_t thread;
	char* addr;
	LIST_ENTRY(connection_t) entries;
};

/*
 * ble_discovered_device -- callback used by scan to report devices.
 * Simply adds the device to our g_ble_connections list.
 */
static void ble_discovered_device(const char* addr, const char* name) {
	struct connection_t *connection;

	if (name) {
		fprintf(stderr, "Discovered %s - '%s'\n", addr, name);
	} else {
		fprintf(stderr, "Discovered %s\n", addr);
	}

	connection = malloc(sizeof(struct connection_t));
	if (connection == NULL) {
		fprintf(stderr, "Failed to allocate connection.\n");
		return;
	}
	connection->addr = strdup(addr);

	LIST_INSERT_HEAD(&g_ble_connections, connection, entries);
}



/*
 * adapter -- global used to store pointer to the BLE adapter
 */
void* adapter = NULL;

/*
 * AllDone -- global flag to indicate
 * program is finished.
 */
int AllDone = 0;

/* forward decl */
void connectAndDiscoverNext();

/* clear out connection from our list, once handled */
void removeCurrentConnectionFromList() {
		struct connection_t * connection = g_ble_connections.lh_first;
		LIST_REMOVE(g_ble_connections.lh_first, entries);
		free(connection->addr);
		free(connection);
}

/* process next scanned device */
void connectionMoveToNext() {

	removeCurrentConnectionFromList();
	connectAndDiscoverNext();
}

/* async connection completed callback: discovers the services
 * and chars of a device, after connection is established.
 *
 * This function actually calls blocking versions of the discovery
 * functions, as
 * 	a) I have yet to actually implement async versions;
 * 	b) using async versions would make the string of callbacks/async
 * 	   calls pretty unweildy; and
 * 	c) they seem pretty fast in any case.
 *
 * It does, however, use the async disconnect function, with it's callback
 * triggering the next connection+discovery.
 */
void connectionEstablishedCb(gatt_connection_t* gatt_connection) {
	gattlib_primary_service_t* services;
	gattlib_characteristic_t* characteristics;
	int services_count, characteristics_count;
	char uuid_str[MAX_LEN_UUID_STR + 1];
	int ret, i;

	if (! gatt_connection) {
		connectionMoveToNext();
		return;
	}

	struct connection_t* connection = g_ble_connections.lh_first;

	char* addr = connection->addr;

	fprintf(stderr, "\n------------START %s ---------------\n", addr);

	ret = gattlib_discover_primary(gatt_connection, &services, &services_count);
	if (ret != 0) {
		fprintf(stderr, "Fail to discover primary services.\n");
		goto disconnect_exit;
	}

	for (i = 0; i < services_count; i++) {
		gattlib_uuid_to_string(&services[i].uuid, uuid_str, sizeof(uuid_str));

		fprintf(stderr, "service[%d] start_handle:%02x end_handle:%02x uuid:%s\n", i,
				services[i].attr_handle_start, services[i].attr_handle_end,
				uuid_str);
	}
	free(services);

	ret = gattlib_discover_char(gatt_connection, &characteristics, &characteristics_count);
	if (ret != 0) {
		fprintf(stderr, "Fail to discover characteristics.\n");
		goto disconnect_exit;
	}
	for (i = 0; i < characteristics_count; i++) {
		gattlib_uuid_to_string(&characteristics[i].uuid, uuid_str, sizeof(uuid_str));

		fprintf(stderr, "characteristic[%d] properties:%02x value_handle:%04x uuid:%s\n", i,
				characteristics[i].properties, characteristics[i].value_handle,
				uuid_str);
	}
	free(characteristics);

disconnect_exit:
	gattlib_disconnect_async(gatt_connection, connectionMoveToNext);
	fprintf(stderr, "------------DONE %s ---------------\n", addr);

	
}

/*
 * connectAndDiscoverNext connect to, and then discover chars,
 * for next ble device in list.
 *
 */
void connectAndDiscoverNext() {

	if (g_ble_connections.lh_first == NULL) {
		AllDone = 1;
		fprintf(stderr, "No more connections to scan");
		return;
	}

	struct connection_t* connection = g_ble_connections.lh_first;

	char* addr = connection->addr;

	fprintf(stderr, "\nDoing async conn to next... @ %s\n" , addr);
	/* call async connect with connectionEstablishedCb callback */
	gattlib_connect_async(NULL, addr, BDADDR_LE_PUBLIC,
			BT_SEC_LOW, 0, 0, connectionEstablishedCb);


}

/*
 * scanDisabledCb -- called when scan is turned off,
 * triggers the start of connection+discovery chain.
 */
void scanDisabledCb() {
	fprintf(stderr, "\nscan now disabled!\n");
	connectAndDiscoverNext();
}

/*
 * scanCompleteCb -- called when our
 * async scanning interval times out, and calls
 * async scan disable.
 *
 */
void scanCompleteCb() {
	fprintf(stderr, "\nscanCompleteCb! startup scan disable\n");
	gattlib_adapter_scan_disable_async(adapter, scanDisabledCb);
}


int main(int argc, const char *argv[]) {
	const char* adapter_name;
	int ret;

	if (argc == 1) {
		adapter_name = NULL;
	} else if (argc == 2) {
		adapter_name = argv[1];
	} else {
		fprintf(stderr, "%s [<bluetooth-adapter>]\n", argv[0]);
		return 1;
	}

	LIST_INIT(&g_ble_connections);

	/* open the adapter */
	ret = gattlib_adapter_open(adapter_name, &adapter);
	if (ret) {
		fprintf(stderr, "ERROR: Failed to open adapter.\n");
		return 1;
	}

	/*
	 * Could use a state machine or whatever, but to keep things
	 * simple and demo the async stuff, we've setup a chain of
	 * callbacks, above.  Each one will trigger the next step.
	 *
	 * To get the ball rolling, we start by scanning the environment,
	 * with an async call to scan_enable.  It will keep scanning
	 * until BLE_SCAN_TIMEOUT expires, then trigger its "done
	 * callback", which will in turn call async scan_disable, etc.
	 */

	ret = gattlib_adapter_scan_enable_async(adapter,
				ble_discovered_device, BLE_SCAN_TIMEOUT, scanCompleteCb);

	/*
	 * Now comes our main loop.  In this toy example, I don't have much to
	 * do, but this is where you'd process user events, UI stuff, whatever.
	 *
	 * In this case, all we do is call one of the async_process functions,
	 * output the occasional character just to show we're still running, and
	 * spend lots of time sleeping.
	 *
	 * The point is that this loop is simple, with all the heavy lifting
	 * happening in callbacks, and no threads needed in this code.
	 */
	int counter = 0;
	while (! AllDone) {
		/* could do gattlib_async_process_all(), but since we're looping 'often' we
		 * just call:
		 */

		gattlib_async_process();

		if (counter++ == 5)
		{
			fprintf(stderr, "o");
		} else if (counter == 10 ) {
			counter = 0;
			fprintf(stderr, "O");
		}
		usleep(MAINLOOP_SLEEP_US);
	}

	fprintf(stderr, "\n\nOk, we're all done!\nGoodbye\n");
	gattlib_adapter_close(adapter);
	return 0;
}
