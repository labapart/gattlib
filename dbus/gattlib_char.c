/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
 */

#include <stdlib.h>

#include "gattlib_internal.h"

#define BLUEZ_GATT_WRITE_VALUE_TYPE_MASK                    (0x7)
#define BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE     (1 << 0)
#define BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITHOUT_RESPONSE  (1 << 1)
#define BLUEZ_GATT_WRITE_VALUE_TYPE_RELIABLE_WRITE          (1 << 2)


const uuid_t m_battery_level_uuid = CREATE_UUID16(0x2A19);
static const uuid_t m_ccc_uuid = CREATE_UUID16(0x2902);


static bool handle_dbus_gattcharacteristic_from_path(struct _gattlib_connection_backend* backend, const uuid_t* uuid,
		struct dbus_characteristic *dbus_characteristic, const char* object_path, GError **error)
{
	OrgBluezGattCharacteristic1 *characteristic = NULL;

	*error = NULL;
	characteristic = org_bluez_gatt_characteristic1_proxy_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			"org.bluez",
			object_path,
			NULL,
			error);
	if (characteristic) {
		if (uuid != NULL) {
			uuid_t characteristic_uuid;
			const gchar *characteristic_uuid_str = org_bluez_gatt_characteristic1_get_uuid(characteristic);
			if (characteristic_uuid_str == NULL) {
				// It should not be expected to get NULL from GATT characteristic UUID but we still test it
				GATTLIB_LOG(GATTLIB_ERROR, "Error: %s path unexpectly returns a NULL UUID.", object_path);
				g_object_unref(characteristic);
				return false;
			}

			gattlib_string_to_uuid(characteristic_uuid_str, strlen(characteristic_uuid_str) + 1, &characteristic_uuid);

			if (gattlib_uuid_cmp(uuid, &characteristic_uuid) != 0) {
				g_object_unref(characteristic);
				return false;
			}
		}

		// We found the right characteristic, now we check if it's the right device.
		*error = NULL;
		OrgBluezGattService1* service = org_bluez_gatt_service1_proxy_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			"org.bluez",
			org_bluez_gatt_characteristic1_get_service(characteristic),
			NULL,
			error);

		if (service) {
			const bool found = !strcmp(backend->device_object_path, org_bluez_gatt_service1_get_device(service));

			g_object_unref(service);

			if (found) {
				dbus_characteristic->gatt = characteristic;
				dbus_characteristic->type = TYPE_GATT;
				return true;
			}
		}

		g_object_unref(characteristic);
	}

	return false;
}

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
static bool handle_dbus_battery_from_uuid(struct _gattlib_connection_backend* backend, const uuid_t* uuid,
		struct dbus_characteristic *dbus_characteristic, const char* object_path, GError **error)
{
	OrgBluezBattery1 *battery = NULL;

	*error = NULL;
	battery = org_bluez_battery1_proxy_new_for_bus_sync (
			G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
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

struct dbus_characteristic get_characteristic_from_uuid(gattlib_connection_t* connection, const uuid_t* uuid) {
	GError *error = NULL;
	GDBusObjectManager *device_manager;
	bool is_battery_level_uuid = false;
	struct dbus_characteristic dbus_characteristic = {
		.type = TYPE_NONE
	};

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_connected(connection)) {
		goto EXIT;
	}

	device_manager = get_device_manager_from_adapter(connection->device->adapter, &error);

	if (device_manager == NULL) {
		if (error != NULL) {
			GATTLIB_LOG(GATTLIB_ERROR, "Gattlib Context not initialized (%d, %d).", error->domain, error->code);
			g_error_free(error);
		} else {
			GATTLIB_LOG(GATTLIB_ERROR, "Gattlib Context not initialized.");
		}
		goto EXIT; // Return characteristic of type TYPE_NONE
	}

	// Some GATT Characteristics are handled by D-BUS
	if (gattlib_uuid_cmp(uuid, &m_battery_level_uuid) == 0) {
		is_battery_level_uuid = true;
	} else if (gattlib_uuid_cmp(uuid, &m_ccc_uuid) == 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Error: Bluez v5.42+ does not expose Client Characteristic Configuration Descriptor through DBUS interface");
		goto EXIT;
	}

	GList *l;
	for (l = connection->backend.dbus_objects; l != NULL; l = l->next)  {
		GDBusInterface *interface;
		bool found = false;
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (interface) {
			g_object_unref(interface);

			found = handle_dbus_gattcharacteristic_from_path(&connection->backend, uuid, &dbus_characteristic, object_path, &error);
			if (found) {
				break;
			}
		}

		if (!found && is_battery_level_uuid) {
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
			interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.Battery1");
			if (interface) {
				g_object_unref(interface);

				found = handle_dbus_battery_from_uuid(&connection->backend, uuid, &dbus_characteristic, object_path, &error);
				if (found) {
					break;
				}
			}
#else
			GATTLIB_LOG(GATTLIB_ERROR, "You might use Bluez v5.48 with gattlib built for pre-v5.40");
#endif
		}
	}

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return dbus_characteristic;
}

static struct dbus_characteristic get_characteristic_from_handle(gattlib_connection_t* connection, unsigned int handle) {
	GError *error = NULL;
	unsigned int char_handle;
	GDBusObjectManager *device_manager;
	struct dbus_characteristic dbus_characteristic = {
		.type = TYPE_NONE
	};

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_connected(connection)) {
		goto EXIT;
	}

