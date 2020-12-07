/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2020 Olivier Martin <olivier@labapart.org>
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

#include "gattlib_internal.h"


int gattlib_adapter_open(const char* adapter_name, void** adapter) {
	char object_path[20];
	OrgBluezAdapter1 *adapter_proxy;
	struct gattlib_adapter *gattlib_adapter;
	GError *error = NULL;

	if (adapter == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	if (adapter_name == NULL) {
		adapter_name = GATTLIB_DEFAULT_ADAPTER;
	}

	snprintf(object_path, sizeof(object_path), "/org/bluez/%s", adapter_name);

	adapter_proxy = org_bluez_adapter1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL, &error);
	if (adapter_proxy == NULL) {
		if (error) {
			fprintf(stderr, "Failed to get adapter %s: %s\n", object_path, error->message);
			g_error_free(error);
		} else {
			fprintf(stderr, "Failed to get adapter %s\n", object_path);
		}
		return GATTLIB_ERROR_DBUS;
	}

	// Ensure the adapter is powered on
	org_bluez_adapter1_set_powered(adapter_proxy, TRUE);

	gattlib_adapter = calloc(1, sizeof(struct gattlib_adapter));
	if (gattlib_adapter == NULL) {
		return GATTLIB_OUT_OF_MEMORY;
	}

	// Initialize stucture
	gattlib_adapter->adapter_name = strdup(adapter_name);
	gattlib_adapter->adapter_proxy = adapter_proxy;

	*adapter = gattlib_adapter;
	return GATTLIB_SUCCESS;
}

struct gattlib_adapter *init_default_adapter(void) {
	struct gattlib_adapter *gattlib_adapter;
	int ret;

	ret = gattlib_adapter_open(NULL, (void**)&gattlib_adapter);
	if (ret != GATTLIB_SUCCESS) {
		return NULL;
	} else {
		return gattlib_adapter;
	}
}

GDBusObjectManager *get_device_manager_from_adapter(struct gattlib_adapter *gattlib_adapter) {
	GError *error = NULL;

	if (gattlib_adapter->device_manager) {
		return gattlib_adapter->device_manager;
	}

	//
	// Get notification when objects are removed from the Bluez ObjectManager.
	// We should get notified when the connection is lost with the target to allow
	// us to advertise us again
	//
	gattlib_adapter->device_manager = g_dbus_object_manager_client_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			"/",
			NULL, NULL, NULL, NULL,
			&error);
	if (gattlib_adapter->device_manager == NULL) {
		if (error) {
			fprintf(stderr, "Failed to get Bluez Device Manager: %s\n", error->message);
			g_error_free(error);
		} else {
			fprintf(stderr, "Failed to get Bluez Device Manager.\n");
		}
		return NULL;
	}

	return gattlib_adapter->device_manager;
}

/*
 * Internal structure to pass to Device Manager signal handlers
 */
struct discovered_device_arg {
	void *adapter;
	uint32_t enabled_filters;
	gattlib_discovered_device_t callback;
	void *user_data;
	GSList** discovered_devices_ptr;
};

static void device_manager_on_device1_signal(const char* device1_path, struct discovered_device_arg *arg)
{
	GError *error = NULL;
	OrgBluezDevice1* device1 = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			device1_path,
			NULL,
			&error);
	if (error) {
		fprintf(stderr, "Failed to connection to new DBus Bluez Device: %s\n",
			error->message);
		g_error_free(error);
	}

	if (device1) {
		const gchar *address = org_bluez_device1_get_address(device1);

		// Check if the device is already part of the list
		GSList *item = g_slist_find_custom(*arg->discovered_devices_ptr, address, (GCompareFunc)g_ascii_strcasecmp);

		// First time this device is in the list
		if (item == NULL) {
			// Add the device to the list
			*arg->discovered_devices_ptr = g_slist_append(*arg->discovered_devices_ptr, g_strdup(address));
		}

		if ((item == NULL) || (arg->enabled_filters & GATTLIB_DISCOVER_FILTER_NOTIFY_CHANGE)) {
			arg->callback(
				arg->adapter,
				org_bluez_device1_get_address(device1),
				org_bluez_device1_get_name(device1),
				arg->user_data);
		}
		g_object_unref(device1);
	}
}

static void on_dbus_object_added(GDBusObjectManager *device_manager,
                     GDBusObject        *object,
                     gpointer            user_data)
{
	const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
	GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.Device1");
	if (!interface) {
		return;
	}

	// It is a 'org.bluez.Device1'
	device_manager_on_device1_signal(object_path, user_data);

	g_object_unref(interface);
}

static void
on_interface_proxy_properties_changed (GDBusObjectManagerClient *device_manager,
                                       GDBusObjectProxy         *object_proxy,
                                       GDBusProxy               *interface_proxy,
                                       GVariant                 *changed_properties,
                                       const gchar *const       *invalidated_properties,
                                       gpointer                  user_data)
{
	// Check if the object is a 'org.bluez.Device1'
	if (strcmp(g_dbus_proxy_get_interface_name(interface_proxy), "org.bluez.Device1") != 0) {
		return;
	}

	// It is a 'org.bluez.Device1'
	device_manager_on_device1_signal(g_dbus_proxy_get_object_path(interface_proxy), user_data);
}

