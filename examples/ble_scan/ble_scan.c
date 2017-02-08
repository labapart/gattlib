#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "gattlib.h"

#define LE_SCAN_PASSIVE                 0x00
#define LE_SCAN_ACTIVE                  0x01

/* These LE scan and inquiry parameters were chosen according to LE General
 * Discovery Procedure specification.
 */
#define DISCOV_LE_SCAN_WIN              0x12
#define DISCOV_LE_SCAN_INT              0x12

#define EIR_NAME_SHORT     0x08  /* shortened local name */
#define EIR_NAME_COMPLETE  0x09  /* complete local name */

#define BLE_EVENT_TYPE     0x05
#define BLE_SCAN_RESPONSE  0x04

#define BLE_SCAN_TIMEOUT   4

typedef void (*ble_discovered_device_t)(const char* addr, const char* name);

// We use a mutex to make the BLE connections synchronous
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

LIST_HEAD(listhead, connection_t) g_ble_connections;
struct connection_t {
	pthread_t thread;
	char* addr;
	LIST_ENTRY(connection_t) entries;
};

int device_desc;

static int ble_scan_enable(int device_desc) {
	uint16_t interval = htobs(DISCOV_LE_SCAN_INT);
	uint16_t window = htobs(DISCOV_LE_SCAN_WIN);
	uint8_t own_address_type = 0x00;
	uint8_t filter_policy = 0x00;

	int ret = hci_le_set_scan_parameters(device_desc, LE_SCAN_ACTIVE, interval, window, own_address_type, filter_policy, 10000);
	if (ret < 0) {
		fprintf(stderr, "ERROR: Set scan parameters failed (are you root?).\n");
		return 1;
	}

	ret = hci_le_set_scan_enable(device_desc, 0x01, 1, 10000);
	if (ret < 0) {
		fprintf(stderr, "ERROR: Enable scan failed.\n");
		return 1;
	}

	return 0;
}

static int ble_scan_disable(int device_desc) {
	if (device_desc == -1) {
		fprintf(stderr, "ERROR: Could not disable scan, not enabled yet.\n");
		return 1;
	}

	int result = hci_le_set_scan_enable(device_desc, 0x00, 1, 10000);
	if (result < 0) {
		fprintf(stderr, "ERROR: Disable scan failed.\n");
	}
	return result;
}

static char* parse_name(uint8_t* data, size_t size) {
	size_t offset = 0;

	while (offset < size) {
		uint8_t field_len = data[0];
		size_t name_len;

		if (field_len == 0 || offset + field_len > size)
			return NULL;

		switch (data[1]) {
		case EIR_NAME_SHORT:
		case EIR_NAME_COMPLETE:
			name_len = field_len - 1;
			if (name_len > size)
				return NULL;

			return strndup((const char*)(data + 2), name_len);
		}

		offset += field_len + 1;
		data += field_len + 1;
	}

	return NULL;
}

static int ble_scan(int device_desc, ble_discovered_device_t discovered_device_cb, int timeout) {
	struct hci_filter old_options;
	socklen_t slen = sizeof(old_options);
	struct hci_filter new_options;
	unsigned char buffer[HCI_MAX_EVENT_SIZE];
	evt_le_meta_event* meta = (evt_le_meta_event*)(buffer + HCI_EVENT_HDR_SIZE + 1);
	le_advertising_info* info;
	char addr[18];
	int len;
#if BLUEZ_VERSION_MAJOR == 4
	struct timeval wait;
	fd_set read_set;
#endif

	if (getsockopt(device_desc, SOL_HCI, HCI_FILTER, &old_options, &slen) < 0) {
		fprintf(stderr, "ERROR: Could not get socket options.\n");
		return 1;
	}

	hci_filter_clear(&new_options);
	hci_filter_set_ptype(HCI_EVENT_PKT, &new_options);
	hci_filter_set_event(EVT_LE_META_EVENT, &new_options);

	if (setsockopt(device_desc, SOL_HCI, HCI_FILTER,
				   &new_options, sizeof(new_options)) < 0) {
		fprintf(stderr, "ERROR: Could not set socket options.\n");
		return 1;
	}

#if BLUEZ_VERSION_MAJOR == 4
	wait.tv_sec = timeout;
	int ts = time(NULL);

	while(1) {
		FD_ZERO(&read_set);
		FD_SET(device_desc, &read_set);

		int err = select(FD_SETSIZE, &read_set, NULL, NULL, &wait);
		if (err <= 0) {
			break;
		}

		len = read(device_desc, buffer, sizeof(buffer));

		if (meta->subevent != 0x02 || (uint8_t)buffer[BLE_EVENT_TYPE] != BLE_SCAN_RESPONSE)
			continue;

		info = (le_advertising_info*) (meta->data + 1);
		ba2str(&info->bdaddr, addr);

		char* name = parse_name(info->data, info->length);
		discovered_device_cb(addr, name);
		if (name) {
			free(name);
		}

		int elapsed = time(NULL) - ts;
		if (elapsed >= timeout) {
			printf("Err2");
			break;
		}

		wait.tv_sec = timeout - elapsed;
	}
#else
	while (1) {
		struct pollfd fds;
		fds.fd     = device_desc;
		fds.events = POLLIN;

		int err = poll(&fds, 1, timeout * 1000);
		if (err <= 0) {
			break;
		} else if ((fds.revents & POLLIN) == 0) {
			continue;
		}

		len = read(device_desc, buffer, sizeof(buffer));

		if (meta->subevent != 0x02 || (uint8_t)buffer[BLE_EVENT_TYPE] != BLE_SCAN_RESPONSE)
			continue;

		info = (le_advertising_info*) (meta->data + 1);
		ba2str(&info->bdaddr, addr);

		char* name = parse_name(info->data, info->length);
		discovered_device_cb(addr, name);
		if (name) {
			free(name);
		}
	}
#endif

	setsockopt(device_desc, SOL_HCI, HCI_FILTER, &old_options, sizeof(old_options));
	return 0;
}

