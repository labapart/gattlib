/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
 */

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "gattlib_internal.h"

#define CONNECT_TIMEOUT_SEC  10

static const char *m_dbus_error_unknown_object = "GDBus.Error:org.freedesktop.DBus.Error.UnknownObject";

static void _on_device_connect(gattlib_connection_t* connection) {
	GDBusObjectManager *device_manager;
	GError *error = NULL;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_valid(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "_on_device_connect: Device not valid");
		goto EXIT;
	}

	// Stop the timeout for connection
	if (connection->backend.connection_timeout_id) {
		g_source_remove(connection->backend.connection_timeout_id);
		connection->backend.connection_timeout_id = 0;
	}

	// Get list of objects belonging to Device Manager
	device_manager = get_device_manager_from_adapter(connection->device->adapter, &error);
	if (device_manager == NULL) {
		if (error != NULL) {
			GATTLIB_LOG(GATTLIB_ERROR, "gattlib_connect: Failed to get device manager from adapter (%d, %d).", error->domain, error->code);
			g_error_free(error);
		} else {
			GATTLIB_LOG(GATTLIB_ERROR, "gattlib_connect: Failed to get device manager from adapter");
		}
		//TODO: Free device
		goto EXIT;
	}
	connection->backend.dbus_objects = g_dbus_object_manager_get_objects(device_manager);

	gattlib_device_set_state(connection->device->adapter, connection->device->device_id, CONNECTED);

	gattlib_on_connected_device(connection);

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
}

gboolean on_handle_device_property_change(
	    GDBusProxy *proxy,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	gattlib_connection_t* connection = user_data;

	// Retrieve 'Value' from 'arg_changed_properties'
	if (g_variant_n_children (arg_changed_properties) > 0) {
		const gchar* device_object_path = g_dbus_proxy_get_object_path(proxy);
		GVariantIter *iter;
		const gchar *key;
		GVariant *value;

		g_variant_get (arg_changed_properties, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
			if (strcmp(key, "Connected") == 0) {
				if (!g_variant_get_boolean(value)) {
					GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: device_property_change(%s): Disconnection", device_object_path);
					gattlib_on_disconnected_device(connection);
				} else {
					GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: device_property_change(%s): Connection", device_object_path);
				}
			} else if (strcmp(key, "ServicesResolved") == 0) {
				if (g_variant_get_boolean(value)) {
					GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: device_property_change(%s): Service Resolved", device_object_path);
					_on_device_connect(connection);
				}
			}
		}
		g_variant_iter_free(iter);
	}
	return TRUE;
}

void get_device_path_from_mac_with_adapter(OrgBluezAdapter1* adapter, const char *mac_address, char *object_path, size_t object_path_len)
{
	char device_address_str[20 + 1];
	const char* adapter_path = g_dbus_proxy_get_object_path((GDBusProxy *)ORG_BLUEZ_ADAPTER1_PROXY(adapter));

	// Transform string from 'DA:94:40:95:E0:87' to 'dev_DA_94_40_95_E0_87'
	strncpy(device_address_str, mac_address, sizeof(device_address_str) - 1);
	for (size_t i = 0; i < strlen(device_address_str); i++) {
		if (device_address_str[i] == ':') {
			device_address_str[i] = '_';
		}
	}

	// Force a null-terminated character
	device_address_str[20] = '\0';

	// Generate object path like: /org/bluez/hci0/dev_DA_94_40_95_E0_87
	snprintf(object_path, object_path_len, "%s/dev_%s", adapter_path, device_address_str);
}


void get_device_path_from_mac(const char *adapter_name, const char *mac_address, char *object_path, size_t object_path_len)
{
	char device_address_str[20 + 1];
	const char* adapter;

	if (adapter_name) {
		adapter = adapter_name;
	} else {
		adapter = "hci0";
	}

	// Transform string from 'DA:94:40:95:E0:87' to 'dev_DA_94_40_95_E0_87'
	strncpy(device_address_str, mac_address, sizeof(device_address_str) - 1);
	for (size_t i = 0; i < strlen(device_address_str); i++) {
		if (device_address_str[i] == ':') {
			device_address_str[i] = '_';
		}
	}

	// Force a null-terminated character
	device_address_str[20] = '\0';

	// Generate object path like: /org/bluez/hci0/dev_DA_94_40_95_E0_87
	snprintf(object_path, object_path_len, "/org/bluez/%s/dev_%s", adapter, device_address_str);
}

