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

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "gattlib_internal.h"

#define CONNECT_TIMEOUT  4

static const char *m_dbus_error_unknown_object = "GDBus.Error:org.freedesktop.DBus.Error.UnknownObject";

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
gatt_connection_t *gattlib_connect(const char *src, const char *dst, unsigned long options)
{
	GError *error = NULL;
	char object_path[100];

	get_device_path_from_mac(src, dst, object_path, sizeof(object_path));

	gattlib_context_t* conn_context = calloc(sizeof(gattlib_context_t), 1);
	if (conn_context == NULL) {
		return NULL;
	}

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
			fprintf(stderr, "Failed to connect to DBus Bluez Device: %s\n", error->message);
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
			fprintf(stderr, "Device '%s' cannot be found\n", dst);
		}  else {
			fprintf(stderr, "Device connected error (device:%s): %s\n",
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

	return connection;

FREE_DEVICE:
	free(conn_context->device_object_path);
	g_object_unref(conn_context->device);

FREE_CONNECTION:
	free(connection);

FREE_CONN_CONTEXT:
	free(conn_context);
	return NULL;
}

gatt_connection_t *gattlib_connect_async(const char *src, const char *dst,
				unsigned long options,
				gatt_connect_cb_t connect_cb, void* data)
{
	gatt_connection_t *connection;

	connection = gattlib_connect(src, dst, options);
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
		fprintf(stderr, "Failed to disconnect DBus Bluez Device: %s\n", error->message);
		g_error_free(error);
	}

	free(conn_context->device_object_path);
	g_object_unref(conn_context->device);

	free(connection->context);
	free(connection);
	return GATTLIB_SUCCESS;
}

