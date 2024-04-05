/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

#if defined(WITH_PYTHON)
void gattlib_discovered_device_python_callback(gattlib_adapter_t* adapter, const char* addr, const char* name, void *user_data) {
	struct gattlib_python_args* args = user_data;
	PyObject *result;

	// In case of Python support, we ensure we acquire the GIL (Global Intepreter Lock) to have
	// a thread-safe Python execution.
	PyGILState_STATE d_gstate = PyGILState_Ensure();

	const char* argument_string;
	// We pass pointer into integer/long parameter. We need to check the address size of the platform
	if (sizeof(void*) == 8) {
		argument_string = "(LssO)";
	} else {
		argument_string = "(IssO)";
	}
	PyObject *arglist = Py_BuildValue(argument_string, adapter, addr, name, args->args);
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
		GATTLIB_LOG(GATTLIB_ERROR, "Python discovered device handler has raised an exception.");
		PyErr_Print();
	}

ON_ERROR:
	PyGILState_Release(d_gstate);
}
#endif

struct gattlib_discovered_device_thread_args {
	struct _gattlib_adapter* gattlib_adapter;
	char* mac_address;
	char* name;
	OrgBluezDevice1* device1;
};

static gpointer _gattlib_discovered_device_thread(gpointer data) {
	struct gattlib_discovered_device_thread_args* args = data;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(args->gattlib_adapter)) {
		g_rec_mutex_unlock(&m_gattlib_mutex);
		goto EXIT;
	}

	if (!gattlib_has_valid_handler(&args->gattlib_adapter->discovered_device_callback)) {
		g_rec_mutex_unlock(&m_gattlib_mutex);
		goto EXIT;
	}

	// Increase adapter reference counter to ensure the adapter is not freed while
	// the callback is in use.
	gattlib_adapter_ref(args->gattlib_adapter);

	g_rec_mutex_unlock(&m_gattlib_mutex);

	args->gattlib_adapter->discovered_device_callback.callback.discovered_device(
		args->gattlib_adapter,
		args->mac_address, args->name,
		args->gattlib_adapter->discovered_device_callback.user_data
	);

	gattlib_adapter_unref(args->gattlib_adapter);

EXIT:
	free(args->mac_address);
	if (args->name != NULL) {
		free(args->name);
		args->name = NULL;
	}
	free(args);
	return NULL;
}

static void* _discovered_device_thread_args_allocator(va_list args) {
	gattlib_adapter_t* gattlib_adapter = va_arg(args, gattlib_adapter_t*);
	OrgBluezDevice1* device1 = va_arg(args, OrgBluezDevice1*);

	struct gattlib_discovered_device_thread_args* thread_args = calloc(sizeof(struct gattlib_discovered_device_thread_args), 1);
	thread_args->gattlib_adapter = gattlib_adapter;
	thread_args->mac_address = strdup(org_bluez_device1_get_address(device1));
	const char* device_name = org_bluez_device1_get_name(device1);
	if (device_name != NULL) {
		thread_args->name = strdup(device_name);
	} else {
		thread_args->name = NULL;
	}
	return thread_args;
}

void gattlib_on_discovered_device(gattlib_adapter_t* gattlib_adapter, OrgBluezDevice1* device1) {
	if (!gattlib_adapter_is_valid(gattlib_adapter)) {
		return;
	}

	gattlib_handler_dispatch_to_thread(
		&gattlib_adapter->discovered_device_callback,
#if defined(WITH_PYTHON)
		gattlib_discovered_device_python_callback /* python_callback */,
#else
		NULL, // No Python support. So we do not need to check the callback against Python callback
#endif
		_gattlib_discovered_device_thread /* thread_func */,
		"gattlib_discovered_device" /* thread_name */,
		_discovered_device_thread_args_allocator /* thread_args_allocator */,
		gattlib_adapter, device1);
}
