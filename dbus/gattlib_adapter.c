/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

// This recursive mutex ensures all gattlib objects can be accessed in a multi-threaded environment
// The recursive mutex allows a same thread to lock twice the mutex without being blocked by itself.
GRecMutex m_gattlib_mutex;

// This structure is used for inter-thread communication
struct gattlib_signal m_gattlib_signal;


int gattlib_adapter_open(const char* adapter_name, gattlib_adapter_t** adapter) {
	char object_path[20];
	gattlib_adapter_t* gattlib_adapter;
	OrgBluezAdapter1 *adapter_proxy;
	GError *error = NULL;

	if (adapter == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	if (adapter_name == NULL) {
		adapter_name = GATTLIB_DEFAULT_ADAPTER;
	}

	snprintf(object_path, sizeof(object_path), "/org/bluez/%s", adapter_name);

	// Check if adapter has already be loaded
	g_rec_mutex_lock(&m_gattlib_mutex);
	*adapter = gattlib_adapter_from_id(object_path);
	if (*adapter != NULL) {
		GATTLIB_LOG(GATTLIB_DEBUG, "Bluetooth adapter %s has already been opened. Re-use it", adapter_name);
		gattlib_adapter_ref(*adapter);
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_SUCCESS;
	}
	g_rec_mutex_unlock(&m_gattlib_mutex);

	GATTLIB_LOG(GATTLIB_DEBUG, "Open bluetooth adapter %s", adapter_name);

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

	gattlib_adapter = calloc(1, sizeof(struct _gattlib_adapter));
	if (gattlib_adapter == NULL) {
		return GATTLIB_OUT_OF_MEMORY;
	}

	// Initialize stucture
	gattlib_adapter->id = strdup(object_path);
	gattlib_adapter->name = strdup(adapter_name);
	gattlib_adapter->reference_counter = 1;
	gattlib_adapter->backend.adapter_proxy = adapter_proxy;

	g_rec_mutex_lock(&m_gattlib_mutex);
	m_adapter_list = g_slist_append(m_adapter_list, gattlib_adapter);
	*adapter = gattlib_adapter;
	g_rec_mutex_unlock(&m_gattlib_mutex);

	return GATTLIB_SUCCESS;
}

const char *gattlib_adapter_get_name(gattlib_adapter_t* adapter) {
	//
	// Note: There is a risk we access the memory when it has been freed
	//       What we should do is to take 'm_gattlib_mutex', then to check the adapter is valid
	//       then to duplicate the string
	//
	return adapter->name;
}

gattlib_adapter_t* init_default_adapter(void) {
	gattlib_adapter_t* gattlib_adapter;
	int ret;

	ret = gattlib_adapter_open(NULL, &gattlib_adapter);
	if (ret != GATTLIB_SUCCESS) {
		return NULL;
	} else {
		return gattlib_adapter;
	}
}

GDBusObjectManager *get_device_manager_from_adapter(gattlib_adapter_t* gattlib_adapter, GError **error) {
	if (gattlib_adapter->backend.device_manager) {
		goto EXIT;
	}

	//
	// Get notification when objects are removed from the Bluez ObjectManager.
	// We should get notified when the connection is lost with the target to allow
	// us to advertise us again
	//
	gattlib_adapter->backend.device_manager = g_dbus_object_manager_client_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			"/",
			NULL, NULL, NULL, NULL,
			error);
	if (gattlib_adapter->backend.device_manager == NULL) {
		return NULL;
	}

EXIT:
	return gattlib_adapter->backend.device_manager;
}

static void device_manager_on_added_device1_signal(const char* device1_path, gattlib_adapter_t* gattlib_adapter)
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
		int ret;

		// Sometimes org_bluez_device1_get_address returns null addresses. If that's the case, early return.
		if (address == NULL) {
			g_object_unref(device1);
			return;
		}

		g_rec_mutex_lock(&m_gattlib_mutex);

		if (!gattlib_adapter_is_valid(gattlib_adapter)) {
			GATTLIB_LOG(GATTLIB_ERROR, "device_manager_on_added_device1_signal: Adapter not valid");
			g_rec_mutex_unlock(&m_gattlib_mutex);
			g_object_unref(device1);
			return;
		}

		//TODO: Add support for connected device with 'gboolean org_bluez_device1_get_connected (OrgBluezDevice1 *object);'
		//      When the device is connected, we potentially need to initialize some attributes
		ret = gattlib_device_set_state(gattlib_adapter, device1_path, DISCONNECTED);
		if (ret == GATTLIB_SUCCESS) {
			gattlib_on_discovered_device(gattlib_adapter, device1);
		}

		g_rec_mutex_unlock(&m_gattlib_mutex);
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
	device_manager_on_added_device1_signal(object_path, user_data);

	g_object_unref(interface);
}