// Bluez was using org.bluez.Device1.GattServices until 5.37 to expose the list of available GATT Services
#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 38)
int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count) {
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
				fprintf(stderr, "Failed to open service '%s': %s\n", *service_str, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open service '%s'.\n", *service_str);
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
	gattlib_context_t* conn_context = connection->context;
	OrgBluezDevice1* device = conn_context->device;
	const gchar* const* service_str;
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	const gchar* const* service_strs = org_bluez_device1_get_uuids(device);

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

	GDBusObjectManager *device_manager = g_dbus_object_manager_client_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			"/",
			NULL, NULL, NULL, NULL,
			&error);
	if (device_manager == NULL) {
		if (error) {
			fprintf(stderr, "Failed to get Bluez Device Manager: %s\n", error->message);
			g_error_free(error);
		} else {
			fprintf(stderr, "Failed to get Bluez Device Manager.\n");
		}
		ret = GATTLIB_ERROR_DBUS;
		goto ON_DEVICE_MANAGER_ERROR;
	}

	GList *objects = g_dbus_object_manager_get_objects(device_manager);
	GList *l;
	for (l = objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattService1");
		if (!interface) {
			continue;
		}

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
				fprintf(stderr, "Failed to open service '%s': %s\n", object_path, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open service '%s'.\n", object_path);
			}
			continue;
		}

		// Ensure the service is attached to this device
		if (strcmp(conn_context->device_object_path, org_bluez_gatt_service1_get_device(service_proxy))) {
			g_object_unref(service_proxy);
			continue;
		}

		if (org_bluez_gatt_service1_get_primary(service_proxy)) {
			primary_services[count].attr_handle_start = 0;
			primary_services[count].attr_handle_end   = 0;

			//Note: We assume the characteristics are always present after the services
			for (GList *m = l; m != NULL; m = m->next)  {
				GDBusObject *characteristic_object = m->data;
				const char* characteristic_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(characteristic_object));
				interface = g_dbus_object_manager_get_interface(device_manager, characteristic_path, "org.bluez.GattCharacteristic1");
				if (!interface) {
					continue;
				} else if (strncmp(object_path, characteristic_path, strlen(object_path)) != 0) {
					continue;
				} else {
					int char_handle;

					// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024/char0029'.
					// We convert the last 4 hex characters into the handle
					sscanf(characteristic_path + strlen(characteristic_path) - 4, "%x", &char_handle);

					if (primary_services[count].attr_handle_start == 0) {
						primary_services[count].attr_handle_start = char_handle;
					} else {
						primary_services[count].attr_handle_start = MIN(primary_services[count].attr_handle_start, char_handle);
					}

					if (primary_services[count].attr_handle_end == 0) {
						primary_services[count].attr_handle_end = char_handle;
					} else {
						primary_services[count].attr_handle_end = MAX(primary_services[count].attr_handle_end, char_handle);
					}
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

	g_list_free_full(objects, g_object_unref);
	g_object_unref(device_manager);

	if (services != NULL) {
		*services       = primary_services;
	}
	if (services_count != NULL) {
		*services_count = count;
	}

ON_DEVICE_MANAGER_ERROR:
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
				fprintf(stderr, "Failed to open services '%s': %s\n", *service_str, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open services '%s'.\n", *service_str);
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
				fprintf(stderr, "Failed to open service '%s': %s\n", *service_str, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open service '%s'.\n", *service_str);
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
					fprintf(stderr, "Failed to open characteristic '%s': %s\n", characteristic_str, error->message);
					g_error_free(error);
				} else {
					fprintf(stderr, "Failed to open characteristic '%s'.\n", characteristic_str);
				}
				continue;
			} else {
				characteristic_list[count].handle       = 0;
				characteristic_list[count].value_handle = 0;

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
static void add_characteristics_from_service(GDBusObjectManager *device_manager, const char* service_object_path, int start, int end,
					     gattlib_characteristic_t* characteristic_list, int* count)
{
	GList *objects = g_dbus_object_manager_get_objects(device_manager);
	GError *error = NULL;
	GList *l;

	for (l = objects; l != NULL; l = l->next) {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			continue;
		}

		OrgBluezGattCharacteristic1* characteristic = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (characteristic == NULL) {
			if (error) {
				fprintf(stderr, "Failed to open characteristic '%s': %s\n", object_path, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open characteristic '%s'.\n", object_path);
			}
			continue;
		}

		if (strcmp(org_bluez_gatt_characteristic1_get_service(characteristic), service_object_path)) {
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
	GError *error = NULL;
	GList *l;

	// Get list of services
	GDBusObjectManager *device_manager = g_dbus_object_manager_client_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			"/",
			NULL, NULL, NULL, NULL,
			&error);
	if (device_manager == NULL) {
		if (error) {
			fprintf(stderr, "Failed to get Bluez Device Manager: %s\n", error->message);
			g_error_free(error);
		} else {
			fprintf(stderr, "Failed to get Bluez Device Manager.\n");
		}
		return GATTLIB_OUT_OF_MEMORY;
	}
	GList *objects = g_dbus_object_manager_get_objects(device_manager);

	// Count the maximum number of characteristic to allocate the array (we count all the characterstic for all devices)
	int count_max = 0, count = 0;
	for (l = objects; l != NULL; l = l->next) {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			continue;
		}
		count_max++;
	}

	gattlib_characteristic_t* characteristic_list = malloc(count_max * sizeof(gattlib_characteristic_t));
	if (characteristic_list == NULL) {
		g_object_unref(device_manager);
		return GATTLIB_OUT_OF_MEMORY;
	}

	// List all services for this device
	for (l = objects; l != NULL; l = l->next) {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattService1");
		if (!interface) {
			continue;
		}

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
				fprintf(stderr, "Failed to open service '%s': %s\n", object_path, error->message);
				g_error_free(error);
			} else {
				fprintf(stderr, "Failed to open service '%s'.\n", object_path);
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
		add_characteristics_from_service(device_manager, object_path, start, end, characteristic_list, &count);
		g_object_unref(service_proxy);
	}

	g_list_free_full(objects, g_object_unref);
	g_object_unref(device_manager);

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

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
gboolean on_handle_battery_level_property_change(
		OrgBluezBattery1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	static guint8 percentage;
	gatt_connection_t* connection = user_data;

	if (gattlib_has_valid_handler(&connection->notification)) {
		// Retrieve 'Value' from 'arg_changed_properties'
		if (g_variant_n_children (arg_changed_properties) > 0) {
			GVariantIter *iter;
			const gchar *key;
			GVariant *value;

			g_variant_get (arg_changed_properties, "a{sv}", &iter);
			while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
				if (strcmp(key, "Percentage") == 0) {
					//TODO: by declaring 'percentage' as a 'static' would mean we could have issue in case of multiple
					//      GATT connection notifiying to Battery level
					percentage = g_variant_get_byte(value);

					gattlib_call_notification_handler(&connection->notification,
							&m_battery_level_uuid,
							(const uint8_t*)&percentage, sizeof(percentage));
					break;
				}
			}
		}
	}
	return TRUE;
}
#endif

static gboolean on_handle_characteristic_property_change(
	    OrgBluezGattCharacteristic1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	gatt_connection_t* connection = user_data;

	if (gattlib_has_valid_handler(&connection->notification)) {
		// Retrieve 'Value' from 'arg_changed_properties'
		if (g_variant_n_children (arg_changed_properties) > 0) {
			GVariantIter *iter;
			const gchar *key;
			GVariant *value;

			g_variant_get (arg_changed_properties, "a{sv}", &iter);
			while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
				if (strcmp(key, "Value") == 0) {
					uuid_t uuid;
					size_t data_length;
					const uint8_t* data = g_variant_get_fixed_array(value, &data_length, sizeof(guchar));

					gattlib_string_to_uuid(
							org_bluez_gatt_characteristic1_get_uuid(object),
							MAX_LEN_UUID_STR + 1,
							&uuid);

					gattlib_call_notification_handler(&connection->notification,
							&uuid, data, data_length);
					break;
				}
			}
		}
	}
	return TRUE;
}

int gattlib_notification_start(gatt_connection_t* connection, const uuid_t* uuid) {
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	}
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		// Register a handle for notification
		g_signal_connect(dbus_characteristic.battery,
			"g-properties-changed",
			G_CALLBACK (on_handle_battery_level_property_change),
			connection);

		return GATTLIB_SUCCESS;
	} else {
		assert(dbus_characteristic.type == TYPE_GATT);
	}
#endif

	// Register a handle for notification
	g_signal_connect(dbus_characteristic.gatt,
		"g-properties-changed",
		G_CALLBACK (on_handle_characteristic_property_change),
		connection);

	GError *error = NULL;
	org_bluez_gatt_characteristic1_call_start_notify_sync(dbus_characteristic.gatt, NULL, &error);

	if (error) {
		fprintf(stderr, "Failed to start DBus GATT notification: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	} else {
		return GATTLIB_SUCCESS;
	}
}

int gattlib_notification_stop(gatt_connection_t* connection, const uuid_t* uuid) {
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	}
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		g_signal_handlers_disconnect_by_func(
				dbus_characteristic.battery,
				G_CALLBACK (on_handle_battery_level_property_change),
				connection);
		return GATTLIB_SUCCESS;
	} else {
		assert(dbus_characteristic.type == TYPE_GATT);
	}
#endif

	g_signal_handlers_disconnect_by_func(
			dbus_characteristic.gatt,
			G_CALLBACK (on_handle_characteristic_property_change),
			connection);

	GError *error = NULL;
	org_bluez_gatt_characteristic1_call_stop_notify_sync(
		dbus_characteristic.gatt, NULL, &error);

	if (error) {
		fprintf(stderr, "Failed to stop DBus GATT notification: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_NOT_FOUND;
	} else {
		return GATTLIB_SUCCESS;
	}
}

#if 0 // Disable until https://github.com/labapart/gattlib/issues/75 is resolved
int gattlib_get_rssi(gatt_connection_t *connection, int16_t *rssi)
{
	gattlib_context_t* conn_context = connection->context;

	if (rssi == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	*rssi = org_bluez_device1_get_rssi(conn_context->device);

	return GATTLIB_SUCCESS;
}
#endif

int gattlib_get_rssi_from_mac(void *adapter, const char *mac_address, int16_t *rssi)
{
	GError *error = NULL;
	char object_path[100];

	if (rssi == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	if (adapter != NULL) {
		get_device_path_from_mac_with_adapter((OrgBluezAdapter1*)adapter, mac_address, object_path, sizeof(object_path));
	} else {
		get_device_path_from_mac(NULL, mac_address, object_path, sizeof(object_path));
	}

	OrgBluezDevice1* bluez_device1 = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			&error);
	if (error) {
		fprintf(stderr, "Failed to connection to new DBus Bluez Device: %s\n",
			error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	*rssi = org_bluez_device1_get_rssi(bluez_device1);
	return GATTLIB_SUCCESS;
}

int gattlib_get_advertisement_data(gatt_connection_t *connection, gattlib_advertisement_data_t **advertisement_data,
		uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size)
{
	//gattlib_context_t* conn_context = connection->context;

	return GATTLIB_NOT_SUPPORTED;
}