static gboolean _stop_connect_func(gpointer data) {
	gattlib_connection_t *connection = data;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_valid(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "_stop_connect_func: Device not valid");
		goto EXIT;
	}

	// Reset the connection timeout
	connection->backend.connection_timeout_id = 0;

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);

	// We return FALSE when it is a one-off event
	return FALSE;
}

/**
 * @brief Function to asynchronously connect to a BLE device
 *
 * @note This function is mainly used before Bluez v5.42 (prior to D-BUS support)
 *
 * @param adapter	Local Adaptater interface. When passing NULL, we use default adapter.
 * @param dst		Remote Bluetooth address
 * @param options	Options to connect to BLE device. See `GATTLIB_CONNECTION_OPTIONS_*`
 * @param connect_cb is the callback to call when the connection is established
 * @param user_data is the user specific data to pass to the callback
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_connect(gattlib_adapter_t* adapter, const char *dst,
		unsigned long options,
		gatt_connect_cb_t connect_cb,
		void* user_data)
{
	const char* adapter_name = NULL;
	GError *error = NULL;
	char object_path[GATTLIB_DBUS_OBJECT_PATH_SIZE_MAX];
	int ret = GATTLIB_SUCCESS;

	// In case NULL is passed, we initialized default adapter
	if (adapter == NULL) {
		adapter = init_default_adapter();
		if (adapter == NULL) {
			GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_connect: No default adapter");
	        return GATTLIB_NOT_FOUND;
		}
	} else {
		adapter_name = adapter->name;
	}

	if (connect_cb == NULL) {
		GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_connect: Missing connection callback");
		return GATTLIB_INVALID_PARAMETER;
	}

	get_device_path_from_mac(adapter_name, dst, object_path, sizeof(object_path));

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_connect: Adapter not valid");
		ret = GATTLIB_ADAPTER_CLOSE;
		goto EXIT;
	}

	gattlib_device_t* device = gattlib_device_get_device(adapter, object_path);
	if (device == NULL) {
		GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_connect: Cannot find connection %s", dst);
		ret = GATTLIB_INVALID_PARAMETER;
		goto EXIT;
	} else if (device->state != DISCONNECTED) {
		GATTLIB_LOG(GATTLIB_DEBUG, "gattlib_connect: Cannot connect to '%s'. Device is in state %s",
			dst, device_state_str[device->state]);
		ret = GATTLIB_BUSY;
		goto EXIT;
	}

	device->connection.on_connection.callback.connection_handler = connect_cb;
	device->connection.on_connection.user_data = user_data;

	GATTLIB_LOG(GATTLIB_DEBUG, "Connecting bluetooth device %s", dst);

	// Mark the device has disconnected
	gattlib_device_set_state(device->adapter, device->device_id, CONNECTING);

	OrgBluezDevice1* bluez_device = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			&error);
	if (bluez_device == NULL) {
		ret = GATTLIB_ERROR_DBUS;
		if (error) {
			ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to connect to DBus Bluez Device: %s", error->message);
			g_error_free(error);
		} else {
			GATTLIB_LOG(GATTLIB_ERROR, "gattlib_connect: Failed to connect to DBus Bluez Device");
		}
		goto EXIT;
	} else {
		device->connection.backend.device = bluez_device;
		device->connection.backend.device_object_path = strdup(object_path);
	}

	// Register a handle for notification
	device->connection.backend.on_handle_device_property_change_id = g_signal_connect(bluez_device,
		"g-properties-changed",
		G_CALLBACK(on_handle_device_property_change),
		&device->connection);

	error = NULL;
	org_bluez_device1_call_connect_sync(bluez_device, NULL, &error);
	if (error) {
		if (strncmp(error->message, m_dbus_error_unknown_object, strlen(m_dbus_error_unknown_object)) == 0) {
			// You might have this error if the computer has not scanned or has not already had
			// pairing information about the targetted device.
			GATTLIB_LOG(GATTLIB_ERROR, "Device '%s' cannot be found (%d, %d)", dst, error->domain, error->code);
			ret = GATTLIB_NOT_FOUND;
		} else if ((error->domain == 238) && (error->code == 60952)) {
			GATTLIB_LOG(GATTLIB_ERROR, "Device '%s': %s", dst, error->message);
			ret = GATTLIB_TIMEOUT;
		} else {
			GATTLIB_LOG(GATTLIB_ERROR, "Device connected error (device:%s): %s",
				device->connection.backend.device_object_path,
				error->message);
			ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		}

		g_error_free(error);

		// Fail to connect. Mark the device has disconnected to be able to reconnect
		gattlib_device_set_state(adapter, device->device_id, DISCONNECTED);

		goto FREE_DEVICE;
	}

	// Wait for the property 'UUIDs' to be changed. We assume 'org.bluez.GattService1
	// and 'org.bluez.GattCharacteristic1' to be advertised at that moment.
	device->connection.backend.connection_timeout_id = g_timeout_add_seconds(CONNECT_TIMEOUT_SEC, _stop_connect_func, &device->connection);

	g_rec_mutex_unlock(&m_gattlib_mutex);
	return GATTLIB_SUCCESS;

FREE_DEVICE:
	g_rec_mutex_lock(&m_gattlib_mutex);
	free(device->connection.backend.device_object_path);
	device->connection.backend.device_object_path = NULL;
	g_rec_mutex_unlock(&m_gattlib_mutex);

	// destroy default adapter
	if(adapter == NULL) {
		gattlib_adapter_close(adapter);
	}

EXIT:
	if (ret != GATTLIB_SUCCESS) {
		connect_cb(adapter, dst, NULL, ret /* error */, user_data);
	}

	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}

