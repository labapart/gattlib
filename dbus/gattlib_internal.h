/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2019 Olivier Martin <olivier@labapart.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __GATTLIB_INTERNAL_H__
#define __GATTLIB_INTERNAL_H__

#include <assert.h>

#include "gattlib_internal_defs.h"
#include "gattlib.h"

#include "org-bluez-adaptater1.h"
#include "org-bluez-device1.h"
#include "org-bluez-gattcharacteristic1.h"
#include "org-bluez-gattdescriptor1.h"
#include "org-bluez-gattservice1.h"

#include "bluez5/lib/uuid.h"

#define BLUEZ_VERSIONS(major, minor)	(((major) << 8) | (minor))
#define BLUEZ_VERSION					BLUEZ_VERSIONS(BLUEZ_VERSION_MAJOR, BLUEZ_VERSION_MINOR)

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	#include "org-bluez-battery1.h"
#endif

typedef struct {
	char* device_object_path;
	OrgBluezDevice1* device;

	// This attribute is only used during the connection stage. By placing the attribute here, we can pass
	// `gatt_connection_t` to
	GMainLoop *connection_loop;
	// ID of the timeout to know if we managed to connect to the device
	guint connection_timeout;
} gattlib_context_t;

struct gattlib_adapter {
	OrgBluezAdapter1 *adapter_proxy;
	GMainLoop *scan_loop;
	guint timeout_id;
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

void get_device_path_from_mac_with_adapter(OrgBluezAdapter1* adapter, const char *mac_address, char *object_path, size_t object_path_len);
void get_device_path_from_mac(const char *adapter_name, const char *mac_address, char *object_path, size_t object_path_len);
int get_bluez_device_from_mac(struct gattlib_adapter *adapter, const char *mac_address, OrgBluezDevice1 **bluez_device1);

struct dbus_characteristic get_characteristic_from_uuid(gatt_connection_t* connection, const uuid_t* uuid);

#endif
