/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
 */

#ifndef __GATTLIB_INTERNAL_H__
#define __GATTLIB_INTERNAL_H__

#include <assert.h>
#include <pthread.h>

#include "gattlib_internal_defs.h"
#include "gattlib.h"

#include "org-bluez-adaptater1.h"
#include "org-bluez-device1.h"
#include "org-bluez-gattcharacteristic1.h"
#include "org-bluez-gattdescriptor1.h"
#include "org-bluez-gattservice1.h"

#if defined(WITH_PYTHON)
	#include <Python.h>

	#define PYTHON_VERSIONS(major, minor)	(((major) << 8) | (minor))
	#define PYTHON_VERSION					PYTHON_VERSIONS(PYTHON_VERSION_MAJOR, PYTHON_VERSION_MINOR)
#endif

#include "bluez5/lib/uuid.h"

#define BLUEZ_VERSIONS(major, minor)	(((major) << 8) | (minor))
#define BLUEZ_VERSION					BLUEZ_VERSIONS(BLUEZ_VERSION_MAJOR, BLUEZ_VERSION_MINOR)

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	#include "org-bluez-battery1.h"
#endif

#define GATTLIB_DEFAULT_ADAPTER "hci0"

typedef struct {
	struct gattlib_adapter *adapter;

	char* device_object_path;
	OrgBluezDevice1* device;

	// ID of the timeout to know if we managed to connect to the device
	guint connection_timeout_id;

	// ID of the device property change signal
	guint on_handle_device_property_change_id;

	// List of DBUS Object managed by 'adapter->device_manager'
	GList *dbus_objects;

	// List of 'OrgBluezGattCharacteristic1*' which has an attached notification
	GList *notified_characteristics;
} gattlib_context_t;

struct gattlib_adapter {
	GDBusObjectManager *device_manager;

	OrgBluezAdapter1 *adapter_proxy;
	char* adapter_name;

	// The recursive mutex allows to ensure sensible operations are always covered by a mutex in a same thread
	GRecMutex mutex;

	// Internal attributes only needed during BLE scanning
	struct {
		int added_signal_id;
		int changed_signal_id;
		int removed_signal_id;

		size_t ble_scan_timeout;
		guint ble_scan_timeout_id;

		GThread *scan_loop_thread; // Thread used to run the '_scan_loop()' when non-blocking
		bool is_scanning;
		struct {
			GMutex mutex;
			GCond cond;
		} scan_loop;

		uint32_t enabled_filters;

		struct gattlib_handler discovered_device_callback;
	} ble_scan;

	// List of `struct _gattlib_device`. This list allows to know weither a device is
	// discovered/disconnected/connecting/connected/disconnecting.
	GSList *devices;
};

struct dbus_characteristic {
	union {
		OrgBluezGattCharacteristic1 *gatt;
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
		OrgBluezBattery1            *battery;
#endif
	};
	enum {
		TYPE_NONE = 0,
		TYPE_GATT,
		TYPE_BATTERY_LEVEL
	} type;
};

extern const uuid_t m_battery_level_uuid;

struct gattlib_adapter *init_default_adapter(void);
GDBusObjectManager *get_device_manager_from_adapter(struct gattlib_adapter *gattlib_adapter, GError **error);

void get_device_path_from_mac_with_adapter(OrgBluezAdapter1* adapter, const char *mac_address, char *object_path, size_t object_path_len);
void get_device_path_from_mac(const char *adapter_name, const char *mac_address, char *object_path, size_t object_path_len);
int get_bluez_device_from_mac(struct gattlib_adapter *adapter, const char *mac_address, OrgBluezDevice1 **bluez_device1);

struct dbus_characteristic get_characteristic_from_uuid(gatt_connection_t* connection, const uuid_t* uuid);

// Invoke when a new device has been discovered
void gattlib_on_discovered_device(struct gattlib_adapter* gattlib_adapter, OrgBluezDevice1* device1);
// Invoke when a new device is being connected
void gattlib_on_connected_device(gatt_connection_t* connection);
// Invoke when a new device is being disconnected
void gattlib_on_disconnected_device(gatt_connection_t* connection);
// Invoke when a new device receive a GATT notification
void gattlib_on_gatt_notification(gatt_connection_t* connection, const uuid_t* uuid, const uint8_t* data, size_t data_length);

void disconnect_all_notifications(gattlib_context_t* conn_context);

#endif
