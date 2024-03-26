/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

// Keep track of the allocated adapters to avoid an adapter to be freed twice.
// It could happen when using Python wrapper.
static GSList *m_adapter_list;
static GMutex m_adapter_list_mutex;


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

	GATTLIB_LOG(GATTLIB_DEBUG, "Open bluetooth adapter %s", adapter_name);

	snprintf(object_path, sizeof(object_path), "/org/bluez/%s", adapter_name);

	adapter_proxy = org_bluez_adapter1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL, &error);
	if (adapter_proxy == NULL) {
		int ret = GATTLIB_ERROR_DBUS;
		if (error) {
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to get adapter %s: %s", object_path, error->message);
			ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
			g_error_free(error);
		} else {
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to get adapter %s", object_path);
		}
		return ret;
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

	g_mutex_lock(&m_adapter_list_mutex);
	m_adapter_list = g_slist_append(m_adapter_list, gattlib_adapter);
	g_mutex_unlock(&m_adapter_list_mutex);

	*adapter = gattlib_adapter;
	return GATTLIB_SUCCESS;
}

const char *gattlib_adapter_get_name(void* adapter) {
	struct gattlib_adapter *gattlib_adapter = adapter;
	return gattlib_adapter->adapter_name;
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

GDBusObjectManager *get_device_manager_from_adapter(struct gattlib_adapter *gattlib_adapter, GError **error) {
	g_mutex_lock(&m_adapter_list_mutex);

	if (gattlib_adapter->device_manager) {
		goto EXIT;
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
			error);
	if (gattlib_adapter->device_manager == NULL) {
		g_mutex_unlock(&m_adapter_list_mutex);
		return NULL;
	}

EXIT:
	g_mutex_unlock(&m_adapter_list_mutex);
	return gattlib_adapter->device_manager;
}

static void device_manager_on_device1_signal(const char* device1_path, struct gattlib_adapter* gattlib_adapter)
{
	GError *error = NULL;
	OrgBluezDevice1* device1 = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
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
		g_mutex_lock(&gattlib_adapter->ble_scan.discovered_devices_mutex);
		GSList *item = g_slist_find_custom(gattlib_adapter->ble_scan.discovered_devices, address, (GCompareFunc)g_ascii_strcasecmp);
		// First time this device is in the list
		if (item == NULL) {
			// Add the device to the list
			gattlib_adapter->ble_scan.discovered_devices = g_slist_append(gattlib_adapter->ble_scan.discovered_devices, g_strdup(address));
		}
		g_mutex_unlock(&gattlib_adapter->ble_scan.discovered_devices_mutex);

		if ((item == NULL) || (gattlib_adapter->ble_scan.enabled_filters & GATTLIB_DISCOVER_FILTER_NOTIFY_CHANGE)) {
			gattlib_on_discovered_device(gattlib_adapter, device1);
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
	const char* proxy_object_path = g_dbus_proxy_get_object_path(interface_proxy);
	struct gattlib_adapter* gattlib_adapter = user_data;

	if (gattlib_adapter->device_manager == NULL) {
		return;
	}

	// Count number of invalidated properties
	size_t invalidated_properties_count = 0;
	if (invalidated_properties != NULL) {
		const gchar *const *invalidated_properties_ptr = invalidated_properties;
		while (*invalidated_properties_ptr != NULL) {
			invalidated_properties_count++;
			invalidated_properties_ptr++;
		}
	}

	GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: on_interface_proxy_properties_changed(%s): interface:%s changed_properties:%s invalidated_properties:%d",
			proxy_object_path,
			g_dbus_proxy_get_interface_name(interface_proxy),
			g_variant_print(changed_properties, TRUE),
			invalidated_properties_count);

	// Check if the object is a 'org.bluez.Device1'
	if (strcmp(g_dbus_proxy_get_interface_name(interface_proxy), "org.bluez.Device1") == 0) {
		// It is a 'org.bluez.Device1'
		GError *error = NULL;

		OrgBluezDevice1* device1 = org_bluez_device1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_PROXY_FLAGS_NONE,
				"org.bluez",
				proxy_object_path, NULL, &error);
		if (error) {
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to connection to new DBus Bluez Device: %s", error->message);
			g_error_free(error);
			return;
		} else if (device1 == NULL) {
			GATTLIB_LOG(GATTLIB_ERROR, "Unexpected NULL device");
			return;
		}

		const char* device_mac_address = org_bluez_device1_get_address(device1);

		// Check if the device has been disconnected
		GVariantDict dict;
		g_variant_dict_init(&dict, changed_properties);
		GVariant* connected = g_variant_dict_lookup_value(&dict, "Connected", NULL);
		GVariant* rssi = g_variant_dict_lookup_value(&dict, "RSSI", NULL);

		g_mutex_lock(&gattlib_adapter->ble_scan.discovered_devices_mutex);

		// Check if the device is already part of the list
		GSList *found_device = g_slist_find_custom(gattlib_adapter->ble_scan.discovered_devices, device_mac_address, (GCompareFunc)g_ascii_strcasecmp);

		if (connected && !g_variant_get_boolean(connected)) {
			// The device has been disconnected. We will remove it from the list of discovered device.
			// In case the device has been found again, it will be seen as a new device

			GATTLIB_LOG(GATTLIB_INFO, "Device %s has been disconnected", device_mac_address);

			if (found_device) {
				gattlib_adapter->ble_scan.discovered_devices = g_slist_remove(gattlib_adapter->ble_scan.discovered_devices, found_device);
			}
		} else if (rssi) {
			// First time this device is in the list
			if (found_device == NULL) {
				// Add the device to the list
				gattlib_adapter->ble_scan.discovered_devices = g_slist_append(gattlib_adapter->ble_scan.discovered_devices, g_strdup(device_mac_address));
				gattlib_on_discovered_device(gattlib_adapter, device1);
			}
		}
		g_mutex_unlock(&gattlib_adapter->ble_scan.discovered_devices_mutex);

		g_variant_dict_end(&dict);

		g_object_unref(device1);
	}
}

