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

#include <assert.h>
#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>

#include "gattlib_internal.h"

#define CONNECT_TIMEOUT  4

static const uuid_t m_battery_level_uuid = CREATE_UUID16(0x2A19);
static const uuid_t m_ccc_uuid = CREATE_UUID16(0x2902);

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

int gattlib_adapter_open(const char* adapter_name, void** adapter) {
	char object_path[20];
	OrgBluezAdapter1 *adapter_proxy;
	GError *error = NULL;

	if (adapter == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	if (adapter_name) {
		snprintf(object_path, sizeof(object_path), "/org/bluez/%s", adapter_name);
	} else {
		strncpy(object_path, "/org/bluez/hci0", sizeof(object_path));
	}

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

	*adapter = adapter_proxy;
	return GATTLIB_SUCCESS;
}

static gboolean stop_scan_func(gpointer data) {
	g_main_loop_quit(data);
	return FALSE;
}

void on_dbus_object_added(GDBusObjectManager *device_manager,
                     GDBusObject        *object,
                     gpointer            user_data)
{
	const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));
	GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.Device1");
	if (!interface) {
		return;
	}

	GError *error = NULL;
	OrgBluezDevice1* device1 = org_bluez_device1_proxy_new_for_bus_sync(
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
	}

	if (device1) {
		gattlib_discovered_device_t discovered_device_cb = user_data;

		discovered_device_cb(
			org_bluez_device1_get_address(device1),
			org_bluez_device1_get_name(device1));
		g_object_unref(device1);
	}
}

int gattlib_adapter_scan_enable(void* adapter, gattlib_discovered_device_t discovered_device_cb, int timeout) {
	GDBusObjectManager *device_manager;
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

	org_bluez_adapter1_call_start_discovery_sync((OrgBluezAdapter1*)adapter, NULL, &error);
	if (error) {
		fprintf(stderr, "Failed to start discovery: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	//
	// Get notification when objects are removed from the Bluez ObjectManager.
	// We should get notified when the connection is lost with the target to allow
	// us to advertise us again
	//
	device_manager = g_dbus_object_manager_client_new_for_bus_sync(
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
		goto DISABLE_SCAN;
	}

	g_signal_connect (G_DBUS_OBJECT_MANAGER(device_manager),
	                    "object-added",
	                    G_CALLBACK (on_dbus_object_added),
	                    discovered_device_cb);

	// Run Glib loop for 'timeout' seconds
	GMainLoop *loop = g_main_loop_new(NULL, 0);
	g_timeout_add_seconds (timeout, stop_scan_func, loop);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	g_object_unref(device_manager);

DISABLE_SCAN:
	// Stop BLE device discovery
	gattlib_adapter_scan_disable(adapter);
	return ret;
}

int gattlib_adapter_scan_disable(void* adapter) {
	GError *error = NULL;

	org_bluez_adapter1_call_stop_discovery_sync((OrgBluezAdapter1*)adapter, NULL, &error);
	// Ignore the error

	return GATTLIB_SUCCESS;
}

int gattlib_adapter_close(void* adapter) {
	g_object_unref(adapter);
	return GATTLIB_SUCCESS;
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
			if (strcmp(key, "Connected") == 0) {
				if (g_variant_get_boolean(value)) {
					// Stop the timeout for connection
					g_source_remove(conn_context->connection_timeout);

					// Tell we are now connected
					g_main_loop_quit(conn_context->connection_loop);
				} else {
					// Disconnection case
					if (connection->disconnection_handler) {
						connection->disconnection_handler(connection->disconnection_user_data);
					}
				}
			}
		}
	}
	return TRUE;
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
	const char* adapter_name;
	char device_address_str[20 + 1];
	char object_path[100];
	int i;

	if (src) {
		adapter_name = src;
	} else {
		adapter_name = "hci0";
	}

	// Transform string from 'DA:94:40:95:E0:87' to 'dev_DA_94_40_95_E0_87'
	strncpy(device_address_str, dst, sizeof(device_address_str));
	for (i = 0; i < strlen(device_address_str); i++) {
		if (device_address_str[i] == ':') {
			device_address_str[i] = '_';
		}
	}

	// Force a null-terminated character
	device_address_str[20] = '\0';

	// Generate object path like: /org/bluez/hci0/dev_DA_94_40_95_E0_87
	snprintf(object_path, sizeof(object_path), "/org/bluez/%s/dev_%s", adapter_name, device_address_str);

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
		fprintf(stderr, "Device connected error: %s\n", error->message);
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

void gattlib_register_on_disconnect(gatt_connection_t *connection, gattlib_disconnection_handler_t handler, void* user_data) {
	connection->disconnection_handler = handler;
	connection->disconnection_user_data = user_data;
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
		*services       = NULL;
		*services_count = 0;
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

	*services       = primary_services;
	*services_count = count;
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
		*services       = NULL;
		*services_count = 0;
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
	}

	g_list_free_full(objects, g_object_unref);
	g_object_unref(device_manager);

	*services       = primary_services;
	*services_count = count;
	return 0;
}
#endif