/**
 * Clean GATTLIB connection on disconnection
 *
 * This function is called by the disconnection callback to always be called on explicit
 * and implicit disconnection.
 */
void gattlib_connection_free(gattlib_connection_t* connection) {
	char* device_id;

	device_id = connection->device->device_id;

	// Remove signal
	if (connection->backend.on_handle_device_property_change_id != 0) {
		g_signal_handler_disconnect(connection->backend.device, connection->backend.on_handle_device_property_change_id);
		connection->backend.on_handle_device_property_change_id = 0;
	}

	// Stop the timeout for connection
	if (connection->backend.connection_timeout_id) {
		g_source_remove(connection->backend.connection_timeout_id);
		connection->backend.connection_timeout_id = 0;
	}

	if (connection->backend.device_object_path != NULL) {
		free(connection->backend.device_object_path);
		connection->backend.device_object_path = NULL;
	}

	g_list_free_full(connection->backend.dbus_objects, g_object_unref);

	disconnect_all_notifications(&connection->backend);

	// Free all handler
	//TODO: Fixme - there is a memory leak by not freeing the handlers
	//gattlib_handler_free(&connection->on_connection);
	//gattlib_handler_free(&connection->on_disconnection);
	//gattlib_handler_free(&connection->indication);
	//gattlib_handler_free(&connection->notification);

	// Note: We do not free adapter as it might still be used by other devices

	// Mark the device has disconnected
	gattlib_device_set_state(connection->device->adapter, device_id, DISCONNECTED);
}

int gattlib_disconnect(gattlib_connection_t* connection, bool wait_disconnection) {
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	if (connection == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Cannot disconnect - connection parameter is not valid.");
		return GATTLIB_INVALID_PARAMETER;
	}

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_connected(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "Cannot disconnect - connection is not in connected state (state=%s).",
			device_state_str[connection->device->state]);
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_BUSY;
	}

	GATTLIB_LOG(GATTLIB_DEBUG, "Disconnecting bluetooth device %s", connection->backend.device_object_path);

	org_bluez_device1_call_disconnect_sync(connection->backend.device, NULL, &error);
	if (error) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to disconnect DBus Bluez Device: %s", error->message);
		g_error_free(error);

		// We continue, we still want to set the correct state
	}

	// Mark the device has disconnected
	gattlib_device_set_state(connection->device->adapter, connection->device->device_id, DISCONNECTING);

	//Note: Signals and memory will be removed/clean on disconnction callback
	//      See _gattlib_clean_on_disconnection()

	// We must release the mutex before the loop to leave other threads to signal the disconnection
	g_rec_mutex_unlock(&m_gattlib_mutex);

	if (wait_disconnection) {
		gint64 end_time;

		g_mutex_lock(&m_gattlib_signal.mutex);

		end_time = g_get_monotonic_time() + GATTLIB_DISCONNECTION_WAIT_TIMEOUT_SEC * G_TIME_SPAN_SECOND;

		while (gattlib_connection_is_connected(connection)) {
			if (!g_cond_wait_until(&m_gattlib_signal.condition, &m_gattlib_signal.mutex, end_time)) {
				ret = GATTLIB_TIMEOUT;
				break;
			}
		}

		g_mutex_unlock(&m_gattlib_signal.mutex);
	}

	return ret;
}