	device_manager = get_device_manager_from_adapter(connection->device->adapter, &error);

	if (device_manager == NULL) {
		if (error != NULL) {
			GATTLIB_LOG(GATTLIB_ERROR, "Gattlib Context not initialized (%d, %d).", error->domain, error->code);
			g_error_free(error);
		} else {
			GATTLIB_LOG(GATTLIB_ERROR, "Gattlib Context not initialized.");
		}
		goto EXIT;
	}

	for (GList *l = connection->backend.dbus_objects; l != NULL; l = l->next)  {
		GDBusInterface *interface;
		bool found;
		GDBusObject *object = l->data;
		const char* object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(object));

		interface = g_dbus_object_manager_get_interface(device_manager, object_path, "org.bluez.GattCharacteristic1");
		if (interface) {
			g_object_unref(interface);

			// Object path is in the form '/org/bluez/hci0/dev_DE_79_A2_A1_E9_FA/service0024'.
			// We convert the last 4 hex characters into the handle
			sscanf(object_path + strlen(object_path) - 4, "%x", &char_handle);

			if (char_handle != handle) {
				continue;
			}

			found = handle_dbus_gattcharacteristic_from_path(&connection->backend, NULL, &dbus_characteristic, object_path, &error);
			if (found) {
				break;
			}
		}
	}

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return dbus_characteristic;
}

static int read_gatt_characteristic(struct dbus_characteristic *dbus_characteristic, void **buffer, size_t* buffer_len) {
	GVariant *out_value;
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

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
		ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to read DBus GATT characteristic: %s", error->message);
		g_error_free(error);
		return ret;
	}

	gsize n_elements = 0;
	gconstpointer const_buffer = g_variant_get_fixed_array(out_value, &n_elements, sizeof(guchar));
	if (const_buffer) {
		*buffer = malloc(n_elements);
		if (*buffer == NULL) {
			ret = GATTLIB_OUT_OF_MEMORY;
			goto EXIT;
		}

		*buffer_len = n_elements;
		memcpy(*buffer, const_buffer, n_elements);
	} else {
		*buffer_len = 0;
	}

EXIT:
	g_variant_unref(out_value);
	return ret;
}

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
static int read_battery_level(struct dbus_characteristic *dbus_characteristic, void** buffer, size_t* buffer_len) {
	guchar percentage = org_bluez_battery1_get_percentage(dbus_characteristic->battery);

	*buffer = malloc(sizeof(uint8_t));
	if (buffer == NULL) {
		*buffer_len = 0;
		return GATTLIB_OUT_OF_MEMORY;
	}

	memcpy(*buffer, &percentage, sizeof(uint8_t));
	*buffer_len = sizeof(uint8_t);

	g_object_unref(dbus_characteristic->battery);
	return GATTLIB_SUCCESS;
}
#endif

int gattlib_read_char_by_uuid(gattlib_connection_t* connection, uuid_t* uuid, void **buffer, size_t *buffer_len) {
	//
	// No need of locking the gattlib mutex. get_characteristic_from_uuid() is taking care of the gattlib
	// object coherency. And 'dbus_characteristic' is not linked to gattlib object
	//

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
		int ret;

		assert(dbus_characteristic.type == TYPE_GATT);

		ret = read_gatt_characteristic(&dbus_characteristic, buffer, buffer_len);

		g_object_unref(dbus_characteristic.gatt);

		return ret;
	}
}

