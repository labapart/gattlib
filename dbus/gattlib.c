/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2017 Olivier Martin <olivier@labapart.org>
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
#include <stdlib.h>

#include "gattlib_internal.h"

#define CONNECT_TIMEOUT  4

#ifdef GATTLIB_DEBUG_OUTPUT_ENABLE
int debug_num_loops = 0 ;
#endif

int gattlib_adapter_open(const char* adapter_name, void** adapter) {
	char object_path[20];
	OrgBluezAdapter1 *adapter_proxy;
	GError *error = NULL;

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
		ERROR_GATTLIB("Failed to get adapter %s\n", object_path);
		return 1;
	}


	*adapter = adapter_proxy;

	// Ensure the adapter is powered on
	org_bluez_adapter1_set_powered(adapter_proxy, TRUE);


	return 0;
}

/* gattlib_adapter_powered -- returns gboolean TRUE (1) if powered.
 * This is useful because you can open the adapter and still not
 * have access to functionaliy, if the user has "turned bluetooth off"
 * manually.
 */
int gattlib_adapter_powered(void* adapter) {
	OrgBluezAdapter1 *adapter_proxy = adapter;
	return org_bluez_adapter1_get_powered(adapter_proxy);
}

static gboolean loop_timeout_func(gpointer data) {
	DEBUG_GATTLIB("\nloop_timeout_func()!\n");
	DEBUG_GATTLIB("g_main_loop_quit\n");
	DEBUG_DEC_NUMLOOPS();
	g_main_loop_quit(data);
	return FALSE;
}

static gboolean stop_scan_func(gpointer data) {
	DEBUG_GATTLIB("\nstop_scan_func()!\n");
	return loop_timeout_func(data);
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

	if (device1) {
		gattlib_discovered_device_t discovered_device_cb = user_data;

		discovered_device_cb(
			org_bluez_device1_get_address(device1),
			org_bluez_device1_get_name(device1));
		g_object_unref(device1);
	}
}


/*
 * gattlib_adapter_scan_enable_setup
 * functionality brought out of scan_enable to allow
 * re-use for async version
 */
int gattlib_adapter_scan_enable_setup(void* adapter, gattlib_discovered_device_t discovered_device_cb,
	GDBusObjectManager **dev_manager) {
	
	GDBusObjectManager *device_manager ;
	GError *error = NULL;
	*dev_manager = NULL;

	org_bluez_adapter1_call_start_discovery_sync((OrgBluezAdapter1*)adapter, NULL, &error);

	//
	// Get notification when objects are removed from the Bluez ObjectManager.
	// We should get notified when the connection is lost with the target to allow
	// us to advertise us again
	//
	device_manager = g_dbus_object_manager_client_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			"/",
			NULL, NULL, NULL, NULL,
			&error);
	if (device_manager == NULL) {
		puts("Failed to get Bluez Device Manager.");
		return 1;
	}

	GList *objects = g_dbus_object_manager_get_objects(device_manager);
	GList *l;
	for (l = objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.Device1");
		if (!interface) {
			continue;
		}

		error = NULL;
		OrgBluezDevice1* device1 = org_bluez_device1_proxy_new_for_bus_sync(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);

		if (device1) {
			discovered_device_cb(
				org_bluez_device1_get_address(device1),
				org_bluez_device1_get_name(device1));
			g_object_unref(device1);
		}
	}

	g_list_free_full(objects, g_object_unref);

	g_signal_connect (G_DBUS_OBJECT_MANAGER(device_manager),
	                    "object-added",
	                    G_CALLBACK (on_dbus_object_added),
	                    discovered_device_cb);

	*dev_manager = device_manager;

	return 0;

}



int gattlib_adapter_scan_enable(void* adapter, gattlib_discovered_device_t discovered_device_cb, int timeout) {

	GDBusObjectManager *device_manager;
	int setupReturn = gattlib_adapter_scan_enable_setup(adapter, discovered_device_cb, &device_manager);
	if (setupReturn)
	{
		return setupReturn;
	}

	// Run Glib loop for 'timeout' seconds
	DEBUG_GATTLIB("g_main_loop_new (def context)\n");
	DEBUG_INC_NUMLOOPS();
	GMainLoop *loop = g_main_loop_new(NULL, 0);
	g_timeout_add_seconds (timeout, stop_scan_func, loop);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	g_object_unref(device_manager);
	return 0;
}



int gattlib_adapter_scan_disable(void* adapter) {
	GError *error = NULL;

	org_bluez_adapter1_call_stop_discovery_sync((OrgBluezAdapter1*)adapter, NULL, &error);
	return 0;
}