// Bluez was using org.bluez.Device1.GattServices until 5.37 to expose the list of available GATT Services
#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 38)
int gattlib_discover_primary(gattlib_connection_t* connection, gattlib_primary_service_t** services, int* services_count) {
	const gchar* const* service_str;
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (connection == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Gattlib connection not initialized.");
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_INVALID_PARAMETER;
	}

	if (!gattlib_connection_is_valid(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_discover_primary: Device not valid");
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_DEVICE_DISCONNECTED;
	}

	// Increase 'bluez_device' reference counter to avoid to keep the lock longer
	OrgBluezDevice1* bluez_device = connection->backend.device;
	g_object_ref(bluez_device);
	g_rec_mutex_unlock(&m_gattlib_mutex);

	const gchar* const* service_strs = org_bluez_device1_get_gatt_services(bluez_device);

	if (service_strs == NULL) {
		if (services != NULL) {
			*services       = NULL;
		}
		if (services_count != NULL) {
			*services_count = 0;
		}
		return GATTLIB_SUCCESS;
	}

	// Maximum number of primary services
	int count_max = 0, count = 0;
	for (service_str = service_strs; *service_str != NULL; service_str++) {
		count_max++;
	}

	gattlib_primary_service_t* primary_services = calloc(count_max * sizeof(gattlib_primary_service_t), 1);
	if (primary_services == NULL) {
		ret = GATTLIB_OUT_OF_MEMORY;
		goto EXIT;
	}

	for (service_str = service_strs; *service_str != NULL; service_str++) {
		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_PROXY_FLAGS_NONE,
				"org.bluez",
				*service_str,
				NULL,
				&error);
		if (service_proxy == NULL) {
			if (error) {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open service '%s': %s", *service_str, error->message);
				g_error_free(error);
			} else {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open service '%s'.", *service_str);
			}
			continue;
		}

		if (org_bluez_gatt_service1_get_primary(service_proxy)) {
			primary_services[count].attr_handle_start = 0;
			primary_services[count].attr_handle_end   = 0;

			gattlib_string_to_uuid(
					org_bluez_gatt_service1_get_uuid(service_proxy),
					MAX_LEN_UUID_STR + 1,
					&primary_services[count].uuid);
			count++;
		}

		g_object_unref(service_proxy);
	}

	if (services != NULL) {
		*services       = primary_services;
	}
	if (services_count != NULL) {
		*services_count = count;
	}

EXIT:
	g_object_unref(bluez_device);
	return ret;
}
#else
int gattlib_discover_primary(gattlib_connection_t* connection, gattlib_primary_service_t** services, int* services_count) {
	GError *error = NULL;
	const gchar* const* service_str;
	int ret = GATTLIB_SUCCESS;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (connection == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Gattlib connection not initialized.");
		ret = GATTLIB_INVALID_PARAMETER;
		goto EXIT;
	}

	if (!gattlib_connection_is_valid(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_discover_primary: Device not valid");
		ret = GATTLIB_DEVICE_DISCONNECTED;
		goto EXIT;
	}

	GDBusObjectManager *device_manager = get_device_manager_from_adapter(connection->device->adapter, &error);
	OrgBluezDevice1* device = connection->backend.device;

	const gchar* const* service_strs = org_bluez_device1_get_uuids(device);

	if (device_manager == NULL) {
		if (error != NULL) {
			ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
			GATTLIB_LOG(GATTLIB_ERROR, "Gattlib Context not initialized (%d, %d).", error->domain, error->code);
			g_error_free(error);
		} else {
			ret = GATTLIB_ERROR_DBUS;
			GATTLIB_LOG(GATTLIB_ERROR, "Gattlib Context not initialized.");
		}
		goto EXIT;
	}

	if (service_strs == NULL) {
		if (services != NULL) {
			*services       = NULL;
		}
		if (services_count != NULL) {
			*services_count = 0;
		}
		goto EXIT;
	}

	// Maximum number of primary services
	int count_max = 0, count = 0;
	for (service_str = service_strs; *service_str != NULL; service_str++) {
		count_max++;
	}

	gattlib_primary_service_t* primary_services = calloc(count_max * sizeof(gattlib_primary_service_t), 1);
	if (primary_services == NULL) {
		ret = GATTLIB_OUT_OF_MEMORY;
		goto EXIT;
	}

	GList *l;
	for (l = connection->backend.dbus_objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattService1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_PROXY_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (service_proxy == NULL) {
			if (error) {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open service '%s': %s", object_path, error->message);
				g_error_free(error);
			} else {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open service '%s'.", object_path);
			}
			continue;
		}

		// Ensure the service is attached to this device
        const gchar * service_property = org_bluez_gatt_service1_get_device(service_proxy);
        if (service_property == NULL) {
            if (error) {
                GATTLIB_LOG(GATTLIB_ERROR, "Failed to get service property '%s': %s", object_path, error->message);
                g_error_free(error);
            } else {
                GATTLIB_LOG(GATTLIB_ERROR, "Failed to get service property '%s'.", object_path);
            }
            continue;
        }
		if (strcmp(connection->backend.device_object_path, service_property)) {
			g_object_unref(service_proxy);
			continue;
		}

		if (org_bluez_gatt_service1_get_primary(service_proxy)) {
			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024
			// We convert the last 4 hex characters into the handle
			unsigned int service_handle = 0xFFFF; // Initialize with an invalid value.
			sscanf(object_path + strlen(object_path) - 4, "%x", &service_handle);
			primary_services[count].attr_handle_start = service_handle;
			primary_services[count].attr_handle_end   = service_handle;

			// Loop through all objects, as ordering is not guaranteed.
			for (GList *m = connection->backend.dbus_objects; m != NULL; m = m->next)  {
				GDBusObject *characteristic_object = m->data;
				const char* characteristic_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(characteristic_object));
				interface = g_dbus_object_manager_get_interface(device_manager, characteristic_path, "org.bluez.GattCharacteristic1");

				if (!interface) {
					continue;
				} else if (strncmp(object_path, characteristic_path, strlen(object_path)) != 0) {
					// The selected characteristic does not belong to the object, ignore.
					g_object_unref(interface);
					continue;
				} else {
					// Release the interface object to prevent memory leak.
					g_object_unref(interface);

					// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
					// We convert the last 4 hex characters into the handle
					unsigned int char_handle = primary_services[count].attr_handle_end; // Initialize with existing good value for safety.
					sscanf(characteristic_path + strlen(characteristic_path) - 4, "%x", &char_handle);

					// Once here, update the end handle of the service
					primary_services[count].attr_handle_end = MAX(primary_services[count].attr_handle_end, char_handle);
				}
			}

			gattlib_string_to_uuid(
					org_bluez_gatt_service1_get_uuid(service_proxy),
					MAX_LEN_UUID_STR + 1,
					&primary_services[count].uuid);
			count++;
		}

		g_object_unref(service_proxy);
	}

	if (services != NULL) {
		*services       = primary_services;
	}
	if (services_count != NULL) {
		*services_count = count;
	}

	if (ret != GATTLIB_SUCCESS) {
		free(primary_services);
	}

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}
#endif