static void on_dbus_object_removed(GDBusObjectManager *device_manager,
                     GDBusObject        *object,
                     gpointer            user_data)
{
	const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
	gattlib_adapter_t* gattlib_adapter = user_data;

	GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: on_object_removed: %s", object_path);

	// Mark the device has not present
	gattlib_device_set_state(gattlib_adapter, object_path, NOT_FOUND);
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
	gattlib_adapter_t* gattlib_adapter = user_data;

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

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(gattlib_adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "on_interface_proxy_properties_changed: Adapter not valid");
		goto EXIT;
	}

	if (gattlib_adapter->backend.device_manager == NULL) {
		goto EXIT;
	}

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
			goto EXIT;
		} else if (device1 == NULL) {
			GATTLIB_LOG(GATTLIB_ERROR, "Unexpected NULL device");
			goto EXIT;
		}

		// Check if the device has been disconnected
		GVariantDict dict;
		g_variant_dict_init(&dict, changed_properties);
		GVariant* has_rssi = g_variant_dict_lookup_value(&dict, "RSSI", NULL);
		GVariant* has_manufacturer_data = g_variant_dict_lookup_value(&dict, "ManufacturerData", NULL);

		enum _gattlib_device_state old_device_state = gattlib_device_get_state(gattlib_adapter, proxy_object_path);

		if (old_device_state == NOT_FOUND) {
			if (has_rssi || has_manufacturer_data) {
				int ret = gattlib_device_set_state(gattlib_adapter, proxy_object_path, DISCONNECTED);
				if (ret == GATTLIB_SUCCESS) {
					gattlib_on_discovered_device(gattlib_adapter, device1);
				}
			}
		}

		g_variant_dict_end(&dict);
		g_object_unref(device1);
	}

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
}

/**
 * Function that waits for the end of the BLE scan
 *
 * It either called when we wait for BLE scan to complete or when we close the BLE adapter
 */
static void _wait_scan_loop_stop_scanning(gattlib_adapter_t* gattlib_adapter) {
	g_mutex_lock(&m_gattlib_signal.mutex);
	while (gattlib_adapter_is_scanning(gattlib_adapter)) {
		g_cond_wait(&m_gattlib_signal.condition, &m_gattlib_signal.mutex);
	}
	g_mutex_unlock(&m_gattlib_signal.mutex);
}

/**
 * Function called when the BLE scan duration has timeout
 */
static gboolean _stop_scan_on_timeout(gpointer data) {
	gattlib_adapter_t* gattlib_adapter = data;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(gattlib_adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "_stop_scan_on_timeout: Adapter not valid");
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return FALSE;
	}

	if (gattlib_adapter->backend.ble_scan.is_scanning) {
		g_mutex_lock(&m_gattlib_signal.mutex);
		gattlib_adapter->backend.ble_scan.is_scanning = false;
		m_gattlib_signal.signals |= GATTLIB_SIGNAL_ADAPTER_STOP_SCANNING;
		g_cond_broadcast(&m_gattlib_signal.condition);
		g_mutex_unlock(&m_gattlib_signal.mutex);
	}

	// Unset timeout ID to not try removing it
	gattlib_adapter->backend.ble_scan.ble_scan_timeout_id = 0;

	g_rec_mutex_unlock(&m_gattlib_mutex);

	GATTLIB_LOG(GATTLIB_DEBUG, "BLE scan is stopped after scanning time has expired.");
	return FALSE;
}

/**
 * Thread that waits for the end of BLE scan that is triggered either by a timeout of the BLE scan
 * or disabling the BLE scan
 */