int gattlib_adapter_close(void* adapter) {
	g_object_unref(adapter);
	return 0;
}

gboolean on_handle_device_property_change(
	    OrgBluezGattCharacteristic1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	GMainLoop *loop = user_data;

	DEBUG_GATTLIB("\non_handle_device_property_change\n");
	// Retrieve 'Value' from 'arg_changed_properties'
	if (g_variant_n_children (arg_changed_properties) > 0) {
		GVariantIter *iter;
		const gchar *key;
		GVariant *value;

		g_variant_get (arg_changed_properties, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
			DEBUG_GATTLIB("  key: %s \n", key);
			if (strcmp(key, "UUIDs") == 0) {
			//if (1) {
				DEBUG_GATTLIB(" Quitting loop because of prop change\n");
				DEBUG_DEC_NUMLOOPS();
				g_main_loop_quit(loop);
				return FALSE;
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
gatt_connection_t *gattlib_connect(const char *src, const char *dst,
				uint8_t dest_type, gattlib_bt_sec_level_t sec_level, int psm, int mtu)
{
	GError *error = NULL;
	const char* adapter_name;
	char device_address_str[20];
	char object_path[100];
	int i;

	if (src) {
		adapter_name = src;
	} else {
		adapter_name = "hci0";
	}


	DEBUG_GATTLIB("gattlib attempting connect through adapter %s\n", adapter_name);

	// Transform string from 'DA:94:40:95:E0:87' to 'dev_DA_94_40_95_E0_87'
	strncpy(device_address_str, dst, sizeof(device_address_str));
	for (i = 0; i < strlen(device_address_str); i++) {
		if (device_address_str[i] == ':') {
			device_address_str[i] = '_';
		}
	}

	// Generate object path like: /org/bluez/hci0/dev_DA_94_40_95_E0_87
	snprintf(object_path, sizeof(object_path), "/org/bluez/%s/dev_%s", adapter_name, device_address_str);

	gattlib_context_t* conn_context = calloc(sizeof(gattlib_context_t), 1);
	if (conn_context == NULL) {
		DEBUG_GATTLIB("connect() couldn't calloc context!\n");
		return NULL;
	}

	gatt_connection_t* connection = calloc(sizeof(gatt_connection_t), 1);
	if (connection == NULL) {
		DEBUG_GATTLIB("connect() couldn't calloc connection!\n");
		return NULL;
	} else {
		connection->context = conn_context;
	}

	DEBUG_GATTLIB("getting bluez dev proxy...");
	OrgBluezDevice1* device = org_bluez_device1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			&error);
	if (device == NULL) {
		DEBUG_GATTLIB("connect() device not present!\n");
		goto FREE_CONNECTION;
	} else {
		conn_context->device = device;
		conn_context->device_object_path = strdup(object_path);
	}


	DEBUG_GATTLIB("got it, calling dev connect_sync\n");
	error = NULL;
	org_bluez_device1_call_connect_sync(device, NULL, &error);
	if (error) {
		ERROR_GATTLIB("Device connected error: %s\n", error->message);
		goto FREE_DEVICE;
	}

	// Wait for the property 'UUIDs' to be changed. We assume 'org.bluez.GattService1
	// and 'org.bluez.GattCharacteristic1' to be advertised at that moment.




	DEBUG_GATTLIB("gattlib_connect starting loop (timeout %i s.)", CONNECT_TIMEOUT);

#define CONNECT_USES_OWN_CONTEXT
#ifdef CONNECT_USES_OWN_CONTEXT
	// run this in its own context to avoid freezing 
	// any glib-loop-based app



	DEBUG_GATTLIB("g_main_loop_new (own context)\n");
	DEBUG_INC_NUMLOOPS();
	GMainContext * loopyContext = g_main_context_new();
	GMainLoop *loop = g_main_loop_new(loopyContext, 0);
	// Register a handle for notification
	g_signal_connect(device,
		"g-properties-changed",
		G_CALLBACK (on_handle_device_property_change),
		loop);

	g_timeout_add_seconds (CONNECT_TIMEOUT, loop_timeout_func, loop);
	g_main_loop_run(loop);
	g_main_context_unref(loopyContext);


#if 0 
	GMainContext * loopyContext = g_main_context_new();
	GMainLoop *loop = g_main_loop_new(loopyContext, TRUE);
	// Register a handle for notification
	g_signal_connect(device,
		"g-properties-changed",
		G_CALLBACK (on_handle_device_property_change),
		loop);

	guint tout = g_timeout_add_seconds (6, loop_timeout_func, loop);
	// guint tout = g_timeout_add_seconds (CONNECT_TIMEOUT, loop_timeout_func, loop);
	/* have to do the loop manually, as the signal will arrive 
	 * on the main/NULL loop
	 */
	while (g_main_loop_is_running(loop))
	{
		// DEBUG_GATTLIB(".");
		g_main_context_iteration(loopyContext, FALSE);
		g_main_context_iteration(NULL, FALSE);
	}
	g_source_remove(tout);
	g_main_context_unref(loopyContext);
#endif 

#else
	DEBUG_GATTLIB("g_main_loop_new (def context)\n");
	DEBUG_INC_NUMLOOPS();
	GMainLoop *loop = g_main_loop_new(NULL, TRUE);
	// Register a handle for notification
	g_signal_connect(device,
		"g-properties-changed",
		G_CALLBACK (on_handle_device_property_change),
		loop);

	guint tout = g_timeout_add_seconds (6, loop_timeout_func, loop);
	g_main_loop_run(loop);
#endif
	g_main_loop_unref(loop);

	return connection;

FREE_DEVICE:
	free(conn_context->device_object_path);
	g_object_unref(conn_context->device);

FREE_CONNECTION:
	free(connection);
	return NULL;
}




int gattlib_disconnect(gatt_connection_t* connection) {
	gattlib_context_t* conn_context = connection->context;
	GError *error = NULL;

	DEBUG_GATTLIB("gattlib_disconnect\n");
	org_bluez_device1_call_disconnect_sync(conn_context->device, NULL, &error);

	free(conn_context->device_object_path);
	g_object_unref(conn_context->device);

	free(connection->context);
	free(connection);
	return 0;
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
		return 0;
	}

	// Maximum number of primary services
	int count_max = 0, count = 0;
	for (service_str = service_strs; *service_str != NULL; service_str++) {
		count_max++;
	}

	gattlib_primary_service_t* primary_services = malloc(count_max * sizeof(gattlib_primary_service_t));
	if (primary_services == NULL) {
		return 1;
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
			ERROR_GATTLIB("Failed to open service '%s'.\n", *service_str);
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
	return 0;
}
#else
int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count) {
	gattlib_context_t* conn_context = connection->context;
	OrgBluezDevice1* device = conn_context->device;
	const gchar* const* service_str;
	GError *error = NULL;

	const gchar* const* service_strs = org_bluez_device1_get_uuids(device);

	if (service_strs == NULL) {
		*services       = NULL;
		*services_count = 0;
		return 0;
	}

	// Maximum number of primary services
	int count_max = 0, count = 0;
	for (service_str = service_strs; *service_str != NULL; service_str++) {
		count_max++;
	}

	gattlib_primary_service_t* primary_services = malloc(count_max * sizeof(gattlib_primary_service_t));
	if (primary_services == NULL) {
		return 1;
	}

	GDBusObjectManager *device_manager = g_dbus_object_manager_client_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			"/",
			NULL, NULL, NULL, NULL,
			&error);
	if (device_manager == NULL) {
		puts("Failed to get Bluez Device Manager.");
		return 1;
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
			ERROR_GATTLIB("Failed to open service '%s'.\n", object_path);
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
	return -1;
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
		return 2;
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
			ERROR_GATTLIB("Failed to open services '%s'.\n", *service_str);
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
		return 1;
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
			ERROR_GATTLIB("Failed to open service '%s'.\n", *service_str);
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
				ERROR_GATTLIB("Failed to open characteristic '%s'.\n", *characteristic_str);
				continue;
			} else {
				characteristic_list[count].handle       = 0;
				characteristic_list[count].value_handle = 0;

				DEBUG_GATTLIB("gattlib got char: ");
				const gchar *const * flags = org_bluez_gatt_characteristic1_get_flags(characteristic_proxy);
				for (; *flags != NULL; flags++) {
					if (strcmp(*flags,"broadcast") == 0) {
						DEBUG_GATTLIB("bcast ");
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_BROADCAST;
					} else if (strcmp(*flags,"read") == 0) {
						DEBUG_GATTLIB("read ");
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_READ;
					} else if (strcmp(*flags,"write") == 0) {
						DEBUG_GATTLIB("write ");
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_WRITE;
					} else if (strcmp(*flags,"write-without-response") == 0) {
						DEBUG_GATTLIB("write[no resp] ");
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP;
					} else if (strcmp(*flags,"notify") == 0) {
						DEBUG_GATTLIB("notify ");
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_NOTIFY;
					} else if (strcmp(*flags,"indicate") == 0) {
						DEBUG_GATTLIB("indicate ");
						characteristic_list[count].properties |= GATTLIB_CHARACTERISTIC_INDICATE;
					}
				}


				DEBUG_GATTLIB("\n");
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
	return 0;
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
			ERROR_GATTLIB("Failed to open characteristic '%s'.\n", object_path);
			continue;
		}

		if (strcmp(org_bluez_gatt_characteristic1_get_service(characteristic), service_object_path)) {
			continue;
		} else {
			characteristic_list[*count].handle       = 0;
			characteristic_list[*count].value_handle = 0;


			DEBUG_GATTLIB("gattlib got char: ");

			const gchar *const * flags = org_bluez_gatt_characteristic1_get_flags(characteristic);
			for (; *flags != NULL; flags++) {
				if (strcmp(*flags,"broadcast") == 0) {
					DEBUG_GATTLIB("bcast ");
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_BROADCAST;
				} else if (strcmp(*flags,"read") == 0) {
					DEBUG_GATTLIB("read ");
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_READ;
				} else if (strcmp(*flags,"write") == 0) {
					DEBUG_GATTLIB("write ");
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_WRITE;
				} else if (strcmp(*flags,"write-without-response") == 0) {
					DEBUG_GATTLIB("write[no resp] ");
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP;
				} else if (strcmp(*flags,"notify") == 0) {
					DEBUG_GATTLIB("notify ");
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_NOTIFY;
				} else if (strcmp(*flags,"indicate") == 0) {
					DEBUG_GATTLIB("indicate ");
					characteristic_list[*count].properties |= GATTLIB_CHARACTERISTIC_INDICATE;
				}
			}

			DEBUG_GATTLIB("\n");

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
		puts("Failed to get Bluez Device Manager.");
		return 1;
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
		return 1;
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
			ERROR_GATTLIB("Failed to open service '%s'.\n", object_path);
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
	return 0;
}
#endif


OrgBluezGattCharacteristic1 *get_characteristic_from_uuid(const uuid_t* uuid) {
	OrgBluezGattCharacteristic1 *characteristic = NULL;
	GError *error = NULL;

	GDBusObjectManager *device_manager = g_dbus_object_manager_client_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			"/",
			NULL, NULL, NULL, NULL,
			&error);
	if (device_manager == NULL) {
		puts("Failed to get Bluez Device Manager.");
		return NULL;
	}

	GList *objects = g_dbus_object_manager_get_objects(device_manager);
	GList *l;
	for (l = objects; l != NULL; l = l->next)  {
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		GDBusInterface *interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (!interface) {
			continue;
		}

		error = NULL;
		characteristic = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync (
				G_BUS_TYPE_SYSTEM,
				G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
				"org.bluez",
				object_path,
				NULL,
				&error);
		if (characteristic) {
			uuid_t characteristic_uuid;
			const gchar *characteristic_uuid_str = org_bluez_gatt_characteristic1_get_uuid(characteristic);

			gattlib_string_to_uuid(characteristic_uuid_str, strlen(characteristic_uuid_str) + 1, &characteristic_uuid);
			if (gattlib_uuid_cmp(uuid, &characteristic_uuid) == 0) {
				break;
			}

			g_object_unref(characteristic);
		}

		// Ensure we set 'characteristic' back to NULL
		characteristic = NULL;
	}

	g_list_free_full(objects, g_object_unref);
	g_object_unref(device_manager);
	return characteristic;
}

int gattlib_discover_desc_range(gatt_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return -1;
}

int gattlib_discover_desc(gatt_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return -1;
}

int gattlib_read_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, void* buffer, size_t* buffer_len) {
	OrgBluezGattCharacteristic1 *characteristic = get_characteristic_from_uuid(uuid);
	if (characteristic == NULL) {
		return -1;
	}

	GVariant *out_value;
	GError *error = NULL;

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)
	org_bluez_gatt_characteristic1_call_read_value_sync(
		characteristic, &out_value, NULL, &error);
