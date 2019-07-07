#include "gattlib_internal.h"

void gattlib_register_notification(gatt_connection_t* connection, gattlib_event_handler_t notification_handler, void* user_data) {
	connection->notification_handler = notification_handler;
	connection->notification_user_data = user_data;
}

void gattlib_register_indication(gatt_connection_t* connection, gattlib_event_handler_t indication_handler, void* user_data) {
	connection->indication_handler = indication_handler;
	connection->indication_user_data = user_data;
}

void bt_uuid_to_uuid(bt_uuid_t* bt_uuid, uuid_t* uuid) {
	memcpy(&uuid->value, &bt_uuid->value, sizeof(uuid->value));
	if (bt_uuid->type == BT_UUID16) {
		uuid->type = SDP_UUID16;
	} else if (bt_uuid->type == BT_UUID32) {
		uuid->type = SDP_UUID32;
	} else if (bt_uuid->type == BT_UUID128) {
		uuid->type = SDP_UUID128;
	} else {
		uuid->type = SDP_UUID_UNSPEC;
	}
}

int gattlib_uuid_to_string(const uuid_t *uuid, char *str, size_t n) {
	if (uuid->type == SDP_UUID16) {
		snprintf(str, n, "0x%.4x", uuid->value.uuid16);
	} else if (uuid->type == SDP_UUID32) {
		snprintf(str, n, "0x%.8x", uuid->value.uuid32);
	} else if (uuid->type == SDP_UUID128) {
		unsigned int data0;
		unsigned short data1;
		unsigned short data2;
		unsigned short data3;
		unsigned int data4;
		unsigned short data5;

		memcpy(&data0, &uuid->value.uuid128.data[0], 4);
		memcpy(&data1, &uuid->value.uuid128.data[4], 2);
		memcpy(&data2, &uuid->value.uuid128.data[6], 2);
		memcpy(&data3, &uuid->value.uuid128.data[8], 2);
		memcpy(&data4, &uuid->value.uuid128.data[10], 4);
		memcpy(&data5, &uuid->value.uuid128.data[14], 2);

		snprintf(str, n, "%.8x-%.4x-%.4x-%.4x-%.8x%.4x",
				ntohl(data0), ntohs(data1), ntohs(data2),
				ntohs(data3), ntohl(data4), ntohs(data5));
	} else {
		snprintf(str, n, "Unsupported type:%d", uuid->type);
		return -1;
	}
	return 0;
}

int gattlib_string_to_uuid(const char *str, size_t n, uuid_t *uuid) {
	bt_uuid_t bt_uuid;

	int ret = bt_string_to_uuid(&bt_uuid, str);
	if (ret == 0) {
		bt_uuid_to_uuid(&bt_uuid, uuid);
	}

	return ret;
}

int gattlib_uuid_cmp(const uuid_t *uuid1, const uuid_t *uuid2) {
	if (uuid1->type != uuid2->type) {
		return 1;
	} else if (uuid1->type == SDP_UUID16) {
		if (uuid1->value.uuid16 == uuid2->value.uuid16) {
			return 0;
		} else {
			return 2;
		}
	} else if (uuid1->type == SDP_UUID32) {
		if (uuid1->value.uuid32 == uuid2->value.uuid32) {
			return 0;
		} else {
			return 2;
		}
	} else if (uuid1->type == SDP_UUID128) {
		if (memcmp(&uuid1->value.uuid128, &uuid2->value.uuid128, sizeof(uuid1->value.uuid128)) == 0) {
			return 0;
		} else {
			return 2;
		}
	} else {
		return 3;
	}
}