// Bluez was using org.bluez.Device1.GattServices until 5.37 to expose the list of available GATT Services
#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 38)
int gattlib_discover_char_range(gattlib_connection_t* connection, uint16_t start, uint16_t end, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	GError *error = NULL;
	int handle;
	int ret = GATTLIB_SUCCESS;

	// Increase bluez_device object reference counter to avoid to keep locking the mutex
	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_valid(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_discover_char_range: Device not valid");
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_DEVICE_DISCONNECTED;
	}

	OrgBluezDevice1* bluez_device = connection->backend.bluez_device;
	g_object_ref(bluez_device);
	g_rec_mutex_unlock(&m_gattlib_mutex);

	const gchar* const* service_strs = org_bluez_device1_get_gatt_services(bluez_device);
	const gchar* const* service_str;
	const gchar* const* characteristic_strs;
	const gchar* characteristic_str;

	if (service_strs == NULL) {
		ret = GATTLIB_INVALID_PARAMETER;
		goto EXIT;
	}

	// Maximum number of primary services
	int count_max = 0, count = 0;
	for (service_str = service_strs; *service_str != NULL; service_str++) {
		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_PROXY_FLAGS_NONE,
				"org.bluez",
				*service_str,
				NULL,
				&error);
		if (service_proxy == NULL) {
			if (error) {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open services '%s': %s", *service_str, error->message);
				g_error_free(error);
			} else {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open services '%s'.", *service_str);
			}
			continue;
		}

		characteristic_strs = org_bluez_gatt_service1_get_characteristics(service_proxy);
		if (characteristic_strs == NULL) {
			g_object_unref(service_proxy);
			continue;
		}

		for (characteristic_str = *characteristic_strs; characteristic_str != NULL; characteristic_str++) {
			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
			// We convert the last 4 hex characters into the handle
			sscanf(characteristic_str + strlen(characteristic_str) - 4, "%x", &handle);

			// Check if handle is in range
			if ((handle < start) || (handle > end)) {
				continue;
			}

			count_max++;
		}

		g_object_unref(service_proxy);
	}


	gattlib_characteristic_t* characteristic_list = calloc(count_max * sizeof(gattlib_characteristic_t), 1);
	if (characteristic_list == NULL) {
		ret = GATTLIB_OUT_OF_MEMORY;
		goto EXIT;
	}

	for (service_str = service_strs; *service_str != NULL; service_str++) {
		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_PROXY_FLAGS_NONE,
				"org.bluez",
				*service_str,
				NULL,
				&error);
		if (service_proxy == NULL) {
			if (error) {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open service '%s': %s", *service_str, error->message);
				g_error_free(error);
			} else {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open service '%s'.", *service_str);
			}
			continue;
		}

		characteristic_strs = org_bluez_gatt_service1_get_characteristics(service_proxy);
		if (characteristic_strs == NULL) {
			g_object_unref(service_proxy);
			continue;
		}

		for (characteristic_str = *characteristic_strs; characteristic_str != NULL; characteristic_str++) {
			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
			// We convert the last 4 hex characters into the handle
			sscanf(characteristic_str + strlen(characteristic_str) - 4, "%x", &handle);

			// Check if handle is in range
			if ((handle < start) || (handle > end)) {
				continue;
			}

			OrgBluezGattCharacteristic1 *characteristic_proxy = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync(
					G_BUS_TYPE_SYSTEM,
					G_DBUS_PROXY_FLAGS_NONE,
					"org.bluez",
					characteristic_str,
					NULL,
					&error);
			if (characteristic_proxy == NULL) {
				if (error) {
					GATTLIB_LOG(GATTLIB_ERROR, "Failed to open characteristic '%s': %s", characteristic_str, error->message);
					g_error_free(error);
				} else {
					GATTLIB_LOG(GATTLIB_ERROR, "Failed to open characteristic '%s'.", characteristic_str);
				}
				continue;
			} else {
				characteristic_list[count].handle       = 0;
				characteristic_list[count].value_handle = 0;
				characteristic_list[count].properties = 0;

				const gchar *const * flags = org_bluez_gatt_characteristic1_get_flags(characteristic_proxy);
				for (; *flags != NULL; flags++) {
					if (strcmp(*flags,"broadcast") == 0) {
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_BROADCAST;
					} else if (strcmp(*flags,"read") == 0) {
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_READ;
					} else if (strcmp(*flags,"write") == 0) {
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_WRITE;
					} else if (strcmp(*flags,"write-without-response") == 0) {
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP;
					} else if (strcmp(*flags,"notify") == 0) {
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_NOTIFY;
					} else if (strcmp(*flags,"indicate") == 0) {
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_INDICATE;
					}
				}

				gattlib_string_to_uuid(
						org_bluez_gatt_characteristic1_get_uuid(characteristic_proxy),
						MAX_LEN_UUID_STR + 1,
						&characteristic_list[count].uuid);
				count++;
			}
			g_object_unref(characteristic_proxy);
		}
		g_object_unref(service_proxy);
	}

	*characteristics       = characteristic_list;
	*characteristics_count = count;

