/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2021-2024, Olivier Martin <olivier@labapart.org>
 */

#ifndef __GATTLIB_INTERNAL_DEFS_H__
#define __GATTLIB_INTERNAL_DEFS_H__

#include <stdbool.h>
#include <glib.h>

#if defined(WITH_PYTHON)
	#include <Python.h>
#endif

#include "gattlib.h"

struct gattlib_python_args {
	PyObject* callback;
	PyObject* args;
};

struct gattlib_handler {
	union {
		gattlib_discovered_device_t discovered_device;
		gatt_connect_cb_t connection_handler;
		gattlib_event_handler_t notification_handler;
		gattlib_disconnection_handler_t disconnection_handler;
		void (*callback)(void);
	} callback;

	void* user_data;
	// We create a thread to ensure the callback is not blocking the mainloop
	GThread *thread;
	// In case of Python callback and argument, we keep track to free it when we stopped to discover BLE devices
	void* python_args;
};

struct _gatt_connection_t {
	void* context;

	struct gattlib_handler on_connection;
	struct gattlib_handler on_connection_error;
	struct gattlib_handler notification;
	struct gattlib_handler indication;
	struct gattlib_handler on_disconnection;
};

void gattlib_handler_dispatch_to_thread(struct gattlib_handler* handler, void (*python_callback)(),
		GThreadFunc thread_func, const char* thread_name, void* (*thread_args_allocator)(va_list args), ...);
void gattlib_handler_free(struct gattlib_handler* handler);
bool gattlib_has_valid_handler(struct gattlib_handler* handler);

#if defined(WITH_PYTHON)
// Callback used by Python to create arguments used by native callback
void* gattlib_python_callback_args(PyObject* python_callback, PyObject* python_args);

/**
 * These functions are called by Python wrapper
 */
void gattlib_discovered_device_python_callback(void *adapter, const char* addr, const char* name, void *user_data);
void gattlib_connected_device_python_callback(void *adapter, const char *dst, gatt_connection_t* connection, int error, void* user_data);
void gattlib_disconnected_device_python_callback(gatt_connection_t* connection, void *user_data);
void gattlib_notification_device_python_callback(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data);
#endif

#endif
