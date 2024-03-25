/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

#if defined(WITH_PYTHON)
void gattlib_connected_device_python_callback(void *adapter, const char *dst, gatt_connection_t* connection, int error, void* user_data) {
	struct gattlib_python_args* args = user_data;
	PyObject *result;

	// In case of Python support, we ensure we acquire the GIL (Global Intepreter Lock) to have
	// a thread-safe Python execution.
	PyGILState_STATE d_gstate = PyGILState_Ensure();

	const char* argument_string;
	// We pass pointer into integer/long parameter. We need to check the address size of the platform
	// arguments: (void *adapter, const char *dst, gatt_connection_t* connection, void* user_data)
	if (sizeof(void*) == 8) {
		argument_string = "(LsLIO)";
	} else {
		argument_string = "(IsIIO)";
	}
	PyObject *arglist = Py_BuildValue(argument_string, adapter, dst, connection, error, args->args);
	if (arglist == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Could not convert argument list to Python arguments");
		PyErr_Print();
		goto ON_ERROR;
	}
#if PYTHON_VERSION >= PYTHON_VERSIONS(3, 9)
	result = PyObject_Call(args->callback, arglist, NULL);
#else
	result = PyEval_CallObject(args->callback, arglist);
#endif
	Py_DECREF(arglist);

	if (result == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Python connection device handler has raised an exception.");
		PyErr_Print();
	}

ON_ERROR:
	PyGILState_Release(d_gstate);
}
#endif

static gpointer _gattlib_connected_device_thread(gpointer data) {
	gatt_connection_t* connection = data;
	gattlib_context_t* conn_context = connection->context;
	const gchar *device_mac_address = org_bluez_device1_get_address(conn_context->device);

	connection->on_connection.callback.connection_handler(
		conn_context->adapter, device_mac_address, connection, 0 /* no error */,
		connection->on_connection.user_data);
	return NULL;
}

static void* _connected_device_thread_args_allocator(va_list args) {
	gatt_connection_t* connection = va_arg(args, gatt_connection_t*);
	return connection;
}

void gattlib_on_connected_device(gatt_connection_t* connection) {
	gattlib_handler_dispatch_to_thread(
		&connection->on_connection,
#if defined(WITH_PYTHON)
		gattlib_connected_device_python_callback /* python_callback */,
#else
		NULL, // No Python support. So we do not need to check the callback against Python callback
#endif
		_gattlib_connected_device_thread /* thread_func */,
		"gattlib_connected_device" /* thread_name */,
		_connected_device_thread_args_allocator /* thread_args_allocator */,
		connection);
}