#else
	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_characteristic1_call_read_value_sync(
		characteristic, g_variant_builder_end(options), &out_value, NULL, &error);
	g_variant_builder_unref(options);
#endif
	if (error != NULL) {
		return -1;
	}

	gsize n_elements = 0;
	gconstpointer const_buffer = g_variant_get_fixed_array(out_value, &n_elements, sizeof(guchar));
	if (const_buffer) {
		n_elements = MIN(n_elements, *buffer_len);
		memcpy(buffer, const_buffer, n_elements);
	}

	*buffer_len = n_elements;

	g_object_unref(characteristic);

#if BLUEZ_VERSION >= BLUEZ_VERSIONS(5, 40)
	//g_variant_unref(in_params); See: https://github.com/labapart/gattlib/issues/28#issuecomment-311486629
#endif
	return 0;
}

int gattlib_write_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len) {
	OrgBluezGattCharacteristic1 *characteristic = get_characteristic_from_uuid(uuid);
	if (characteristic == NULL) {

		DEBUG_GATTLIB("\nwrite() can't find this characteristic!\n");
		return -1;
	}

	GVariant *value = g_variant_new_from_data(G_VARIANT_TYPE ("ay"), buffer, buffer_len, TRUE, NULL, NULL);
	GError *error = NULL;

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)
	org_bluez_gatt_characteristic1_call_write_value_sync(characteristic, value, NULL, &error);
