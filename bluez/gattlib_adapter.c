#include "gattlib_internal.h"

#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#define LE_SCAN_PASSIVE                 0x00
#define LE_SCAN_ACTIVE                  0x01

/* These LE scan and inquiry parameters were chosen according to LE General
 * Discovery Procedure specification.
 */
#define DISCOV_LE_SCAN_WIN              0x12
#define DISCOV_LE_SCAN_INT              0x12

#define BLE_EVENT_TYPE     0x05
#define BLE_SCAN_RESPONSE  0x04

#define EIR_NAME_SHORT     0x08  /* shortened local name */
#define EIR_NAME_COMPLETE  0x09  /* complete local name */

int gattlib_adapter_open(const char* adapter_name, void** adapter) {
	int dev_id;

	if (adapter_name) {
		dev_id = hci_devid(adapter_name);
	} else {
		dev_id = hci_get_route(NULL);
	}

	if (dev_id < 0) {
		fprintf(stderr, "ERROR: Invalid device.\n");
		return 1;
	}

	int* device_desc = malloc(sizeof(int));
	if (device_desc == NULL) {
		return 2;
	} else {
		*adapter = device_desc;
	}

	*device_desc = hci_open_dev(dev_id);
	if (device_desc < 0) {
		fprintf(stderr, "ERROR: Could not open device.\n");
		return 3;
	}

	return 0;
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

static int ble_scan(int device_desc, gattlib_discovered_device_t discovered_device_cb, int timeout) {
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

int gattlib_adapter_scan_enable(void* adapter, gattlib_discovered_device_t discovered_device_cb, int timeout) {
	int device_desc = *(int*)adapter;

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

	ret = ble_scan(device_desc, discovered_device_cb, timeout);
	if (ret != 0) {
		fprintf(stderr, "ERROR: Advertisement fail.\n");
		return 1;
	}

	return 0;
}

int gattlib_adapter_scan_disable(void* adapter) {
	int device_desc = *(int*)adapter;

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

int gattlib_adapter_close(void* adapter) {
	hci_close_dev(*(int*)adapter);
	free(adapter);
	return 0;
}
