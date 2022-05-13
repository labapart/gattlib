/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2022, Olivier Martin <olivier@labapart.org>
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
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to get adapter %s: %s", object_path, error->message);
			g_error_free(error);
		} else {
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to get adapter %s", object_path);
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
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to get Bluez Device Manager: %s", error->message);
			g_error_free(error);
		} else {
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to get Bluez Device Manager.");
		}
		return NULL;
	}

	return gattlib_adapter->device_manager;
}

static void device_manager_on_device1_signal(const char* device1_path, struct gattlib_adapter* gattlib_adapter)
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
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to connection to new DBus Bluez Device: %s",
			error->message);
		g_error_free(error);
	}

	if (device1) {
		const gchar *address = org_bluez_device1_get_address(device1);

		// Sometimes org_bluez_device1_get_address returns null addresses. If that's the case, early return.
		if (address == NULL) {
			g_object_unref(device1);
			return;
		}

		// Check if the device is already part of the list
		GSList *item = g_slist_find_custom(gattlib_adapter->ble_scan.discovered_devices, address, (GCompareFunc)g_ascii_strcasecmp);

		// First time this device is in the list
		if (item == NULL) {
			// Add the device to the list
			gattlib_adapter->ble_scan.discovered_devices = g_slist_append(gattlib_adapter->ble_scan.discovered_devices, g_strdup(address));
		}

		if ((item == NULL) || (gattlib_adapter->ble_scan.enabled_filters & GATTLIB_DISCOVER_FILTER_NOTIFY_CHANGE)) {
#if defined(WITH_PYTHON)
			// In case of Python support, we ensure we acquire the GIL (Global Intepreter Lock) to have
			// a thread-safe Python execution.
			PyGILState_STATE d_gstate;
			d_gstate = PyGILState_Ensure();
#endif

			gattlib_adapter->ble_scan.discovered_device_callback(
				gattlib_adapter,
				org_bluez_device1_get_address(device1),
				org_bluez_device1_get_name(device1),
				gattlib_adapter->ble_scan.discovered_device_user_data);

#if defined(WITH_PYTHON)
			PyGILState_Release(d_gstate);
#endif
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
		GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: on_object_added: %s (not 'org.bluez.Device1')", object_path);
		return;
	}

	GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: on_object_added: %s (has 'org.bluez.Device1')", object_path);

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
	GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: on_interface_proxy_properties_changed: interface:%s changed_properties:%s invalidated_properties:%s",
			g_dbus_proxy_get_interface_name(interface_proxy),
			g_variant_print(changed_properties, TRUE),
			invalidated_properties);

	// Check if the object is a 'org.bluez.Device1'
	if (strcmp(g_dbus_proxy_get_interface_name(interface_proxy), "org.bluez.Device1") != 0) {
		return;
	}

	// It is a 'org.bluez.Device1'
	device_manager_on_device1_signal(g_dbus_proxy_get_object_path(interface_proxy), user_data);
}

static void* _ble_scan_loop(void* args) {
	struct gattlib_adapter *gattlib_adapter = args;

	// Run Glib loop for 'timeout' seconds
	gattlib_adapter->ble_scan.scan_loop = g_main_loop_new(NULL, 0);
	if (gattlib_adapter->ble_scan.ble_scan_timeout > 0) {
		gattlib_adapter->ble_scan.ble_scan_timeout_id = g_timeout_add_seconds(gattlib_adapter->ble_scan.ble_scan_timeout,
			stop_scan_func, gattlib_adapter->ble_scan.scan_loop);
	}

	// And start the loop...
	g_main_loop_run(gattlib_adapter->ble_scan.scan_loop);
	// At this point, either the timeout expired (and automatically was removed) or scan_disable was called, removing the timer.
	gattlib_adapter->ble_scan.ble_scan_timeout_id = 0;

	// Note: The function only resumes when loop timeout as expired or g_main_loop_quit has been called.

	g_signal_handler_disconnect(G_DBUS_OBJECT_MANAGER(gattlib_adapter->device_manager), gattlib_adapter->ble_scan.added_signal_id);
	g_signal_handler_disconnect(G_DBUS_OBJECT_MANAGER(gattlib_adapter->device_manager), gattlib_adapter->ble_scan.changed_signal_id);

	// Ensure BLE device discovery is stopped
	gattlib_adapter_scan_disable(gattlib_adapter);

	// Free discovered device list
	g_slist_foreach(gattlib_adapter->ble_scan.discovered_devices, (GFunc)g_free, NULL);
	g_slist_free(gattlib_adapter->ble_scan.discovered_devices);
	return 0;
}