int gattlib_discover_char_range(gatt_connection_t* connection, int start, int end, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	return GATTLIB_NOT_SUPPORTED;
}

// Bluez was using org.bluez.Device1.GattServices until 5.37 to expose the list of available GATT Services
#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 38)
int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristic_count) {
	gattlib_context_t* conn_context = connection->context;
	OrgBluezDevice1* device = conn_context->device;
	GError *error = NULL;

	const gchar* const* service_strs = org_bluez_device1_get_gatt_services(device);
	const gchar* const* service_str;
	const gchar* const* characteristic_strs;
	const gchar* const* characteristic_str;

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
			continue;
		}

		for (characteristic_str = characteristic_strs; *characteristic_str != NULL; characteristic_str++) {
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
			continue;
		}

		for (characteristic_str = characteristic_strs; *characteristic_str != NULL; characteristic_str++) {
			OrgBluezGattCharacteristic1 *characteristic_proxy = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync(
					G_BUS_TYPE_SYSTEM,
					G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
					"org.bluez",
					*characteristic_str,
					NULL,
					&error);
			if (characteristic_proxy == NULL) {
				if (error) {
					fprintf(stderr, "Failed to open characteristic '%s': %s\n", *characteristic_str, error->message);
					g_error_free(error);
				} else {
					fprintf(stderr, "Failed to open characteristic '%s'.\n", *characteristic_str);
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

	*characteristics      = characteristic_list;
	*characteristic_count = count;
	return GATTLIB_SUCCESS;
}
#else
static void add_characteristics_from_service(GDBusObjectManager *device_manager, const char* service_object_path, gattlib_characteristic_t* characteristic_list, int* count) {
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
			continue;
		} else {
			characteristic_list[*count].handle       = 0;
			characteristic_list[*count].value_handle = 0;

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
	}
}

int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristic_count) {
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
			continue;
		}

		// Add all characteristics attached to this service
		add_characteristics_from_service(device_manager, object_path, characteristic_list, &count);
	}

	g_list_free_full(objects, g_object_unref);
	g_object_unref(device_manager);

	*characteristics      = characteristic_list;
	*characteristic_count = count;
	return GATTLIB_SUCCESS;
}
#endif

static bool handle_dbus_gattcharacteristic_from_uuid(gattlib_context_t* conn_context, const uuid_t* uuid,
		struct dbus_characteristic *dbus_characteristic, const char* object_path, GError **error)
{
	OrgBluezGattCharacteristic1 *characteristic = NULL;