static void _stop_scan_loop_thread(struct gattlib_adapter *gattlib_adapter) {
	if (gattlib_adapter->ble_scan.is_scanning) {
		g_mutex_lock(&gattlib_adapter->ble_scan.scan_loop_mutex);
		gattlib_adapter->ble_scan.is_scanning = false;
		g_cond_broadcast(&gattlib_adapter->ble_scan.scan_loop_cond);
		g_mutex_unlock(&gattlib_adapter->ble_scan.scan_loop_mutex);
	}
}

static void _wait_scan_loop_stop_scanning(struct gattlib_adapter *gattlib_adapter) {
	g_mutex_lock(&gattlib_adapter->ble_scan.scan_loop_mutex);
	while (gattlib_adapter->ble_scan.is_scanning) {
		g_cond_wait(&gattlib_adapter->ble_scan.scan_loop_cond, &gattlib_adapter->ble_scan.scan_loop_mutex);
	}
	g_mutex_unlock(&gattlib_adapter->ble_scan.scan_loop_mutex);
}

/**
 * Function called when the BLE scan duration has timeout
 */
static gboolean _stop_scan_func(gpointer data) {
	struct gattlib_adapter *gattlib_adapter = data;

	_stop_scan_loop_thread(gattlib_adapter);

	// Unset timeout ID to not try removing it
	gattlib_adapter->ble_scan.ble_scan_timeout_id = 0;

	GATTLIB_LOG(GATTLIB_DEBUG, "BLE scan is stopped after scanning time has expired.");
	return FALSE;
}