static void *ble_connect_device(void *arg) {
	struct connection_t *connection = arg;
	char* addr = connection->addr;
	gatt_connection_t* gatt_connection;
	gattlib_primary_service_t* services;
	gattlib_characteristic_t* characteristics;
	int services_count, characteristics_count;
	char uuid_str[MAX_LEN_UUID_STR + 1];
	int ret, i;

	pthread_mutex_lock(&g_mutex);

	printf("------------START %s ---------------\n", addr);

	gatt_connection = gattlib_connect(NULL, addr, BDADDR_LE_PUBLIC, BT_SEC_LOW, 0, 0);
	if (gatt_connection == NULL) {
		gatt_connection = gattlib_connect(NULL, addr, BDADDR_LE_RANDOM, BT_SEC_LOW, 0, 0);
		if (gatt_connection == NULL) {
			fprintf(stderr, "Fail to connect to the bluetooth device.\n");
			//gattlib_disconnect(connection);
			goto connection_exit;
		} else {
			puts("Succeeded to connect to the bluetooth device with random address.");
		}
	} else {
		puts("Succeeded to connect to the bluetooth device.");
	}

	ret = gattlib_discover_primary(gatt_connection, &services, &services_count);
	if (ret != 0) {
		fprintf(stderr, "Fail to discover primary services.\n");
		return NULL;
	}

	for (i = 0; i < services_count; i++) {
		gattlib_uuid_to_string(&services[i].uuid, uuid_str, sizeof(uuid_str));

		printf("service[%d] start_handle:%02x end_handle:%02x uuid:%s\n", i,
				services[i].attr_handle_start, services[i].attr_handle_end,
				uuid_str);
	}
	free(services);

	ret = gattlib_discover_char(gatt_connection, &characteristics, &characteristics_count);
	if (ret != 0) {
		fprintf(stderr, "Fail to discover characteristics.\n");
		return NULL;
	}
	for (i = 0; i < characteristics_count; i++) {
		gattlib_uuid_to_string(&characteristics[i].uuid, uuid_str, sizeof(uuid_str));

		printf("characteristic[%d] properties:%02x value_handle:%04x uuid:%s\n", i,
				characteristics[i].properties, characteristics[i].value_handle,
				uuid_str);
	}
	free(characteristics);

	gattlib_disconnect(gatt_connection);

connection_exit:
	printf("------------DONE %s ---------------\n", addr);
	pthread_mutex_unlock(&g_mutex);
	return NULL;
}

static void ble_discovered_device(const char* addr, const char* name) {
	struct connection_t *connection;
	int ret;

	if (name) {
		printf("Discovered %s - '%s'\n", addr, name);
	} else {
		printf("Discovered %s\n", addr);
	}

	connection = malloc(sizeof(struct connection_t));
	if (connection == NULL) {
		fprintf(stderr, "Failt to allocate connection.\n");
		return;
	}
	connection->addr = strdup(addr);

	ret = pthread_create(&connection->thread, NULL,	ble_connect_device, connection);
	if (ret != 0) {
		fprintf(stderr, "Failt to create BLE connection thread.\n");
		free(connection);
		return;
	}
	LIST_INSERT_HEAD(&g_ble_connections, connection, entries);
}

int main(int argc, char *argv[]) {
	int dev_id;
	int ret;

	if (argc == 1) {
		dev_id = hci_get_route(NULL);
	} else if (argc == 2) {
		dev_id = hci_devid(argv[1]);
	} else {
		fprintf(stderr, "%s [<bluetooth-adapter>]\n", argv[0]);
		return 1;
	}
	if (dev_id < 0) {
		fprintf(stderr, "ERROR: Invalid device.\n");
		return 1;
	}

	LIST_INIT(&g_ble_connections);

	device_desc = hci_open_dev(dev_id);
	if (device_desc < 0) {
		fprintf(stderr, "ERROR: Could not open device.\n");
		return 1;
	}

	ret = ble_scan_enable(device_desc);
	if (ret != 0) {
		fprintf(stderr, "ERROR: Scanning fail.\n");
		return 1;
	}

	pthread_mutex_lock(&g_mutex);
	ret = ble_scan(device_desc, ble_discovered_device, BLE_SCAN_TIMEOUT);
	if (ret != 0) {
		fprintf(stderr, "ERROR: Advertisement fail.\n");
		return 1;
	}

	ble_scan_disable(device_desc);

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

	hci_close_dev(device_desc);
	return 0;
}
