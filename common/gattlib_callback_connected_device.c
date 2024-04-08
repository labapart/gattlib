/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

#if defined(WITH_PYTHON)
void gattlib_connected_device_python_callback(gattlib_adapter_t* adapter, const char *dst, gattlib_connection_t* connection, int error, void* user_data) {
	struct gattlib_python_args* args = user_data;
	PyObject *result;

	// In case of Python support, we ensure we acquire the GIL (Global Intepreter Lock) to have
	// a thread-safe Python execution.
	PyGILState_STATE d_gstate = PyGILState_Ensure();

	const char* argument_string;
	// We pass pointer into integer/long parameter. We need to check the address size of the platform
	// arguments: (gattlib_adapter_t* adapter, const char *dst, gattlib_connection_t* connection, void* user_data)
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
	gattlib_connection_t* connection = data;
	const gchar *device_mac_address = org_bluez_device1_get_address(connection->backend.device);

	// Mutex to ensure the handler is valid
	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_connected(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "_gattlib_connected_device_thread: Device is not connected (state:%s)",
			device_state_str[connection->device->state]);
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return NULL;
	}

	if (!gattlib_has_valid_handler(&connection->on_connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "_gattlib_connected_device_thread: Handler is not valid");
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return NULL;
	}

	// Ensure we increment device reference counter to prevent the device/connection is freed during the execution
	gattlib_device_ref(connection->device);

	// We need to release the lock here to ensure the connection callback that is actually
	// doing the application sepcific work is not locking the BLE state.
	g_rec_mutex_unlock(&m_gattlib_mutex);

	connection->on_connection.callback.connection_handler(
		connection->device->adapter, device_mac_address, connection, 0 /* no error */,
		connection->on_connection.user_data);

	gattlib_device_unref(connection->device);
	return NULL;
}

static void* _connected_device_thread_args_allocator(va_list args) {
	gattlib_connection_t* connection = va_arg(args, gattlib_connection_t*);
	return connection;
}

void gattlib_on_connected_device(gattlib_connection_t* connection) {
	if (!gattlib_connection_is_valid(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_on_connected_device: Device is not valid");
		return;
	}

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