static void* _ble_scan_loop(void* args) {
	struct gattlib_adapter *gattlib_adapter = args;

	if (gattlib_adapter->ble_scan.ble_scan_timeout_id > 0) {
		GATTLIB_LOG(GATTLIB_WARNING, "A BLE scan seems to already be in progress.");
	}

	gattlib_adapter->ble_scan.is_scanning = true;

	if (gattlib_adapter->ble_scan.ble_scan_timeout > 0) {
		GATTLIB_LOG(GATTLIB_DEBUG, "Scan for BLE devices for %ld seconds", gattlib_adapter->ble_scan.ble_scan_timeout);

		gattlib_adapter->ble_scan.ble_scan_timeout_id = g_timeout_add_seconds(gattlib_adapter->ble_scan.ble_scan_timeout,
			_stop_scan_func, gattlib_adapter);
	}

	// Wait for the BLE scan to be explicitely stopped by 'gattlib_adapter_scan_disable()' or timeout.
	_wait_scan_loop_stop_scanning(gattlib_adapter);

	// Note: The function only resumes when loop timeout as expired or g_main_loop_quit has been called.

	g_signal_handler_disconnect(G_DBUS_OBJECT_MANAGER(gattlib_adapter->device_manager), gattlib_adapter->ble_scan.added_signal_id);
	g_signal_handler_disconnect(G_DBUS_OBJECT_MANAGER(gattlib_adapter->device_manager), gattlib_adapter->ble_scan.changed_signal_id);

	// Ensure BLE device discovery is stopped
	gattlib_adapter_scan_disable(gattlib_adapter);

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
	int ret;

	if ((gattlib_adapter == NULL) || (gattlib_adapter->adapter_proxy == NULL)) {
		GATTLIB_LOG(GATTLIB_ERROR, "Could not start BLE scan. No opened bluetooth adapter");
		return GATTLIB_NO_ADAPTER;
	}

	g_variant_builder_init(&arg_properties_builder, G_VARIANT_TYPE("a{sv}"));

	if (enabled_filters & GATTLIB_DISCOVER_FILTER_USE_UUID) {
		char uuid_str[MAX_LEN_UUID_STR + 1];
		GVariantBuilder list_uuid_builder;

		if (uuid_list == NULL) {
			GATTLIB_LOG(GATTLIB_ERROR, "Could not start BLE scan. Missing list of UUIDs");
			return GATTLIB_INVALID_PARAMETER;
		}

		GATTLIB_LOG(GATTLIB_DEBUG, "Configure bluetooth scan with UUID");

		g_variant_builder_init(&list_uuid_builder, G_VARIANT_TYPE ("as"));

		for (uuid_t **uuid_ptr = uuid_list; *uuid_ptr != NULL; uuid_ptr++) {
			gattlib_uuid_to_string(*uuid_ptr, uuid_str, sizeof(uuid_str));
			g_variant_builder_add(&list_uuid_builder, "s", uuid_str);
		}

		g_variant_builder_add(&arg_properties_builder, "{sv}", "UUIDs", g_variant_builder_end(&list_uuid_builder));
	}

	if (enabled_filters & GATTLIB_DISCOVER_FILTER_USE_RSSI) {
		GATTLIB_LOG(GATTLIB_DEBUG, "Configure bluetooth scan with RSSI");
		GVariant *rssi_variant = g_variant_new_int16(rssi_threshold);
		g_variant_builder_add(&arg_properties_builder, "{sv}", "RSSI", rssi_variant);
	}

	org_bluez_adapter1_call_set_discovery_filter_sync(gattlib_adapter->adapter_proxy,
			g_variant_builder_end(&arg_properties_builder), NULL, &error);

	if (rssi_variant) {
		g_variant_unref(rssi_variant);
	}

	if (error) {
		ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to set discovery filter: %s (%d.%d)",
				error->message, error->domain, error->code);
		g_error_free(error);
		return ret;
	}

	//
	// Get notification when objects are removed from the Bluez ObjectManager.
	// We should get notified when the connection is lost with the target to allow
	// us to advertise us again
	//
	device_manager = get_device_manager_from_adapter(gattlib_adapter, &error);
	if (device_manager == NULL) {
		if (error != NULL) {
			ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
			g_error_free(error);
		} else {
			ret = GATTLIB_ERROR_DBUS;
		}
		return ret;
	}

	// Clear BLE scan structure
	memset(&gattlib_adapter->ble_scan, 0, sizeof(gattlib_adapter->ble_scan));
	gattlib_adapter->ble_scan.enabled_filters = enabled_filters;
	gattlib_adapter->ble_scan.ble_scan_timeout = timeout;
	gattlib_adapter->ble_scan.discovered_device_callback.callback.discovered_device = discovered_device_cb;
	gattlib_adapter->ble_scan.discovered_device_callback.user_data = user_data;

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
		ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to start discovery: %s", error->message);
		g_error_free(error);
		return ret;
	}

	GATTLIB_LOG(GATTLIB_DEBUG, "Bluetooth scan started");
	return GATTLIB_SUCCESS;
}