	*error = NULL;
	characteristic = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			error);
	if (characteristic) {
		uuid_t characteristic_uuid;
		const gchar *characteristic_uuid_str = org_bluez_gatt_characteristic1_get_uuid(characteristic);

		gattlib_string_to_uuid(characteristic_uuid_str, strlen(characteristic_uuid_str) + 1, &characteristic_uuid);

		if (gattlib_uuid_cmp(uuid, &characteristic_uuid) == 0) {
			// We found the right characteristic, now we check if it's the right device.

			*error = NULL;
			OrgBluezGattService1* service = org_bluez_gatt_service1_proxy_new_for_bus_sync (
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				org_bluez_gatt_characteristic1_get_service(characteristic),
				NULL,
				error);

			if (service) {
				const bool found = !strcmp(conn_context->device_object_path, org_bluez_gatt_service1_get_device(service));

				g_object_unref(service);

				if (found) {
					dbus_characteristic->gatt = characteristic;
					dbus_characteristic->type = TYPE_GATT;
					return true;
				}
			}
		}

		g_object_unref(characteristic);
	}

	return false;
}

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
static bool handle_dbus_battery_from_uuid(gattlib_context_t* conn_context, const uuid_t* uuid,
		struct dbus_characteristic *dbus_characteristic, const char* object_path, GError **error)
{
	OrgBluezBattery1 *battery = NULL;

	*error = NULL;
	battery = org_bluez_battery1_proxy_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			error);
	if (battery) {
		dbus_characteristic->battery = battery;
		dbus_characteristic->type = TYPE_BATTERY_LEVEL;
	}

	return false;
}
#endif

static struct dbus_characteristic get_characteristic_from_uuid(gatt_connection_t* connection, const uuid_t* uuid) {
	gattlib_context_t* conn_context = connection->context;
	GError *error = NULL;
	bool is_battery_level_uuid = false;

	struct dbus_characteristic dbus_characteristic = {
			.type = TYPE_NONE
	};

	// Some GATT Characteristics are handled by D-BUS
	if (gattlib_uuid_cmp(uuid, &m_battery_level_uuid) == 0) {
		is_battery_level_uuid = true;
	} else if (gattlib_uuid_cmp(uuid, &m_ccc_uuid) == 0) {
		fprintf(stderr, "Error: Bluez v5.42+ does not expose Client Characteristic Configuration Descriptor through DBUS interface\n");
		return dbus_characteristic;
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
		return dbus_characteristic; // Return characteristic of type TYPE_NONE
	}

	GList *objects = g_dbus_object_manager_get_objects(device_manager);
	GList *l;
	for (l = objects; l != NULL; l = l->next)  {
		GDBusInterface *interface;
		bool found;
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (interface) {
			found = handle_dbus_gattcharacteristic_from_uuid(conn_context, uuid, &dbus_characteristic, object_path, &error);
			if (found) {
				break;
			}
		}

		if (!found && is_battery_level_uuid) {
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
			interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.Battery1");
			if (interface) {
				found = handle_dbus_battery_from_uuid(conn_context, uuid, &dbus_characteristic, object_path, &error);
				if (found) {
					break;
				}
			}
#else
			fprintf(stderr, "You might use Bluez v5.48 with gattlib built for pre-v5.40\n");
#endif
		}
	}

	g_list_free_full(objects, g_object_unref);
	g_object_unref(device_manager);
	return dbus_characteristic;
}

int gattlib_discover_desc_range(gatt_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_discover_desc(gatt_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return GATTLIB_NOT_SUPPORTED;
}

static int read_gatt_characteristic(struct dbus_characteristic *dbus_characteristic, void* buffer, size_t* buffer_len) {
	GVariant *out_value;
	GError *error = NULL;

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)
	org_bluez_gatt_characteristic1_call_read_value_sync(
		dbus_characteristic->gatt, &out_value, NULL, &error);
#else
	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_characteristic1_call_read_value_sync(
			dbus_characteristic->gatt, g_variant_builder_end(options), &out_value, NULL, &error);
	g_variant_builder_unref(options);
#endif
	if (error != NULL) {
		fprintf(stderr, "Failed to read DBus GATT characteristic: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	gsize n_elements = 0;
	gconstpointer const_buffer = g_variant_get_fixed_array(out_value, &n_elements, sizeof(guchar));
	if (const_buffer) {
		n_elements = MIN(n_elements, *buffer_len);
		memcpy(buffer, const_buffer, n_elements);
	}

	*buffer_len = n_elements;

	g_object_unref(dbus_characteristic->gatt);

#if BLUEZ_VERSION >= BLUEZ_VERSIONS(5, 40)
	//g_variant_unref(in_params); See: https://github.com/labapart/gattlib/issues/28#issuecomment-311486629
#endif
	return GATTLIB_SUCCESS;
}

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
static int read_battery_level(struct dbus_characteristic *dbus_characteristic, void* buffer, size_t* buffer_len) {
	guchar percentage = org_bluez_battery1_get_percentage(dbus_characteristic->battery);

	memcpy(buffer, &percentage, sizeof(uint8_t));
	*buffer_len = sizeof(uint8_t);

	return GATTLIB_SUCCESS;
}
#endif

int gattlib_read_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, void* buffer, size_t* buffer_len) {
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	}
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		return read_battery_level(&dbus_characteristic, buffer, buffer_len);
	}