EXIT:
	g_object_unref(bluez_device);
	return ret;
}
#else
static void add_characteristics_from_service(struct _gattlib_connection_backend* backend, GDBusObjectManager *device_manager,
			const char* service_object_path,
			unsigned int start, unsigned int end,
			gattlib_characteristic_t* characteristic_list, int count_max, int* count)
{
	GError *error = NULL;

	for (GList *l = backend->dbus_objects; l != NULL; l = l->next) {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		OrgBluezGattCharacteristic1* characteristic = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_PROXY_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (characteristic == NULL) {
			if (error) {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open characteristic '%s': %s", object_path, error->message);
				g_error_free(error);
			} else {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open characteristic '%s'.", object_path);
			}
			continue;
		}

        const gchar * property_value = org_bluez_gatt_characteristic1_get_service(characteristic);
        if (property_value == NULL){
            if (error) {
                GATTLIB_LOG(GATTLIB_ERROR, "Failed to get service '%s': %s", object_path, error->message);
                g_error_free(error);
            } else {
                GATTLIB_LOG(GATTLIB_ERROR, "Failed to get service '%s'.", object_path);
            }
            continue;
        }
		if (strcmp(property_value, service_object_path)) {
			// This GATT characteristic is not for the current GATT service. Ignore it
			g_object_unref(characteristic);
			continue;
		} else {
			unsigned int handle;

			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
			// We convert the last 4 hex characters into the handle
			sscanf(object_path + strlen(object_path) - 4, "%x", &handle);

			// Check if handle is in range
			if ((handle < start) || (handle > end)) {
				continue;
			}

			// Sanity check to avoid buffer overflow
			if (*count >= count_max) {
				GATTLIB_LOG(GATTLIB_WARNING, "Skip GATT characteristic %s. Not enough space in the GATT characteristic array.", object_path);
				continue;
			}

			characteristic_list[*count].handle = handle;
			characteristic_list[*count].value_handle = handle;
			characteristic_list[*count].properties = 0;

			const gchar *const * flags = org_bluez_gatt_characteristic1_get_flags(characteristic);
			for (; *flags != NULL; flags++) {
				if (strcmp(*flags,"broadcast") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_BROADCAST;
				} else if (strcmp(*flags,"read") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_READ;
				} else if (strcmp(*flags,"write") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_WRITE;
				} else if (strcmp(*flags,"write-without-response") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP;
				} else if (strcmp(*flags,"notify") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_NOTIFY;
				} else if (strcmp(*flags,"indicate") == 0) {
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_INDICATE;
				}
			}

			gattlib_string_to_uuid(
					org_bluez_gatt_characteristic1_get_uuid(characteristic),
					MAX_LEN_UUID_STR + 1,
					&characteristic_list[*count].uuid);
			*count = *count + 1;
		}

		g_object_unref(characteristic);
	}
}