int gattlib_adapter_scan_enable_with_filter(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
		gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	struct gattlib_adapter *gattlib_adapter = adapter;
	GError *error = NULL;
	int ret;

	ret = _gattlib_adapter_scan_enable_with_filter(adapter, uuid_list, rssi_threshold, enabled_filters,
		discovered_device_cb, timeout, user_data);
	if (ret != GATTLIB_SUCCESS) {
		return ret;
	}

	gattlib_adapter->ble_scan.is_scanning = true;

	gattlib_adapter->ble_scan.scan_loop_thread = g_thread_try_new("gattlib_ble_scan", _ble_scan_loop, gattlib_adapter, &error);
	if (gattlib_adapter->ble_scan.scan_loop_thread == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create BLE scan thread: %s", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_INTERNAL;
	}

	g_mutex_lock(&gattlib_adapter->ble_scan.scan_loop_mutex);
	while (gattlib_adapter->ble_scan.is_scanning) {
		g_cond_wait(&gattlib_adapter->ble_scan.scan_loop_cond, &gattlib_adapter->ble_scan.scan_loop_mutex);
	}

	// Free thread
	g_thread_unref(gattlib_adapter->ble_scan.scan_loop_thread);
	gattlib_adapter->ble_scan.scan_loop_thread = NULL;
	g_mutex_unlock(&gattlib_adapter->ble_scan.scan_loop_mutex);

	return 0;
}

