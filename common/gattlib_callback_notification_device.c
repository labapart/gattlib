/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

void gattlib_notification_device_python_callback(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	struct gattlib_python_args* args = user_data;
	char uuid_str[MAX_LEN_UUID_STR + 1];
	PyGILState_STATE d_gstate;
	PyObject *result;
	int ret;

	ret = gattlib_uuid_to_string(uuid, uuid_str, sizeof(uuid_str));
	assert(ret == 0);

	d_gstate = PyGILState_Ensure();

	const char* argument_string;
	// We pass pointer into integer/long parameter. We need to check the address size of the platform
	if (sizeof(void*) == 8) {
		argument_string = "(sLIO)";
	} else {
		argument_string = "(sIIO)";
	}
	PyObject *arglist = Py_BuildValue(argument_string, uuid_str, data, data_length, args->args);
#if PYTHON_VERSION >= PYTHON_VERSIONS(3, 9)
	result = PyObject_Call(args->callback, arglist, NULL);
#else
	result = PyEval_CallObject(args->callback, arglist);
#endif
	Py_DECREF(arglist);

	if (result == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Python notification handler has raised an exception.");
		PyErr_Print();
	}

	PyGILState_Release(d_gstate);
}

struct gattlib_notification_device_thread_args {
	gatt_connection_t* connection;
	uuid_t* uuid;
	uint8_t* data;
	size_t data_length;
};

static gpointer _gattlib_notification_device_thread(gpointer data) {
	struct gattlib_notification_device_thread_args* args = data;

	args->connection->notification.callback.notification_handler(
		args->uuid, args->data, args->data_length,
		args->connection->notification.user_data
	);

	if (args->uuid != NULL) {
		free(args->uuid);
	}
	if (args->data != NULL) {
		free(args->data);
	}
	return NULL;
}

static void* _notification_device_thread_args_allocator(va_list args) {
	gatt_connection_t* connection = va_arg(args, gatt_connection_t*);
	const uuid_t* uuid = va_arg(args, const uuid_t*);
	const uint8_t* data = va_arg(args, const uint8_t*);
	size_t data_length = va_arg(args, size_t);

	struct gattlib_notification_device_thread_args* thread_args = malloc(sizeof(struct gattlib_notification_device_thread_args));
	thread_args->connection = connection;
	thread_args->uuid = malloc(sizeof(uuid_t));
	if (thread_args->uuid != NULL) {
		memcpy(thread_args->uuid, uuid, sizeof(uuid_t));
	}
	thread_args->data = malloc(data_length);
	if (thread_args->data != NULL) {
		memcpy(thread_args->data, data, data_length);
	}
	thread_args->data_length = data_length;

	return thread_args;
}

void gattlib_on_gatt_notification(gatt_connection_t* connection, const uuid_t* uuid, const uint8_t* data, size_t data_length) {
	gattlib_handler_dispatch_to_thread(
		&connection->on_connection,
		gattlib_notification_device_python_callback /* python_callback */,
		_gattlib_notification_device_thread /* thread_func */,
		"gattlib_notification_device" /* thread_name */,
		_notification_device_thread_args_allocator /* thread_args_allocator */,
		connection, uuid, data, data_length);
}