int gattlib_discover_char_range(gattlib_connection_t* connection, uint16_t start, uint16_t end, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	GError *error = NULL;
	GDBusObjectManager *device_manager;
	GList *l;
	int ret = GATTLIB_SUCCESS;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_valid(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_discover_char_range: Device not valid");
		ret = GATTLIB_DEVICE_DISCONNECTED;
		goto EXIT;
	}

	device_manager = get_device_manager_from_adapter(connection->device->adapter, &error);
	if (device_manager == NULL) {
		if (error != NULL) {
			ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
			GATTLIB_LOG(GATTLIB_ERROR, "Gattlib Context not initialized (%d, %d).", error->domain, error->code);
			g_error_free(error);
		} else {
			ret = GATTLIB_ERROR_DBUS;
			GATTLIB_LOG(GATTLIB_ERROR, "Gattlib Context not initialized.");
		}
		goto EXIT;
	}

	// Count the maximum number of characteristic to allocate the array (we count all the characterstic for all devices)
	int count_max = 0, count = 0;
	for (l = connection->backend.dbus_objects; l != NULL; l = l->next) {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			// Check if this DBUS Path is actually the Battery interface
			interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.Battery1");
			if (!interface) {
				continue;
			}
		}

		g_object_unref(interface);

		count_max++;
	}

	gattlib_characteristic_t* characteristic_list = calloc(count_max * sizeof(gattlib_characteristic_t), 1);
	if (characteristic_list == NULL) {
		ret = GATTLIB_OUT_OF_MEMORY;
		goto EXIT;
	}

	// List all services for this device
	for (l = connection->backend.dbus_objects; l != NULL; l = l->next) {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattService1");
		if (!interface) {
			// Check if this DBUS Path is actually the Battery interface. In this case,
			// we add a fake characteristic for the battery.
			interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.Battery1");
			if (interface) {
				g_object_unref(interface);

				// Sanity check to avoid buffer overflow
				if (count >= count_max) {
					GATTLIB_LOG(GATTLIB_WARNING, "Skip battery characteristic. Not enough space in the GATT characteristic array.");
					continue;
				}

				characteristic_list[count].handle = 0;
				characteristic_list[count].value_handle = 0;
				characteristic_list[count].properties = GATTLIB_CHARACTERISTIC_READ | GATTLIB_CHARACTERISTIC_NOTIFY;

				gattlib_string_to_uuid(
						"00002a19-0000-1000-8000-00805f9b34fb",
						MAX_LEN_UUID_STR + 1,
						&characteristic_list[count].uuid);
				count++;
			}

			continue;
		}

		g_object_unref(interface);

		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_PROXY_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (service_proxy == NULL) {
			if (error) {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open service '%s': %s", object_path, error->message);
				g_error_free(error);
			} else {
				GATTLIB_LOG(GATTLIB_ERROR, "Failed to open service '%s'.", object_path);
			}
			continue;
		}

		// Ensure the service is attached to this device
		const char* service_object_path = org_bluez_gatt_service1_get_device(service_proxy);
		if (strcmp(connection->backend.device_object_path, service_object_path)) {
			g_object_unref(service_proxy);
			continue;
		}

		// Add all characteristics attached to this service
		add_characteristics_from_service(&connection->backend, device_manager, object_path, start, end, characteristic_list,
			count_max, &count);
		g_object_unref(service_proxy);
	}

	*characteristics       = characteristic_list;
	*characteristics_count = count;
EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}
#endif

int gattlib_discover_char(gattlib_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristics_count)
{
	return gattlib_discover_char_range(connection, 0x00, 0xFF, characteristics, characteristics_count);
}

int gattlib_discover_desc_range(gattlib_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_discover_desc(gattlib_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return GATTLIB_NOT_SUPPORTED;
}

int get_bluez_device_from_mac(struct _gattlib_adapter *adapter, const char *mac_address, OrgBluezDevice1 **bluez_device1)
{
	GError *error = NULL;
	char object_path[GATTLIB_DBUS_OBJECT_PATH_SIZE_MAX];

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_adapter_is_valid(adapter)) {
		GATTLIB_LOG(GATTLIB_ERROR, "get_bluez_device_from_mac: Adapter not valid");
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_ADAPTER_CLOSE;
	}

	if (adapter->backend.adapter_proxy == NULL) {
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_NO_ADAPTER;
	}

	if (adapter != NULL) {
		get_device_path_from_mac_with_adapter(adapter->backend.adapter_proxy, mac_address, object_path, sizeof(object_path));
	} else {
		get_device_path_from_mac(NULL, mac_address, object_path, sizeof(object_path));
	}

	// No need to keep the mutex longer. After it is DBUS specific operations not depending on gattlib structure
	g_rec_mutex_unlock(&m_gattlib_mutex);

	*bluez_device1 = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			&error);
	if (error) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to connection to new DBus Bluez Device: %s", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS_WITH_ERROR(error);
	}

	return GATTLIB_SUCCESS;
}

int gattlib_get_rssi(gattlib_connection_t *connection, int16_t *rssi)
{
	if (rssi == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (connection == NULL) {
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_INVALID_PARAMETER;
	}

	if (!gattlib_connection_is_valid(connection)) {
		GATTLIB_LOG(GATTLIB_ERROR, "gattlib_get_rssi: Device not valid");
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_DEVICE_DISCONNECTED;
	}

	// device is actually a GObject. Increasing its reference counter prevents to
	// be freed if the connection is released.
	OrgBluezDevice1* dbus_device = connection->backend.device;
	g_object_ref(dbus_device);
	g_rec_mutex_unlock(&m_gattlib_mutex);

	*rssi = org_bluez_device1_get_rssi(dbus_device);

	g_object_unref(dbus_device);

	return GATTLIB_SUCCESS;
}

int gattlib_get_rssi_from_mac(gattlib_adapter_t* adapter, const char *mac_address, int16_t *rssi)
{
	OrgBluezDevice1 *bluez_device1;
	int ret;

	if (rssi == NULL || mac_address == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	//
	// No need of locking the mutex in this function. get_bluez_device_from_mac() ensures the adapter is valid.
	//

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		return ret;
	}

	*rssi = org_bluez_device1_get_rssi(bluez_device1);

	g_object_unref(bluez_device1);
	return GATTLIB_SUCCESS;
}
