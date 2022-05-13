/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2022, Olivier Martin <olivier@labapart.org>
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

	// These attributes are needed to handle incoming events from GLib
	pthread_t event_thread;
	GMainLoop *connection_loop;
	// ID of the timeout to know if we managed to connect to the device
	guint connection_timeout;

	// List of DBUS Object managed by 'adapter->device_manager'
	GList *dbus_objects;

	// List of 'OrgBluezGattCharacteristic1*' which has an attached notification
	GList *notified_characteristics;
} gattlib_context_t;

struct gattlib_adapter {
	GDBusObjectManager *device_manager;

	OrgBluezAdapter1 *adapter_proxy;
	char* adapter_name;

	// Internal attributes only needed during BLE scanning
	struct {
		// This list is used to stored discovered devices during BLE scan.
		// The list is freed when the BLE scanning is completed.
		GSList *discovered_devices;

		int added_signal_id;
		int changed_signal_id;

		size_t ble_scan_timeout;
		guint ble_scan_timeout_id;

		pthread_t thread; // Thread used to run the scan_loop
		GMainLoop *scan_loop;

		uint32_t enabled_filters;
		gattlib_discovered_device_t discovered_device_callback;
		void *discovered_device_user_data;
	} ble_scan;
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

gboolean stop_scan_func(gpointer data);

struct gattlib_adapter *init_default_adapter(void);
GDBusObjectManager *get_device_manager_from_adapter(struct gattlib_adapter *gattlib_adapter);

void get_device_path_from_mac_with_adapter(OrgBluezAdapter1* adapter, const char *mac_address, char *object_path, size_t object_path_len);
void get_device_path_from_mac(const char *adapter_name, const char *mac_address, char *object_path, size_t object_path_len);
int get_bluez_device_from_mac(struct gattlib_adapter *adapter, const char *mac_address, OrgBluezDevice1 **bluez_device1);

struct dbus_characteristic get_characteristic_from_uuid(gatt_connection_t* connection, const uuid_t* uuid);

void disconnect_all_notifications(gattlib_context_t* conn_context);

#endif
