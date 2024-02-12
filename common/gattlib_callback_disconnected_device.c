/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

void gattlib_disconnected_device_python_callback(gatt_connection_t* connection, void *user_data) {
	struct gattlib_python_args* args = user_data;
	PyObject *result;
	PyGILState_STATE d_gstate;
	d_gstate = PyGILState_Ensure();

	PyObject *arglist = Py_BuildValue("(O)", args->args);
#if PYTHON_VERSION >= PYTHON_VERSIONS(3, 9)
	result = PyObject_Call(args->callback, arglist, NULL);
#else
	result = PyEval_CallObject(args->callback, arglist);
#endif
	Py_DECREF(arglist);

	if (result == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Python disconnection handler has raised an exception.");
		PyErr_Print();
	}

	PyGILState_Release(d_gstate);
}

static gpointer _gattlib_disconnected_device_thread(gpointer data) {
	gatt_connection_t* connection = data;

	connection->on_disconnection.callback.disconnection_handler(connection, connection->on_disconnection.user_data);
	return NULL;
}

static void* _disconnected_device_thread_args_allocator(va_list args) {
	gatt_connection_t* connection = va_arg(args, gatt_connection_t*);
	return connection;
}

void gattlib_on_disconnected_device(gatt_connection_t* connection) {
	gattlib_handler_dispatch_to_thread(
		&connection->on_disconnection,
		gattlib_disconnected_device_python_callback /* python_callback */,
		_gattlib_disconnected_device_thread /* thread_func */,
		"gattlib_disconnected_device" /* thread_name */,
		_disconnected_device_thread_args_allocator /* thread_args_allocator */,
		connection);
}