static int _gattlib_adapter_scan_enable_with_filter(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
		gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	struct gattlib_adapter *gattlib_adapter = adapter;
	GDBusObjectManager *device_manager;
	GError *error = NULL;
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
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to set discovery filter: %s (%d.%d)",
				error->message, error->domain, error->code);
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
		return GATTLIB_ERROR_DBUS;
	}

	// Clear BLE scan structure
	memset(&gattlib_adapter->ble_scan, 0, sizeof(gattlib_adapter->ble_scan));
	gattlib_adapter->ble_scan.enabled_filters = enabled_filters;
	gattlib_adapter->ble_scan.ble_scan_timeout = timeout;
	gattlib_adapter->ble_scan.discovered_device_callback = discovered_device_cb;
	gattlib_adapter->ble_scan.discovered_device_user_data = user_data;

	gattlib_adapter->ble_scan.added_signal_id = g_signal_connect(G_DBUS_OBJECT_MANAGER(device_manager),
	                    "object-added",
	                    G_CALLBACK (on_dbus_object_added),
	                    gattlib_adapter);

	// List for object changes to see if there are still devices around
	gattlib_adapter->ble_scan.changed_signal_id = g_signal_connect(G_DBUS_OBJECT_MANAGER(device_manager),
					     "interface-proxy-properties-changed",
					     G_CALLBACK(on_interface_proxy_properties_changed),
					     gattlib_adapter);

	// Now, start BLE discovery
	org_bluez_adapter1_call_start_discovery_sync(gattlib_adapter->adapter_proxy, NULL, &error);
	if (error) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to start discovery: %s", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	return GATTLIB_SUCCESS;
}

int gattlib_adapter_scan_enable_with_filter(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
		gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	int ret;

	ret = _gattlib_adapter_scan_enable_with_filter(adapter, uuid_list, rssi_threshold, enabled_filters,
		discovered_device_cb, timeout, user_data);
	if (ret != GATTLIB_SUCCESS) {
		return ret;
	}

	_ble_scan_loop(adapter);
	return 0;
}

int gattlib_adapter_scan_enable_with_filter_non_blocking(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
		gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	struct gattlib_adapter *gattlib_adapter = adapter;
	int ret;

	ret = _gattlib_adapter_scan_enable_with_filter(adapter, uuid_list, rssi_threshold, enabled_filters,
		discovered_device_cb, timeout, user_data);
	if (ret != GATTLIB_SUCCESS) {
		return ret;
	}

	ret = pthread_create(&gattlib_adapter->ble_scan.thread, NULL, _ble_scan_loop, gattlib_adapter);
	if (ret != 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failt to create BLE scan thread.");
		return ret;
	}

	return 0;
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

	if (gattlib_adapter->ble_scan.scan_loop) {
		GError *error = NULL;

		org_bluez_adapter1_call_stop_discovery_sync(gattlib_adapter->adapter_proxy, NULL, &error);
		// Ignore the error

		// Remove timeout
		if (gattlib_adapter->ble_scan.ble_scan_timeout_id) {
			g_source_remove(gattlib_adapter->ble_scan.ble_scan_timeout_id);
			gattlib_adapter->ble_scan.ble_scan_timeout_id = 0;
		}

		// Ensure the scan loop is quit
		if (g_main_loop_is_running(gattlib_adapter->ble_scan.scan_loop)) {
			g_main_loop_quit(gattlib_adapter->ble_scan.scan_loop);
		}
		g_main_loop_unref(gattlib_adapter->ble_scan.scan_loop);
		gattlib_adapter->ble_scan.scan_loop = NULL;
	}

	return GATTLIB_SUCCESS;
}

int gattlib_adapter_close(void* adapter)
{
	struct gattlib_adapter *gattlib_adapter = adapter;

	if (gattlib_adapter->device_manager)
		g_object_unref(gattlib_adapter->device_manager);
	g_object_unref(gattlib_adapter->adapter_proxy);
	free(gattlib_adapter->adapter_name);
	free(gattlib_adapter);

	return GATTLIB_SUCCESS;
}

gboolean stop_scan_func(gpointer data)
{
	g_main_loop_quit(data);
	return FALSE;
}