#endif
	else {
		assert(dbus_characteristic.type == TYPE_GATT);

		return read_gatt_characteristic(&dbus_characteristic, buffer, buffer_len);
	}
}

int gattlib_read_char_by_uuid_async(gatt_connection_t* connection, uuid_t* uuid, gatt_read_cb_t gatt_read_cb) {
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	}
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		//TODO: Having 'percentage' as a 'static' is a limitation when we would support multiple connections
		static uint8_t percentage;

		percentage = org_bluez_battery1_get_percentage(dbus_characteristic.battery);

		gatt_read_cb((const void*)&percentage, sizeof(percentage));

		return GATTLIB_SUCCESS;
	} else {
		assert(dbus_characteristic.type == TYPE_GATT);
	}
#endif

	GVariant *out_value;
	GError *error = NULL;

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)
	org_bluez_gatt_characteristic1_call_read_value_sync(
		dbus_characteristic.gatt, &out_value, NULL, &error);
#else
	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_characteristic1_call_read_value_sync(
			dbus_characteristic.gatt, g_variant_builder_end(options), &out_value, NULL, &error);
	g_variant_builder_unref(options);
#endif
	if (error != NULL) {
		fprintf(stderr, "Failed to read DBus GATT characteristic: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	gsize n_elements;
	gconstpointer const_buffer = g_variant_get_fixed_array(out_value, &n_elements, sizeof(guchar));
	if (const_buffer) {
		gatt_read_cb(const_buffer, n_elements);
	}

	g_object_unref(dbus_characteristic.gatt);

#if BLUEZ_VERSION >= BLUEZ_VERSIONS(5, 40)
	//g_variant_unref(in_params); See: https://github.com/labapart/gattlib/issues/28#issuecomment-311486629
#endif
	return GATTLIB_SUCCESS;
}

int gattlib_write_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len) {
	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	} else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		return GATTLIB_NOT_SUPPORTED; // Battery level does not support write
	} else {
		assert(dbus_characteristic.type == TYPE_GATT);
	}

	GVariant *value = g_variant_new_from_data(G_VARIANT_TYPE ("ay"), buffer, buffer_len, TRUE, NULL, NULL);
	GError *error = NULL;

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)
	org_bluez_gatt_characteristic1_call_write_value_sync(dbus_characteristic.gatt, value, NULL, &error);
#else
	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_characteristic1_call_write_value_sync(dbus_characteristic.gatt, value, g_variant_builder_end(options), NULL, &error);
	g_variant_builder_unref(options);
#endif
	if (error != NULL) {
		fprintf(stderr, "Failed to write DBus GATT characteristic: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_ERROR_DBUS;
	}

	g_object_unref(dbus_characteristic.gatt);
#if BLUEZ_VERSION >= BLUEZ_VERSIONS(5, 40)
	//g_variant_unref(in_params); See: https://github.com/labapart/gattlib/issues/28#issuecomment-311486629
#endif
	return GATTLIB_SUCCESS;
}

int gattlib_write_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len) {
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

	if (connection->notification_handler) {
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

					connection->notification_handler(&m_battery_level_uuid,
							(const uint8_t*)&percentage, sizeof(percentage),
							connection->notification_user_data);
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

	if (connection->notification_handler) {
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

					connection->notification_handler(&uuid, data, data_length, connection->notification_user_data);
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
		return GATTLIB_ERROR_DBUS;
	} else {
		return GATTLIB_SUCCESS;
	}
}
