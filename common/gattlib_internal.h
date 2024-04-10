/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2021-2024, Olivier Martin <olivier@labapart.org>
 */

#ifndef __GATTLIB_INTERNAL_H__
#define __GATTLIB_INTERNAL_H__

#include <stdbool.h>
#include <glib.h>

#if defined(WITH_PYTHON)
	#include <Python.h>
#endif

#include "gattlib.h"
#include "gattlib_backend.h"

#if defined(WITH_PYTHON)
struct gattlib_python_args {
	PyObject* callback;
	PyObject* args;
};
#endif

#define GATTLIB_SIGNAL_DEVICE_DISCONNECTION		(1 << 0)
#define GATTLIB_SIGNAL_ADAPTER_STOP_SCANNING    (1 << 1)

struct gattlib_signal {
	// Used by gattlib_disconnection when we want to wait for the disconnection to be effective
	GCond condition;
	// Mutex for condition
	GMutex mutex;
	// Identify the gattlib signals
	uint32_t signals;
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
	// Thread pool
	GThreadPool *thread_pool;
#if defined(WITH_PYTHON)
	// In case of Python callback and argument, we keep track to free it when we stopped to discover BLE devices
	void* python_args;
#endif
};

enum _gattlib_device_state {
	NOT_FOUND = 0,
	CONNECTING,
	CONNECTED,
	DISCONNECTING,
	DISCONNECTED
};

struct _gattlib_adapter {
	// Context specific to the backend implementation (eg: dbus backend)
	struct _gattlib_adapter_backend backend;

	// BLE adapter id (could be its DBUS device path on Linux)
	char* id;

	// BLE adapter name
	char* name;

	// reference counter is used to know whether the adapter is still use by callback
	// When the reference counter is 0 then the adapter is freed
	uintptr_t reference_counter;

	// List of `gattlib_device_t`. This list allows to know weither a device is
	// discovered/disconnected/connecting/connected/disconnecting.
	GSList *devices;

	// Handler calls on discovered device
	struct gattlib_handler discovered_device_callback;
};

struct _gattlib_connection {
	struct _gattlib_device* device;

	// Context specific to the backend implementation (eg: dbus backend)
	struct _gattlib_connection_backend backend;

	struct gattlib_handler on_connection;
	struct gattlib_handler notification;
	struct gattlib_handler indication;
	struct gattlib_handler on_disconnection;
};

typedef struct _gattlib_device {
	struct _gattlib_adapter* adapter;
	// On some platform, the name could be a UUID, on others its the DBUS device path
	char* device_id;

	// reference counter is used to know whether the device is still use by callback
	// When the reference counter is 0 then the device is freed
	uintptr_t reference_counter;

	// We keep the state to prevent concurrent connecting/connected/disconnecting operation
	enum _gattlib_device_state state;

	struct _gattlib_connection connection;
} gattlib_device_t;

// This recursive mutex ensures all gattlib objects can be accessed in a multi-threaded environment
// The recursive mutex allows a same thread to lock twice the mutex without being blocked by itself.
extern GRecMutex m_gattlib_mutex;

// Keep track of the allocated adapters to avoid an adapter to be freed twice.
// It could happen when using Python wrapper.
extern GSList *m_adapter_list;

// This structure is used for inter-thread communication
extern struct gattlib_signal m_gattlib_signal;

gattlib_adapter_t* gattlib_adapter_from_id(const char* adapter_id);
bool gattlib_adapter_is_valid(gattlib_adapter_t* adapter);
bool gattlib_adapter_is_scanning(gattlib_adapter_t* adapter);
int gattlib_adapter_ref(gattlib_adapter_t* adapter);
int gattlib_adapter_unref(gattlib_adapter_t* adapter);

bool gattlib_device_is_valid(gattlib_device_t* device);
int gattlib_device_ref(gattlib_device_t* device);
int gattlib_device_unref(gattlib_device_t* device);
/**
 * This function is similar to 'gattlib_device_is_valid()' except we check if
 * the connection (connected or not) still belongs to a valid device.
 *
 * It is to avoid to use 'connection->device' when the device has been freed
 */
bool gattlib_connection_is_valid(gattlib_connection_t* connection);
bool gattlib_connection_is_connected(gattlib_connection_t* connection);

void gattlib_handler_dispatch_to_thread(struct gattlib_handler* handler, void (*python_callback)(),
		GThreadFunc thread_func, const char* thread_name, void* (*thread_args_allocator)(va_list args), ...);
void gattlib_handler_free(struct gattlib_handler* handler);
bool gattlib_has_valid_handler(struct gattlib_handler* handler);

void gattlib_notification_device_thread(gpointer data, gpointer user_data);

/**
 * Clean GATTLIB connection on disconnection
 *
 * This function is called by the disconnection callback to always be called on explicit
 * and implicit disconnection.
 */
void gattlib_connection_free(gattlib_connection_t* connection);

extern const char* device_state_str[];
gattlib_device_t* gattlib_device_get_device(gattlib_adapter_t* adapter, const char* device_id);
enum _gattlib_device_state gattlib_device_get_state(gattlib_adapter_t* adapter, const char* device_id);
int gattlib_device_set_state(gattlib_adapter_t* adapter, const char* device_id, enum _gattlib_device_state new_state);
int gattlib_devices_are_disconnected(gattlib_adapter_t* adapter);
int gattlib_devices_free(gattlib_adapter_t* adapter);

#ifdef DEBUG
void gattlib_adapter_dump_state(gattlib_adapter_t* adapter);
#endif

#if defined(WITH_PYTHON)
// Callback used by Python to create arguments used by native callback
void* gattlib_python_callback_args(PyObject* python_callback, PyObject* python_args);

/**
 * These functions are called by Python wrapper
 */
void gattlib_discovered_device_python_callback(gattlib_adapter_t* adapter, const char* addr, const char* name, void *user_data);
void gattlib_connected_device_python_callback(gattlib_adapter_t* adapter, const char *dst, gattlib_connection_t* connection, int error, void* user_data);
void gattlib_disconnected_device_python_callback(gattlib_connection_t* connection, void *user_data);
void gattlib_notification_device_python_callback(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data);
#endif

#endif
