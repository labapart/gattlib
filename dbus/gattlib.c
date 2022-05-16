/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2021, Olivier Martin <olivier@labapart.org>
 */

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "gattlib_internal.h"

#define CONNECT_TIMEOUT  4

static const char *m_dbus_error_unknown_object = "GDBus.Error:org.freedesktop.DBus.Error.UnknownObject";

static void* glib_event_thread(void* main_loop_p) {
	GMainLoop** main_loop = (GMainLoop**) main_loop_p;
	g_main_loop_run(*main_loop);
	return NULL;
}

gboolean on_handle_device_property_change(
	    OrgBluezGattCharacteristic1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	gatt_connection_t* connection = user_data;
	gattlib_context_t* conn_context = connection->context;

	// Retrieve 'Value' from 'arg_changed_properties'
	if (g_variant_n_children (arg_changed_properties) > 0) {
		GVariantIter *iter;
		const gchar *key;
		GVariant *value;

		g_variant_get (arg_changed_properties, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
			GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: device_property_change: %s: %s", key, g_variant_print(value, TRUE));
			if (strcmp(key, "Connected") == 0) {
				if (!g_variant_get_boolean(value)) {
					// Disconnection case
					if (gattlib_has_valid_handler(&connection->disconnection)) {
						gattlib_call_disconnection_handler(&connection->disconnection);
					}
				}
			} else if (strcmp(key, "ServicesResolved") == 0) {
				if (g_variant_get_boolean(value)) {
					// Stop the timeout for connection
					g_source_remove(conn_context->connection_timeout);

					// Tell we are now connected
					g_main_loop_quit(conn_context->connection_loop);
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
	strncpy(device_address_str, mac_address, sizeof(device_address_str));
	for (int i = 0; i < strlen(device_address_str); i++) {
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
	strncpy(device_address_str, mac_address, sizeof(device_address_str));
	for (int i = 0; i < strlen(device_address_str); i++) {
		if (device_address_str[i] == ':') {
			device_address_str[i] = '_';
		}
	}

	// Force a null-terminated character
	device_address_str[20] = '\0';

	// Generate object path like: /org/bluez/hci0/dev_DA_94_40_95_E0_87
	snprintf(object_path, object_path_len, "/org/bluez/%s/dev_%s", adapter, device_address_str);
}

/**
 * @param src		Local Adaptater interface
 * @param dst		Remote Bluetooth address
 * @param dst_type	Set LE address type (either BDADDR_LE_PUBLIC or BDADDR_LE_RANDOM)
 * @param sec_level	Set security level (either BT_IO_SEC_LOW, BT_IO_SEC_MEDIUM, BT_IO_SEC_HIGH)
 * @param psm       Specify the PSM for GATT/ATT over BR/EDR
 * @param mtu       Specify the MTU size
 */
gatt_connection_t *gattlib_connect(void* adapter, const char *dst, unsigned long options)
{
	struct gattlib_adapter *gattlib_adapter = adapter;
	const char* adapter_name = NULL;
	GDBusObjectManager *device_manager;
	GError *error = NULL;
	char object_path[100];

	// In case NULL is passed, we initialized default adapter
	if (gattlib_adapter == NULL) {
		gattlib_adapter = init_default_adapter();
	} else {
		adapter_name = gattlib_adapter->adapter_name;
	}

    // even after init_default_adapter() - the adapter can be NULL
    if (gattlib_adapter == NULL) {
        return NULL;
    }

	get_device_path_from_mac(adapter_name, dst, object_path, sizeof(object_path));

	gattlib_context_t* conn_context = calloc(sizeof(gattlib_context_t), 1);
	if (conn_context == NULL) {
		return NULL;
	}
	conn_context->adapter = gattlib_adapter;

	gatt_connection_t* connection = calloc(sizeof(gatt_connection_t), 1);
	if (connection == NULL) {
		goto FREE_CONN_CONTEXT;
	} else {
		connection->context = conn_context;
	}

	OrgBluezDevice1* device = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			&error);
	if (device == NULL) {
		if (error) {
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to connect to DBus Bluez Device: %s", error->message);
			g_error_free(error);
		}
		goto FREE_CONNECTION;
	} else {
		conn_context->device = device;
		conn_context->device_object_path = strdup(object_path);
	}

	// Register a handle for notification
	g_signal_connect(device,
		"g-properties-changed",
		G_CALLBACK (on_handle_device_property_change),
		connection);

	error = NULL;
	org_bluez_device1_call_connect_sync(device, NULL, &error);
	if (error) {
		if (strncmp(error->message, m_dbus_error_unknown_object, strlen(m_dbus_error_unknown_object)) == 0) {
			// You might have this error if the computer has not scanned or has not already had
			// pairing information about the targetted device.
			GATTLIB_LOG(GATTLIB_ERROR, "Device '%s' cannot be found", dst);
		}  else {
			GATTLIB_LOG(GATTLIB_ERROR, "Device connected error (device:%s): %s",
				conn_context->device_object_path,
				error->message);
		}

		g_error_free(error);
		goto FREE_DEVICE;
	}

	// Wait for the property 'UUIDs' to be changed. We assume 'org.bluez.GattService1
	// and 'org.bluez.GattCharacteristic1' to be advertised at that moment.
	conn_context->connection_loop = g_main_loop_new(NULL, 0);

	conn_context->connection_timeout = g_timeout_add_seconds(CONNECT_TIMEOUT, stop_scan_func,
								 conn_context->connection_loop);
	g_main_loop_run(conn_context->connection_loop);
	g_main_loop_unref(conn_context->connection_loop);
	// Set the attribute to NULL even if not required
	conn_context->connection_loop = NULL;

	// Get list of objects belonging to Device Manager
	device_manager = get_device_manager_from_adapter(conn_context->adapter);
    if (device_manager == NULL) {
        goto FREE_DEVICE;
    }
	conn_context->dbus_objects = g_dbus_object_manager_get_objects(device_manager);

	// Set up a new GMainLoop to handle notification/indication events.
	conn_context->connection_loop = g_main_loop_new(NULL, 0);
	pthread_create(&conn_context->event_thread, NULL, glib_event_thread, &conn_context->connection_loop);

	return connection;

FREE_DEVICE:
	free(conn_context->device_object_path);
	g_object_unref(conn_context->device);

FREE_CONNECTION:
	free(connection);

FREE_CONN_CONTEXT:
	free(conn_context);

	// destroy default adapter
	if(adapter == NULL)
	{
		gattlib_adapter_close(gattlib_adapter);
	}

	return NULL;
}

gatt_connection_t *gattlib_connect_async(void *adapter, const char *dst,
				unsigned long options,
				gatt_connect_cb_t connect_cb, void* data)
{
	gatt_connection_t *connection;

	connection = gattlib_connect(adapter, dst, options);
	if ((connection != NULL) && (connect_cb != NULL)) {
		connect_cb(connection, data);
	}

	return connection;
}

int gattlib_disconnect(gatt_connection_t* connection) {
	gattlib_context_t* conn_context = connection->context;
	GError *error = NULL;

	org_bluez_device1_call_disconnect_sync(conn_context->device, NULL, &error);
	if (error) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to disconnect DBus Bluez Device: %s", error->message);
		g_error_free(error);
	}

	free(conn_context->device_object_path);
	g_object_unref(conn_context->device);
	g_list_free_full(conn_context->dbus_objects, g_object_unref);
	g_main_loop_quit(conn_context->connection_loop);
	pthread_join(conn_context->event_thread, NULL);
	g_main_loop_unref(conn_context->connection_loop);
	disconnect_all_notifications(conn_context);
	
	free(conn_context->adapter->adapter_name);
	free(conn_context->adapter);

	free(connection->context);
	free(connection);
	return GATTLIB_SUCCESS;
}

// Bluez was using org.bluez.Device1.GattServices until 5.37 to expose the list of available GATT Services
#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 38)
int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count) {
	if (connection == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Gattlib connection not initialized.");
		return GATTLIB_INVALID_PARAMETER;
	}

	gattlib_context_t* conn_context = connection->context;
	OrgBluezDevice1* device = conn_context->device;
	const gchar* const* service_str;
	GError *error = NULL;

	const gchar* const* service_strs = org_bluez_device1_get_gatt_services(device);

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

	gattlib_primary_service_t* primary_services = malloc(count_max * sizeof(gattlib_primary_service_t));
	if (primary_services == NULL) {
		return GATTLIB_OUT_OF_MEMORY;
	}

	for (service_str = service_strs; *service_str != NULL; service_str++) {
		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
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
	return GATTLIB_SUCCESS;
}
#else
int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count) {
	if (connection == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Gattlib connection not initialized.");
		return GATTLIB_INVALID_PARAMETER;
	}

	gattlib_context_t* conn_context = connection->context;
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(conn_context->adapter);
	OrgBluezDevice1* device = conn_context->device;
	const gchar* const* service_str;
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	const gchar* const* service_strs = org_bluez_device1_get_uuids(device);

	if (device_manager == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Gattlib context not initialized.");
		return GATTLIB_INVALID_PARAMETER;
	}

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

	gattlib_primary_service_t* primary_services = malloc(count_max * sizeof(gattlib_primary_service_t));
	if (primary_services == NULL) {
		return GATTLIB_OUT_OF_MEMORY;
	}

	GList *l;
	for (l = conn_context->dbus_objects; l != NULL; l = l->next)  {
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
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
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
		if (strcmp(conn_context->device_object_path, service_property)) {
			g_object_unref(service_proxy);
			continue;
		}

		if (org_bluez_gatt_service1_get_primary(service_proxy)) {
			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024
			// We convert the last 4 hex characters into the handle
			int service_handle = 0xFFFF; // Initialize with an invalid value.
			sscanf(object_path + strlen(object_path) - 4, "%x", &service_handle);
			primary_services[count].attr_handle_start = service_handle;
			primary_services[count].attr_handle_end   = service_handle;

			// Loop through all objects, as ordering is not guaranteed.
			for (GList *m = conn_context->dbus_objects; m != NULL; m = m->next)  {
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
					int char_handle = primary_services[count].attr_handle_end; // Initialize with existing good value for safety.
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
	return ret;
}
#endif

// Bluez was using org.bluez.Device1.GattServices until 5.37 to expose the list of available GATT Services
#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 38)
int gattlib_discover_char_range(gatt_connection_t* connection, int start, int end, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	gattlib_context_t* conn_context = connection->context;
	OrgBluezDevice1* device = conn_context->device;
	GError *error = NULL;
	int handle;

	const gchar* const* service_strs = org_bluez_device1_get_gatt_services(device);
	const gchar* const* service_str;
	const gchar* const* characteristic_strs;
	const gchar* characteristic_str;

	if (service_strs == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	// Maximum number of primary services
	int count_max = 0, count = 0;
	for (service_str = service_strs; *service_str != NULL; service_str++) {
		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
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


	gattlib_characteristic_t* characteristic_list = malloc(count_max * sizeof(gattlib_characteristic_t));
	if (characteristic_list == NULL) {
		return GATTLIB_OUT_OF_MEMORY;
	}

	for (service_str = service_strs; *service_str != NULL; service_str++) {
		error = NULL;
		OrgBluezGattService1* service_proxy = org_bluez_gatt_service1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
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
					G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
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
	return GATTLIB_SUCCESS;
}
#else
static void add_characteristics_from_service(gattlib_context_t* conn_context, GDBusObjectManager *device_manager,
			const char* service_object_path,
			int start, int end,
			gattlib_characteristic_t* characteristic_list, int* count)
{
	GError *error = NULL;

	for (GList *l = conn_context->dbus_objects; l != NULL; l = l->next) {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			continue;
		}

		g_object_unref(interface);

		OrgBluezGattCharacteristic1* characteristic = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
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
			g_object_unref(characteristic);
			continue;
		} else {
			int handle;

			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
			// We convert the last 4 hex characters into the handle
			sscanf(object_path + strlen(object_path) - 4, "%x", &handle);

			// Check if handle is in range
			if ((handle < start) || (handle > end)) {
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

int gattlib_discover_char_range(gatt_connection_t* connection, int start, int end, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	gattlib_context_t* conn_context = connection->context;
	GDBusObjectManager *device_manager = get_device_manager_from_adapter(conn_context->adapter);
	GError *error = NULL;
	GList *l;

	if (device_manager == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Gattlib context not initialized.");
		return GATTLIB_INVALID_PARAMETER;
	}

	// Count the maximum number of characteristic to allocate the array (we count all the characterstic for all devices)
	int count_max = 0, count = 0;
	for (l = conn_context->dbus_objects; l != NULL; l = l->next) {
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

	gattlib_characteristic_t* characteristic_list = malloc(count_max * sizeof(gattlib_characteristic_t));
	if (characteristic_list == NULL) {
		return GATTLIB_OUT_OF_MEMORY;
	}

	// List all services for this device
	for (l = conn_context->dbus_objects; l != NULL; l = l->next) {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattService1");
		if (!interface) {
			// Check if this DBUS Path is actually the Battery interface. In this case,
			// we add a fake characteristic for the battery.
			interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.Battery1");
			if (interface) {
				g_object_unref(interface);
	
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
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
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
		if (strcmp(conn_context->device_object_path, service_object_path)) {
			g_object_unref(service_proxy);
			continue;
		}

		// Add all characteristics attached to this service
		add_characteristics_from_service(conn_context, device_manager, object_path, start, end, characteristic_list, &count);
		g_object_unref(service_proxy);
	}

	*characteristics       = characteristic_list;
	*characteristics_count = count;
	return GATTLIB_SUCCESS;
}
#endif

int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristics_count)
{
	return gattlib_discover_char_range(connection, 0x00, 0xFF, characteristics, characteristics_count);
}

int gattlib_discover_desc_range(gatt_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_discover_desc(gatt_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return GATTLIB_NOT_SUPPORTED;
}

int get_bluez_device_from_mac(struct gattlib_adapter *adapter, const char *mac_address, OrgBluezDevice1 **bluez_device1)
{
	GError *error = NULL;
	char object_path[100];

	if (adapter != NULL) {
		get_device_path_from_mac_with_adapter(adapter->adapter_proxy, mac_address, object_path, sizeof(object_path));
	} else {
		get_device_path_from_mac(NULL, mac_address, object_path, sizeof(object_path));
	}

	*bluez_device1 = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			&error);
	if (error) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to connection to new DBus Bluez Device: %s", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	return GATTLIB_SUCCESS;
}

int gattlib_get_rssi(gatt_connection_t *connection, int16_t *rssi)
{
	if (connection == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	gattlib_context_t* conn_context = connection->context;

	if (rssi == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	*rssi = org_bluez_device1_get_rssi(conn_context->device);

	return GATTLIB_SUCCESS;
}

int gattlib_get_rssi_from_mac(void *adapter, const char *mac_address, int16_t *rssi)
{
	OrgBluezDevice1 *bluez_device1;
	int ret;

	if (rssi == NULL || mac_address == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		g_object_unref(bluez_device1);
		return ret;
	}

	*rssi = org_bluez_device1_get_rssi(bluez_device1);

	g_object_unref(bluez_device1);
	return GATTLIB_SUCCESS;
}