static void* _ble_scan_loop_thread(void* args) {
	gattlib_adapter_t* gattlib_adapter = args;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(gattlib_adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "_ble_scan_loop_thread: Adapter not valid (1)");
		goto EXIT;
	}

	if (gattlib_adapter->backend.ble_scan.ble_scan_timeout_id > 0) {
		GATTLIB_LOG(GATTLIB_WARNING, "A BLE scan seems to already be in progress.");
	}

	gattlib_adapter->backend.ble_scan.is_scanning = true;

	if (gattlib_adapter->backend.ble_scan.ble_scan_timeout > 0) {
		GATTLIB_LOG(GATTLIB_DEBUG, "Scan for BLE devices for %ld seconds", gattlib_adapter->backend.ble_scan.ble_scan_timeout);

		gattlib_adapter->backend.ble_scan.ble_scan_timeout_id = g_timeout_add_seconds(gattlib_adapter->backend.ble_scan.ble_scan_timeout,
			_stop_scan_on_timeout, gattlib_adapter);
	}

	g_rec_mutex_unlock(&m_gattlib_mutex);

	// Wait for the BLE scan to be explicitely stopped by 'gattlib_adapter_scan_disable()' or timeout.
	_wait_scan_loop_stop_scanning(gattlib_adapter);

	// Note: The function only resumes when loop timeout as expired or g_main_loop_quit has been called.

	g_rec_mutex_lock(&m_gattlib_mutex);

	// Confirm gattlib_adapter is still valid
	if (!gattlib_adapter_is_valid(gattlib_adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "_ble_scan_loop_thread: Adapter not valid (2)");
		goto EXIT;
	}

	g_signal_handler_disconnect(G_DBUS_OBJECT_MANAGER(gattlib_adapter->backend.device_manager), gattlib_adapter->backend.ble_scan.added_signal_id);
	g_signal_handler_disconnect(G_DBUS_OBJECT_MANAGER(gattlib_adapter->backend.device_manager), gattlib_adapter->backend.ble_scan.removed_signal_id);
	g_signal_handler_disconnect(G_DBUS_OBJECT_MANAGER(gattlib_adapter->backend.device_manager), gattlib_adapter->backend.ble_scan.changed_signal_id);

	// Ensure BLE device discovery is stopped
	gattlib_adapter_scan_disable(gattlib_adapter);

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return NULL;
}

