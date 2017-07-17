/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2017 Olivier Martin <olivier@labapart.org>
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

#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <errno.h>

#include "gattlib_internal.h"

#include "uuid.h"
#include "att.h"
#include "gattrib.h"
#include "gatt.h"

struct gattlib_result_read_uuid_t {
	void*          buffer;
	size_t         buffer_max_len;
	size_t         buffer_len;
	gatt_read_cb_t callback;
	int            completed;
};

static void gattlib_result_read_uuid_cb(guint8 status, const guint8 *pdu, guint16 len, gpointer user_data) {
	struct gattlib_result_read_uuid_t* gattlib_result = user_data;
	struct att_data_list *list;
	int i;

	if (status == ATT_ECODE_ATTR_NOT_FOUND) {
		goto done;
	}

	if (status != 0) {
		fprintf(stderr, "Read characteristics by UUID failed: %s\n", att_ecode2str(status));
		goto done;
	}

	list = dec_read_by_type_resp(pdu, len);
	if (list == NULL) {
		goto done;
	}

	for (i = 0; i < list->num; i++) {
		uint8_t *value = list->data[i];

		value += 2;

		gattlib_result->buffer_len = list->len - 2;

		if (gattlib_result->callback) {
			gattlib_result->callback(value, gattlib_result->buffer_len);
		} else {
			memcpy(gattlib_result->buffer, value, MIN(gattlib_result->buffer_len, gattlib_result->buffer_max_len));
		}
	}

	att_data_list_free(list);

done:
	if (gattlib_result->callback) {
		free(gattlib_result);
	} else {
		gattlib_result->completed = TRUE;
	}
}

void uuid_to_bt_uuid(uuid_t* uuid, bt_uuid_t* bt_uuid) {
	memcpy(&bt_uuid->value, &uuid->value, sizeof(bt_uuid->value));
	if (uuid->type == SDP_UUID16) {
		bt_uuid->type = BT_UUID16;
	} else if (uuid->type == SDP_UUID32) {
		bt_uuid->type = BT_UUID32;
	} else if (uuid->type == SDP_UUID128) {
		bt_uuid->type = BT_UUID128;
	} else {
		bt_uuid->type = BT_UUID_UNSPEC;
	}
}

int gattlib_read_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid,
							void* buffer, size_t* buffer_len)
{
	gattlib_context_t* conn_context = connection->context;
	struct gattlib_result_read_uuid_t* gattlib_result;
	bt_uuid_t bt_uuid;
	const int start = 0x0001;
	const int end   = 0xffff;

	gattlib_result = malloc(sizeof(struct gattlib_result_read_uuid_t));
	if (gattlib_result == NULL) {
		return 1;
	}
	gattlib_result->buffer         = buffer;
	gattlib_result->buffer_max_len = *buffer_len;
	gattlib_result->buffer_len     = 0;
	gattlib_result->callback       = NULL;
	gattlib_result->completed      = FALSE;

	uuid_to_bt_uuid(uuid, &bt_uuid);

	gatt_read_char_by_uuid(conn_context->attrib, start, end, &bt_uuid,
							gattlib_result_read_uuid_cb, gattlib_result);

	// Wait for completion of the event
	while(gattlib_result->completed == FALSE) {
		gattlib_iteration();
	}

	*buffer_len = gattlib_result->buffer_len;

	free(gattlib_result);
	return 0;
}

int gattlib_read_char_by_uuid_async(gatt_connection_t* connection, uuid_t* uuid,
									gatt_read_cb_t gatt_read_cb)
{
	gattlib_context_t* conn_context = connection->context;
	struct gattlib_result_read_uuid_t* gattlib_result;
	const int start = 0x0001;
	const int end   = 0xffff;
	bt_uuid_t bt_uuid;

	gattlib_result = malloc(sizeof(struct gattlib_result_read_uuid_t));
	if (gattlib_result == NULL) {
		return 0;
	}
	gattlib_result->buffer         = NULL;
	gattlib_result->buffer_max_len = 0;
	gattlib_result->buffer_len     = 0;
	gattlib_result->callback       = gatt_read_cb;
	gattlib_result->completed      = FALSE;

	uuid_to_bt_uuid(uuid, &bt_uuid);

	guint id = gatt_read_char_by_uuid(conn_context->attrib, start, end, &bt_uuid,
								gattlib_result_read_uuid_cb, gattlib_result);

	if (id) {
		return 0;
	} else {
		return -1;
	}
}

void gattlib_write_result_cb(guint8 status, const guint8 *pdu, guint16 len, gpointer user_data) {
	int* write_completed = user_data;

	*write_completed = TRUE;
}

int gattlib_write_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len) {
	gattlib_context_t* conn_context = connection->context;
	int write_completed = FALSE;

	guint ret = gatt_write_char(conn_context->attrib, handle, (void*)buffer, buffer_len,
								gattlib_write_result_cb, &write_completed);
	if (ret == 0) {
		return 1;
	}

	// Wait for completion of the event
	while(write_completed == FALSE) {
		gattlib_iteration();
	}

	return 0;
}

void  gattlib_write_cmd_cb (gpointer data)
{
    int* write_completed = data;
    *write_completed = TRUE;
}

int gattlib_write_cmd_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len) {
	gattlib_context_t* conn_context = connection->context;
	int write_completed = FALSE;

	guint ret = gatt_write_cmd(conn_context->attrib, handle, (void*)buffer, buffer_len,
								gattlib_write_cmd_cb, &write_completed);
	if (ret == 0) {
		return 1;
	}

	// Wait for completion of the event
	while(write_completed == FALSE) {
		gattlib_iteration();
	}

	return 0;
}


int gattlib_write_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len) {
	uint16_t handle = 0;
	int ret;

	ret = get_handle_from_uuid(connection, uuid, &handle);
	if (ret) {
		fprintf(stderr, "Fail to find handle for UUID.\n");
		return ret;
	}

	return gattlib_write_char_by_handle(connection, handle, buffer, buffer_len);
}

int gattlib_notification_start(gatt_connection_t* connection, const uuid_t* uuid) {
	uint16_t handle;
	uint16_t enable_notification = 0x0001;

	int ret = get_handle_from_uuid(connection, uuid, &handle);
	if (ret) {
		return -1;
	}

	// Enable Status Notification
	return gattlib_write_char_by_handle(connection, handle + 1, &enable_notification, sizeof(enable_notification));
}

int gattlib_notification_stop(gatt_connection_t* connection, const uuid_t* uuid) {
	uint16_t handle;
	uint16_t enable_notification = 0x0000;

	int ret = get_handle_from_uuid(connection, uuid, &handle);
	if (ret) {
		return -1;
	}

	// Enable Status Notification
	return gattlib_write_char_by_handle(connection, handle + 1, &enable_notification, sizeof(enable_notification));
}
