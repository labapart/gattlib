/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

#if defined(WITH_PYTHON)
void gattlib_notification_device_python_callback(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	struct gattlib_python_args* args = user_data;
	char uuid_str[MAX_LEN_UUID_STR + 1];
	PyGILState_STATE d_gstate;
	PyObject *result;
	int ret;

	ret = gattlib_uuid_to_string(uuid, uuid_str, sizeof(uuid_str));
	if (ret != 0) {
		return;
	}

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
#endif

struct gattlib_notification_device_thread_args {
	gattlib_connection_t* connection;
	uuid_t* uuid;
	uint8_t* data;
	size_t data_length;
};

void gattlib_notification_device_thread(gpointer data, gpointer user_data) {
	struct gattlib_notification_device_thread_args* args = data;
	struct gattlib_handler* handler = user_data;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_connected(args->connection)) {
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return;
	}

	handler->callback.notification_handler(
		args->uuid, args->data, args->data_length,
		handler->user_data
	);

	g_rec_mutex_unlock(&m_gattlib_mutex);

	if (args->uuid != NULL) {
		free(args->uuid);
		args->uuid = NULL;
	}
	if (args->data != NULL) {
		free(args->data);
		args->data = NULL;
	}
}

static void* _notification_device_thread_args_allocator(gattlib_connection_t* connection, const uuid_t* uuid, const uint8_t* data, size_t data_length) {
	struct gattlib_notification_device_thread_args* thread_args = calloc(sizeof(struct gattlib_notification_device_thread_args), 1);
	thread_args->connection = connection;
	thread_args->uuid = calloc(sizeof(uuid_t), 1);
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

void gattlib_on_gatt_notification(gattlib_connection_t* connection, const uuid_t* uuid, const uint8_t* data, size_t data_length) {
	GError *error = NULL;

	assert(connection->notification.thread_pool != NULL);

	void* arg = _notification_device_thread_args_allocator(connection, uuid, data, data_length);
	if (arg == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_on_gatt_notification: Failed to allocate arguments for thread");
		return;
	}
	g_thread_pool_push(connection->notification.thread_pool, arg, &error);
	if (error != NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_on_gatt_notification: Failed to push thread in pool: %s", error->message);
		g_error_free(error);
	}
}
