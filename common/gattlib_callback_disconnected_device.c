/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

#if defined(WITH_PYTHON)
void gattlib_disconnected_device_python_callback(gattlib_connection_t* connection, void *user_data) {
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
#endif

void gattlib_on_disconnected_device(gattlib_connection_t* connection) {
	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_valid(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_on_disconnected_device: Device not valid");
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return;
	}

	if (gattlib_has_valid_handler(&connection->on_disconnection)) {
#if defined(WITH_PYTHON)
		// Check if we are using the Python callback, in case of Python argument we keep track of the argument to free them
		// once we are done with the handler.
		if ((gattlib_disconnection_handler_t)connection->on_disconnection.callback.callback == gattlib_disconnected_device_python_callback) {
			connection->on_disconnection.python_args = connection->on_disconnection.user_data;
		}
#endif

		// For GATT disconnection we do not use thread to ensure the callback is synchronous.
		connection->on_disconnection.callback.disconnection_handler(connection, connection->on_disconnection.user_data);
	}

	// Clean GATTLIB connection on disconnection
	gattlib_connection_free(connection);

	g_rec_mutex_unlock(&m_gattlib_mutex);

	// Signal the device is now disconnected
	g_mutex_lock(&m_gattlib_signal.mutex);
	m_gattlib_signal.signals |= GATTLIB_SIGNAL_DEVICE_DISCONNECTION;
	g_cond_broadcast(&m_gattlib_signal.condition);
	g_mutex_unlock(&m_gattlib_signal.mutex);
}