int gattlib_adapter_scan_enable_with_filter_non_blocking(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
		gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	struct gattlib_adapter *gattlib_adapter = adapter;
	GError *error = NULL;
	int ret;

	ret = _gattlib_adapter_scan_enable_with_filter(adapter, uuid_list, rssi_threshold, enabled_filters,
		discovered_device_cb, timeout, user_data);
	if (ret != GATTLIB_SUCCESS) {
		return ret;
	}

	gattlib_adapter->ble_scan.scan_loop_thread = g_thread_try_new("gattlib_ble_scan", _ble_scan_loop, gattlib_adapter, &error);
	if (gattlib_adapter->ble_scan.scan_loop_thread == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create BLE scan thread: %s", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_INTERNAL;
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
	GError *error = NULL;

	if (gattlib_adapter->adapter_proxy == NULL) {
		GATTLIB_LOG(GATTLIB_INFO, "Could not disable BLE scan. No BLE adapter setup.");
		return GATTLIB_NO_ADAPTER;
	}

	g_mutex_lock(&gattlib_adapter->ble_scan.scan_loop_mutex);

	if (!org_bluez_adapter1_get_discovering(gattlib_adapter->adapter_proxy)) {
		GATTLIB_LOG(GATTLIB_DEBUG, "No discovery in progress. We skip discovery stopping.");
		goto EXIT;
	}

	GATTLIB_LOG(GATTLIB_DEBUG, "Stop bluetooth scan.");

	org_bluez_adapter1_call_stop_discovery_sync(gattlib_adapter->adapter_proxy, NULL, &error);
	if (error != NULL) {
		if (((error->domain == 238) || (error->domain == 239)) && (error->code == 36)) {
			// Correspond to error: GDBus.Error:org.bluez.Error.Failed: No discovery started
			goto EXIT;
		} else {
			GATTLIB_LOG(GATTLIB_WARNING, "Error while stopping BLE discovery: %s (%d,%d)", error->message, error->domain, error->code);
		}
	}

	// Free and reset callback to stop calling it after we stopped
	gattlib_handler_free(&gattlib_adapter->ble_scan.discovered_device_callback);

	// Stop BLE scan loop thread
	if (gattlib_adapter->ble_scan.is_scanning) {
		gattlib_adapter->ble_scan.is_scanning = false;
		g_cond_broadcast(&gattlib_adapter->ble_scan.scan_loop_cond);
	}

	// Remove timeout
	if (gattlib_adapter->ble_scan.ble_scan_timeout_id) {
		g_source_remove(gattlib_adapter->ble_scan.ble_scan_timeout_id);
		gattlib_adapter->ble_scan.ble_scan_timeout_id = 0;
	}

	// Free discovered device list
	g_mutex_lock(&gattlib_adapter->ble_scan.discovered_devices_mutex);
	g_slist_foreach(gattlib_adapter->ble_scan.discovered_devices, (GFunc)g_free, NULL);
	g_slist_free(gattlib_adapter->ble_scan.discovered_devices);
	gattlib_adapter->ble_scan.discovered_devices = NULL;
	g_mutex_unlock(&gattlib_adapter->ble_scan.discovered_devices_mutex);

EXIT:
	g_mutex_unlock(&gattlib_adapter->ble_scan.scan_loop_mutex);
	return GATTLIB_SUCCESS;
}

int gattlib_adapter_close(void* adapter)
{
	struct gattlib_adapter *gattlib_adapter = adapter;

	g_mutex_lock(&m_adapter_list_mutex);
	GSList *adapter_entry = g_slist_find(m_adapter_list, adapter);
	if (adapter_entry == NULL) {
		GATTLIB_LOG(GATTLIB_WARNING, "Adapter has already been closed");
		goto EXIT;
	}

	GATTLIB_LOG(GATTLIB_DEBUG, "Close bluetooth adapter %s", gattlib_adapter->adapter_name);

	if (gattlib_adapter->ble_scan.is_scanning) {
		gattlib_adapter_scan_disable(gattlib_adapter);

		_wait_scan_loop_stop_scanning(gattlib_adapter);
		g_thread_join(gattlib_adapter->ble_scan.scan_loop_thread);
	}

	// Ensure the thread is freed on adapter closing
	if (gattlib_adapter->ble_scan.scan_loop_thread) {
		g_thread_unref(gattlib_adapter->ble_scan.scan_loop_thread);
		gattlib_adapter->ble_scan.scan_loop_thread = NULL;
	}

	if (gattlib_adapter->device_manager) {
		g_object_unref(gattlib_adapter->device_manager);
		gattlib_adapter->device_manager = NULL;
	}

	if (gattlib_adapter->adapter_proxy != NULL) {
		g_object_unref(gattlib_adapter->adapter_proxy);
		gattlib_adapter->adapter_proxy = NULL;
	}

	if (gattlib_adapter->adapter_name != NULL) {
		free(gattlib_adapter->adapter_name);
		gattlib_adapter->adapter_name = NULL;
	}

	free(gattlib_adapter);

	// Remove adapter from the global list
	m_adapter_list = g_slist_remove(m_adapter_list, gattlib_adapter);

	gattlib_adapter = NULL;

EXIT:
	g_mutex_unlock(&m_adapter_list_mutex);
	return GATTLIB_SUCCESS;
}