int gattlib_read_char_by_uuid_async(gattlib_connection_t* connection, uuid_t* uuid, gatt_read_cb_t gatt_read_cb) {
	int ret = GATTLIB_SUCCESS;

	//
	// No need of locking the gattlib mutex. get_characteristic_from_uuid() is taking care of the gattlib
	// object coherency. And 'dbus_characteristic' is not linked to gattlib object
	//

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
		ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to read DBus GATT characteristic: %s", error->message);
		g_error_free(error);
		goto EXIT;
	}

	gsize n_elements;
	gconstpointer const_buffer = g_variant_get_fixed_array(out_value, &n_elements, sizeof(guchar));
	if (const_buffer) {
		gatt_read_cb(const_buffer, n_elements);
	}

	g_object_unref(dbus_characteristic.gatt);
	g_variant_unref(out_value);

EXIT:
	return ret;
}

static int write_char(struct dbus_characteristic *dbus_characteristic, const void* buffer, size_t buffer_len, uint32_t options)
{
	GVariant *value = g_variant_new_from_data(G_VARIANT_TYPE ("ay"), buffer, buffer_len, TRUE, NULL, NULL);
	GError *error = NULL;
	int ret = GATTLIB_SUCCESS;

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)
	org_bluez_gatt_characteristic1_call_write_value_sync(dbus_characteristic->gatt, value, NULL, &error);
#else
	GVariantBuilder *variant_options = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if ((options & BLUEZ_GATT_WRITE_VALUE_TYPE_MASK) == BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITHOUT_RESPONSE) {
		g_variant_builder_add(variant_options, "{sv}", "type", g_variant_new("s", "command"));
	}

	org_bluez_gatt_characteristic1_call_write_value_sync(dbus_characteristic->gatt, value, g_variant_builder_end(variant_options), NULL, &error);
	g_variant_builder_unref(variant_options);
#endif

	if (error != NULL) {
		if ((error->domain == 238) && (error->code == 36)) {
			ret = GATTLIB_DEVICE_NOT_CONNECTED;
		} else {
			GATTLIB_LOG(GATTLIB_ERROR, "Failed to write DBus GATT characteristic: %s (%d,%d)",
				error->message, error->domain, error->code);
			ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		}
		g_error_free(error);
		return ret;
	}

	//
	// @note: No need to free `value` has it is freed by org_bluez_gatt_characteristic1_call_write_value_sync()
	//        See: https://developer.gnome.org/gio/stable/GDBusProxy.html#g-dbus-proxy-call
	//

	return ret;
}

int gattlib_write_char_by_uuid(gattlib_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len)
{
	int ret;

	//
	// No need of locking the gattlib mutex. get_characteristic_from_uuid() is taking care of the gattlib
	// object coherency. And 'dbus_characteristic' is not linked to gattlib object
	//

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	} else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		return GATTLIB_NOT_SUPPORTED; // Battery level does not support write
	} else {
		assert(dbus_characteristic.type == TYPE_GATT);
	}

	ret = write_char(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE);

	g_object_unref(dbus_characteristic.gatt);
	return ret;
}

int gattlib_write_char_by_handle(gattlib_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len)
{
	int ret;

	//
	// No need of locking the gattlib mutex. get_characteristic_from_handle() is taking care of the gattlib
	// object coherency. And 'dbus_characteristic' is not linked to gattlib object
	//

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_handle(connection, handle);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	}

	ret = write_char(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITH_RESPONSE);

	g_object_unref(dbus_characteristic.gatt);
	return ret;
}

int gattlib_write_without_response_char_by_uuid(gattlib_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len)
{
	int ret;

	//
	// No need of locking the gattlib mutex. get_characteristic_from_uuid() is taking care of the gattlib
	// object coherency. And 'dbus_characteristic' is not linked to gattlib object
	//

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	} else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		return GATTLIB_NOT_SUPPORTED; // Battery level does not support write
	} else {
		assert(dbus_characteristic.type == TYPE_GATT);
	}

	ret = write_char(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITHOUT_RESPONSE);

	g_object_unref(dbus_characteristic.gatt);
	return ret;
}

int gattlib_write_without_response_char_by_handle(gattlib_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len)
{
	int ret;

	//
	// No need of locking the gattlib mutex. get_characteristic_from_handle() is taking care of the gattlib
	// object coherency. And 'dbus_characteristic' is not linked to gattlib object
	//

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_handle(connection, handle);
	if (dbus_characteristic.type == TYPE_NONE) {
		return GATTLIB_NOT_FOUND;
	}

	ret = write_char(&dbus_characteristic, buffer, buffer_len, BLUEZ_GATT_WRITE_VALUE_TYPE_WRITE_WITHOUT_RESPONSE);

	g_object_unref(dbus_characteristic.gatt);
	return ret;
}

void gattlib_characteristic_free_value(void *ptr) {
	free(ptr);
}