static int _gattlib_adapter_scan_enable_with_filter(gattlib_adapter_t* adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
	gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	GDBusObjectManager *device_manager;
	GError *error = NULL;
	GVariantBuilder arg_properties_builder;
	GVariant *rssi_variant = NULL;
	int ret;

	if ((adapter == NULL) || (adapter->backend.adapter_proxy == NULL)) {
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

	org_bluez_adapter1_call_set_discovery_filter_sync(adapter->backend.adapter_proxy,
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
	device_manager = get_device_manager_from_adapter(adapter, &error);
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
	memset(&adapter->backend.ble_scan, 0, sizeof(adapter->backend.ble_scan));
	adapter->backend.ble_scan.enabled_filters = enabled_filters;
	adapter->backend.ble_scan.ble_scan_timeout = timeout;
	adapter->discovered_device_callback.callback.discovered_device = discovered_device_cb;
	adapter->discovered_device_callback.user_data = user_data;

	adapter->backend.ble_scan.added_signal_id = g_signal_connect(G_DBUS_OBJECT_MANAGER(device_manager),
	                    "object-added",
	                    G_CALLBACK(on_dbus_object_added),
	                    adapter);

	adapter->backend.ble_scan.removed_signal_id = g_signal_connect(G_DBUS_OBJECT_MANAGER(device_manager),
	                    "object-removed",
	                    G_CALLBACK(on_dbus_object_removed),
	                    adapter);

	// List for object changes to see if there are still devices around
	adapter->backend.ble_scan.changed_signal_id = g_signal_connect(G_DBUS_OBJECT_MANAGER(device_manager),
					     "interface-proxy-properties-changed",
					     G_CALLBACK(on_interface_proxy_properties_changed),
					     adapter);

	// Now, start BLE discovery
	org_bluez_adapter1_call_start_discovery_sync(adapter->backend.adapter_proxy, NULL, &error);
	if (error) {
		ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to start discovery: %s", error->message);
		g_error_free(error);
		return ret;
	}

	GATTLIB_LOG(GATTLIB_DEBUG, "Bluetooth scan started");
	return GATTLIB_SUCCESS;
}

int gattlib_adapter_scan_enable_with_filter(gattlib_adapter_t* adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
		gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_adapter_scan_enable_with_filter: Adapter not valid (1)");
		ret = GATTLIB_ADAPTER_CLOSE;
		goto EXIT;
	}

	ret = _gattlib_adapter_scan_enable_with_filter(adapter, uuid_list, rssi_threshold, enabled_filters,
		discovered_device_cb, timeout, user_data);
	if (ret != GATTLIB_SUCCESS) {
		goto EXIT;
	}

	adapter->backend.ble_scan.is_scanning = true;

	adapter->backend.ble_scan.scan_loop_thread = g_thread_try_new("gattlib_ble_scan", _ble_scan_loop_thread, adapter, &error);
	if (adapter->backend.ble_scan.scan_loop_thread == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create BLE scan thread: %s", error->message);
		g_error_free(error);
		ret = GATTLIB_ERROR_INTERNAL;
		goto EXIT;
	}

	// We need to release the mutex to ensure we leave the other thread to signal us
	g_rec_mutex_unlock(&m_gattlib_mutex);

	g_mutex_lock(&m_gattlib_signal.mutex);
	while (gattlib_adapter_is_scanning(adapter)) {
		g_cond_wait(&m_gattlib_signal.condition, &m_gattlib_signal.mutex);
	}
	g_mutex_unlock(&m_gattlib_signal.mutex);

	// Get the mutex again
	g_rec_mutex_lock(&m_gattlib_mutex);

	// Ensure the adapter is still valid when we get the mutex again
	if (!gattlib_adapter_is_valid(adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_adapter_scan_enable_with_filter: Adapter not valid (2)");
		ret = GATTLIB_ADAPTER_CLOSE;
		goto EXIT;
	}

	// Free thread
	g_thread_unref(adapter->backend.ble_scan.scan_loop_thread);
	adapter->backend.ble_scan.scan_loop_thread = NULL;

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}

int gattlib_adapter_scan_enable_with_filter_non_blocking(gattlib_adapter_t* adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
		gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_adapter_scan_enable_with_filter_non_blocking: Adapter not valid (2)");
		ret = GATTLIB_ADAPTER_CLOSE;
		goto EXIT;
	}

	ret = _gattlib_adapter_scan_enable_with_filter(adapter, uuid_list, rssi_threshold, enabled_filters,
		discovered_device_cb, timeout, user_data);
	if (ret != GATTLIB_SUCCESS) {
		goto EXIT;
	}

	adapter->backend.ble_scan.scan_loop_thread = g_thread_try_new("gattlib_ble_scan", _ble_scan_loop_thread, adapter, &error);
	if (adapter->backend.ble_scan.scan_loop_thread == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to create BLE scan thread: %s", error->message);
		g_error_free(error);
		ret = GATTLIB_ERROR_INTERNAL;
		goto EXIT;
	}

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}

int gattlib_adapter_scan_enable(gattlib_adapter_t* adapter, gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
{
	return gattlib_adapter_scan_enable_with_filter(adapter,
			NULL, 0 /* RSSI Threshold */,
			GATTLIB_DISCOVER_FILTER_USE_NONE,
			discovered_device_cb, timeout, user_data);
}

int gattlib_adapter_scan_disable(gattlib_adapter_t* adapter) {
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_adapter_scan_disable: Adapter not valid");
		ret = GATTLIB_ADAPTER_CLOSE;
		goto EXIT;
	}

	if (adapter->backend.adapter_proxy == NULL) {
		GATTLIB_LOG(GATTLIB_INFO, "Could not disable BLE scan. No BLE adapter setup.");
		ret = GATTLIB_NO_ADAPTER;
		goto EXIT;
	}

	if (!org_bluez_adapter1_get_discovering(adapter->backend.adapter_proxy)) {
		GATTLIB_LOG(GATTLIB_DEBUG, "No discovery in progress. We skip discovery stopping (1).");
		goto EXIT;
	} else if (!adapter->backend.ble_scan.is_scanning) {
		GATTLIB_LOG(GATTLIB_DEBUG, "No discovery in progress. We skip discovery stopping (2).");
		goto EXIT;
	}

	GATTLIB_LOG(GATTLIB_DEBUG, "Stop bluetooth scan.");

	org_bluez_adapter1_call_stop_discovery_sync(adapter->backend.adapter_proxy, NULL, &error);
	if (error != NULL) {
		if (((error->domain == 238) || (error->domain == 239)) && (error->code == 36)) {
			GATTLIB_LOG(GATTLIB_WARNING, "No bluetooth scan has been started.");
			// Correspond to error: GDBus.Error:org.bluez.Error.Failed: No discovery started
			goto EXIT;
		} else {
			GATTLIB_LOG(GATTLIB_WARNING, "Error while stopping BLE discovery: %s (%d,%d)", error->message, error->domain, error->code);
		}
	}

	// Free and reset callback to stop calling it after we stopped
	gattlib_handler_free(&adapter->discovered_device_callback);

	// Stop BLE scan loop thread
	if (adapter->backend.ble_scan.is_scanning) {
		adapter->backend.ble_scan.is_scanning = false;
		g_mutex_lock(&m_gattlib_signal.mutex);
		m_gattlib_signal.signals |= GATTLIB_SIGNAL_ADAPTER_STOP_SCANNING;
		g_cond_broadcast(&m_gattlib_signal.condition);
		g_mutex_unlock(&m_gattlib_signal.mutex);
	}

	// Remove timeout
	if (adapter->backend.ble_scan.ble_scan_timeout_id) {
		g_source_remove(adapter->backend.ble_scan.ble_scan_timeout_id);
		adapter->backend.ble_scan.ble_scan_timeout_id = 0;
	}

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}

int gattlib_adapter_close(gattlib_adapter_t* adapter) {
	bool are_devices_disconnected;
	int ret = GATTLIB_SUCCESS;

    g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_adapter_close: Adapter not valid");
		ret = GATTLIB_ADAPTER_CLOSE;
		goto EXIT;
	}

	are_devices_disconnected = gattlib_devices_are_disconnected(adapter);
	if (!are_devices_disconnected) {
		GATTLIB_LOG(GATTLIB_ERROR, "Adapter cannot be closed as some devices are not disconnected");
		ret = GATTLIB_BUSY;
		goto EXIT;
	}

	GSList *adapter_entry = g_slist_find(m_adapter_list, adapter);
	if (adapter_entry == NULL) {
		GATTLIB_LOG(GATTLIB_WARNING, "Adapter has already been closed");
		goto EXIT;
	}

	GATTLIB_LOG(GATTLIB_DEBUG, "Close bluetooth adapter %s", adapter->name);

	if (adapter->backend.ble_scan.is_scanning) {
		GATTLIB_LOG(GATTLIB_DEBUG, "Bluetooth adapter %s was scanning. We stop the scan", adapter->name);
		gattlib_adapter_scan_disable(adapter);

		// We must release gattlib mutex to not block the library
		// We must also increase reference counter to not wait for a thread that has been freed
		GThread *scan_loop_thread = adapter->backend.ble_scan.scan_loop_thread;
		g_thread_ref(scan_loop_thread);
		g_rec_mutex_unlock(&m_gattlib_mutex);

		_wait_scan_loop_stop_scanning(adapter);

		g_thread_join(adapter->backend.ble_scan.scan_loop_thread);
		// At this stage scan_loop_thread should have completed
		g_rec_mutex_lock(&m_gattlib_mutex);
		g_thread_unref(scan_loop_thread);
	}

	// Unref/Free the adapter
	gattlib_adapter_unref(adapter);

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}