int gattlib_adapter_scan_enable_with_filter(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
		gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	struct gattlib_adapter *gattlib_adapter = adapter;
	GDBusObjectManager *device_manager;
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;
	int added_signal_id, changed_signal_id;
	GSList *discovered_devices = NULL;
	GVariantBuilder arg_properties_builder;
	GVariant *rssi_variant = NULL;

	g_variant_builder_init(&arg_properties_builder, G_VARIANT_TYPE("a{sv}"));

	if (enabled_filters & GATTLIB_DISCOVER_FILTER_USE_UUID) {
		char uuid_str[MAX_LEN_UUID_STR + 1];
		GVariantBuilder list_uuid_builder;

		g_variant_builder_init(&list_uuid_builder, G_VARIANT_TYPE ("as"));

		for (uuid_t **uuid_ptr = uuid_list; *uuid_ptr != NULL; uuid_ptr++) {
			gattlib_uuid_to_string(*uuid_ptr, uuid_str, sizeof(uuid_str));
			g_variant_builder_add(&list_uuid_builder, "s", uuid_str);
		}

		g_variant_builder_add(&arg_properties_builder, "{sv}", "UUIDs", g_variant_builder_end(&list_uuid_builder));
	}

	if (enabled_filters & GATTLIB_DISCOVER_FILTER_USE_RSSI) {
		GVariant *rssi_variant = g_variant_new_int16(rssi_threshold);
		g_variant_builder_add(&arg_properties_builder, "{sv}", "RSSI", rssi_variant);
	}

	org_bluez_adapter1_call_set_discovery_filter_sync(gattlib_adapter->adapter_proxy,
			g_variant_builder_end(&arg_properties_builder), NULL, &error);

	if (rssi_variant) {
		g_variant_unref(rssi_variant);
	}

	if (error) {
		printf("error: %d.%d\n", error->domain, error->code);
		fprintf(stderr, "Failed to set discovery filter: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	//
	// Get notification when objects are removed from the Bluez ObjectManager.
	// We should get notified when the connection is lost with the target to allow
	// us to advertise us again
	//
	device_manager = get_device_manager_from_adapter(gattlib_adapter);
	if (device_manager == NULL) {
		goto DISABLE_SCAN;
	}

	// Pass the user callback and the discovered device list pointer to the signal handlers
	struct discovered_device_arg discovered_device_arg = {
		.adapter = adapter,
		.enabled_filters = enabled_filters,
		.callback = discovered_device_cb,
		.user_data = user_data,
		.discovered_devices_ptr = &discovered_devices,
	};

	added_signal_id = g_signal_connect(G_DBUS_OBJECT_MANAGER(device_manager),
	                    "object-added",
	                    G_CALLBACK (on_dbus_object_added),
	                    &discovered_device_arg);

	// List for object changes to see if there are still devices around
	changed_signal_id = g_signal_connect(G_DBUS_OBJECT_MANAGER(device_manager),
					     "interface-proxy-properties-changed",
					     G_CALLBACK(on_interface_proxy_properties_changed),
					     &discovered_device_arg);

	// Now, start BLE discovery
	org_bluez_adapter1_call_start_discovery_sync(gattlib_adapter->adapter_proxy, NULL, &error);
	if (error) {
		fprintf(stderr, "Failed to start discovery: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	// Run Glib loop for 'timeout' seconds
	gattlib_adapter->scan_loop = g_main_loop_new(NULL, 0);
	if (timeout > 0) {
		gattlib_adapter->timeout_id = g_timeout_add_seconds(timeout, stop_scan_func, gattlib_adapter->scan_loop);
	}
	g_main_loop_run(gattlib_adapter->scan_loop);
	// At this point, either the timeout expired (and automatically was removed) or scan_disable was called, removing the timer.
	gattlib_adapter->timeout_id = 0;

	// Note: The function only resumes when loop timeout as expired or g_main_loop_quit has been called.

	g_signal_handler_disconnect(G_DBUS_OBJECT_MANAGER(device_manager), added_signal_id);
	g_signal_handler_disconnect(G_DBUS_OBJECT_MANAGER(device_manager), changed_signal_id);

DISABLE_SCAN:
	// Stop BLE device discovery
	gattlib_adapter_scan_disable(adapter);

	// Free discovered device list
	g_slist_foreach(discovered_devices, (GFunc)g_free, NULL);
	g_slist_free(discovered_devices);
	return ret;
}

int gattlib_adapter_scan_enable(void* adapter, gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	return gattlib_adapter_scan_enable_with_filter(adapter,
			NULL, 0 /* RSSI Threshold */,
			GATTLIB_DISCOVER_FILTER_USE_NONE,
			discovered_device_cb, timeout, user_data);
}

int gattlib_adapter_scan_disable(void* adapter) {
	struct gattlib_adapter *gattlib_adapter = adapter;

	if (gattlib_adapter->scan_loop) {
		GError *error = NULL;

		org_bluez_adapter1_call_stop_discovery_sync(gattlib_adapter->adapter_proxy, NULL, &error);
		// Ignore the error

		// Remove timeout
		if (gattlib_adapter->timeout_id) {
			g_source_remove(gattlib_adapter->timeout_id);
			gattlib_adapter->timeout_id = 0;
		}

		// Ensure the scan loop is quit
		if (g_main_loop_is_running(gattlib_adapter->scan_loop)) {
			g_main_loop_quit(gattlib_adapter->scan_loop);
		}
		g_main_loop_unref(gattlib_adapter->scan_loop);
		gattlib_adapter->scan_loop = NULL;
	}

	return GATTLIB_SUCCESS;
}

int gattlib_adapter_close(void* adapter)
{
	struct gattlib_adapter *gattlib_adapter = adapter;

	g_object_unref(gattlib_adapter->device_manager);
	g_object_unref(gattlib_adapter->adapter_proxy);
	free(gattlib_adapter);

	return GATTLIB_SUCCESS;
}

gboolean stop_scan_func(gpointer data)
{
	g_main_loop_quit(data);
	return FALSE;
}