#else
	GVariantBuilder *options =  g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	org_bluez_gatt_characteristic1_call_write_value_sync(characteristic, value, g_variant_builder_end(options), NULL, &error);
	g_variant_builder_unref(options);
#endif
	if (error != NULL) {
		DEBUG_GATTLIB("\nwrite() error returned from bluez write\n");
		return -1;
	}

	g_object_unref(characteristic);
#if BLUEZ_VERSION >= BLUEZ_VERSIONS(5, 40)
	//g_variant_unref(in_params); See: https://github.com/labapart/gattlib/issues/28#issuecomment-311486629
#endif
	return 0;
}

int gattlib_write_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len) {
	return -1;
}

gboolean on_handle_characteristic_property_change(
	    OrgBluezGattCharacteristic1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	gatt_connection_t* connection = user_data;

	DEBUG_GATTLIB("\nchar prop changed called ");
	if (!connection->notification_handler) {
		DEBUG_GATTLIB("but we have NO notification_handler\n");
		return TRUE;
	}
	DEBUG_GATTLIB("and we have notification_handler ");
	// Retrieve 'Value' from 'arg_changed_properties'

	if (g_variant_n_children(arg_changed_properties) <= 0) {

		DEBUG_GATTLIB(" but 0 changed properties...\n");
	}
	DEBUG_GATTLIB(" and some changed properties...\n");
	GVariantIter *iter;
	const gchar *key;
	GVariant *value;

	g_variant_get(arg_changed_properties, "a{sv}", &iter);
	while (g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
		if (strcmp(key, "Value") == 0) {
			uuid_t uuid;
			size_t data_length;
			const uint8_t* data = g_variant_get_fixed_array(value, &data_length,
					sizeof(guchar));

			DEBUG_GATTLIB("Got Value of len %li, passing to notif handler\n", data_length);
			gattlib_string_to_uuid(
					org_bluez_gatt_characteristic1_get_uuid(object),
					MAX_LEN_UUID_STR + 1, &uuid);

			connection->notification_handler(&uuid, data, data_length,
					user_data);
			break;
		}
	}


	return TRUE;
}