int gattlib_adapter_ref(gattlib_adapter_t* adapter) {
	g_rec_mutex_lock(&m_gattlib_mutex);
	adapter->reference_counter++;
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return GATTLIB_SUCCESS;
}

int gattlib_adapter_unref(gattlib_adapter_t* adapter) {
	int ret = GATTLIB_SUCCESS;

	g_rec_mutex_lock(&m_gattlib_mutex);
	adapter->reference_counter--;

	if (adapter->reference_counter > 0) {
		goto EXIT;
	}

	// Ensure the thread is freed on adapter closing
	if (adapter->backend.ble_scan.scan_loop_thread) {
		g_thread_unref(adapter->backend.ble_scan.scan_loop_thread);
		adapter->backend.ble_scan.scan_loop_thread = NULL;
	}

	if (adapter->backend.device_manager) {
		g_object_unref(adapter->backend.device_manager);
		adapter->backend.device_manager = NULL;
	}

	if (adapter->backend.adapter_proxy != NULL) {
		g_object_unref(adapter->backend.adapter_proxy);
		adapter->backend.adapter_proxy = NULL;
	}

	if (adapter->id != NULL) {
		free(adapter->id);
		adapter->id = NULL;
	}

	if (adapter->name != NULL) {
		free(adapter->name);
		adapter->name = NULL;
	}

	gattlib_devices_free(adapter);

	// Remove adapter from the global list
	m_adapter_list = g_slist_remove(m_adapter_list, adapter);

	free(adapter);

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}
