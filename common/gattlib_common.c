/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2021-2024, Olivier Martin <olivier@labapart.org>
 */

#include <stdio.h>

#include "gattlib_internal.h"

int gattlib_register_notification(gatt_connection_t* connection, gattlib_event_handler_t notification_handler, void* user_data) {
	GError *error = NULL;

	if (connection == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	connection->notification.callback.notification_handler = notification_handler;
	connection->notification.user_data = user_data;

	connection->notification.thread_pool = g_thread_pool_new(
		gattlib_notification_device_thread,
		&connection->notification,
		1 /* max_threads */, FALSE /* exclusive */, &error);
	if (error != NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_register_notification: Failed to create thread pool: %s", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_INTERNAL;
	} else {
		assert(connection->notification.thread_pool != NULL);
		return GATTLIB_SUCCESS;
	}
}

int gattlib_register_indication(gatt_connection_t* connection, gattlib_event_handler_t indication_handler, void* user_data) {
	GError *error = NULL;

	if (connection == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}
	connection->indication.callback.notification_handler = indication_handler;
	connection->indication.user_data = user_data;

	connection->indication.thread_pool = g_thread_pool_new(
		gattlib_notification_device_thread,
		&connection->indication,
		1 /* max_threads */, FALSE /* exclusive */, &error);
	if (error != NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_register_indication: Failed to create thread pool: %s", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_INTERNAL;
	} else {
		return GATTLIB_SUCCESS;
	}
}

int gattlib_register_on_disconnect(gatt_connection_t *connection, gattlib_disconnection_handler_t handler, void* user_data) {
	if (connection == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}
	connection->on_disconnection.callback.disconnection_handler = handler;
	connection->on_disconnection.user_data = user_data;
	return GATTLIB_SUCCESS;
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

void gattlib_handler_free(struct gattlib_handler* handler) {
	// Reset callback to stop calling it after we stopped
	handler->callback.callback = NULL;

	if (handler->python_args != NULL) {
		struct gattlib_python_args* args = handler->python_args;
		Py_DECREF(args->callback);
		Py_DECREF(args->args);
		handler->python_args = NULL;
		free(handler->python_args);
		handler->python_args = NULL;
	}

	if (handler->thread_pool != NULL) {
		g_thread_pool_free(handler->thread_pool, FALSE /* immediate */, TRUE /* wait */);
		handler->thread_pool = NULL;
	}
}

bool gattlib_has_valid_handler(struct gattlib_handler* handler) {
	return (handler->callback.callback != NULL);
}

void gattlib_handler_dispatch_to_thread(struct gattlib_handler* handler, void (*python_callback)(),
		GThreadFunc thread_func, const char* thread_name, void* (*thread_args_allocator)(va_list args), ...) {
	GError *error = NULL;

	if (handler->callback.callback == NULL) {
		// We do not have (anymore) a callback, nothing to do
		return;
	}

	// Check if we are using the Python callback, in case of Python argument we keep track of the argument to free them
	// once we are done with the handler.
	if (handler->callback.callback == python_callback) {
		handler->python_args = handler->user_data;
	}

	// We create a thread to ensure the callback is not blocking the mainloop
	va_list args;
	va_start(args, thread_args_allocator);
	void* thread_args = thread_args_allocator(args);
	va_end(args);

	handler->thread = g_thread_try_new(thread_name, thread_func, thread_args, &error);
	if (handler->thread == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create thread '%s': %s", thread_name, error->message);
		g_error_free(error);
		return;
	}
}

// Helper function to free memory from Python frontend
void gattlib_free_mem(void *ptr) {
	if (ptr != NULL) {
		free(ptr);
	}
}