int gattlib_notification_start(gatt_connection_t* connection, const uuid_t* uuid) {
	DEBUG_GATTLIB("\ngattlib_notification_start()... ");
	OrgBluezGattCharacteristic1 *characteristic = get_characteristic_from_uuid(uuid);
	if (characteristic == NULL) {
		DEBUG_GATTLIB("can't find this char\n");
		return -1;
	}

	// Register a handle for notification
	g_signal_connect(characteristic,
		"g-properties-changed",
		G_CALLBACK (on_handle_characteristic_property_change),
		connection);

	GError *error = NULL;
	org_bluez_gatt_characteristic1_call_start_notify_sync(characteristic, NULL, &error);

	if (error) {

		DEBUG_GATTLIB("error returned by bluez start notify\n");
		return 1;
	} else {
		DEBUG_GATTLIB("OK\n");
		return 0;
	}
}

int gattlib_notification_stop(gatt_connection_t* connection, const uuid_t* uuid) {
	DEBUG_GATTLIB("\ngattlib_notification_stop()... ");
	OrgBluezGattCharacteristic1 *characteristic = get_characteristic_from_uuid(uuid);
	if (characteristic == NULL) {
		DEBUG_GATTLIB("can't find this char\n");
		return -1;
	}

	GError *error = NULL;
	org_bluez_gatt_characteristic1_call_stop_notify_sync(
		characteristic, NULL, &error);

	if (error) {
		DEBUG_GATTLIB("error returned by bluez start notify\n");
		return 1;
	} else {
		DEBUG_GATTLIB("OK\n");
		return 0;
	}
}
