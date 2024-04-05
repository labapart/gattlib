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

struct gattlib_notification_handle {
	OrgBluezGattCharacteristic1 *gatt;
	gulong signal_id;
	uuid_t uuid;
};

#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
gboolean on_handle_battery_level_property_change(
		OrgBluezBattery1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	static guint8 percentage;
	gattlib_connection_t* connection = user_data;

	GATTLIB_LOG(GATTLIB_DEBUG, "DBUS: on_handle_battery_level_property_change: changed_properties:%s invalidated_properties:%s",
			g_variant_print(arg_changed_properties, TRUE),
			arg_invalidated_properties);

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_connected(connection)) {
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return FALSE;
	}

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

					gattlib_on_gatt_notification(connection,
							&m_battery_level_uuid,
							(const uint8_t*)&percentage, sizeof(percentage));
					break;
				}
			}
			g_variant_iter_free(iter);
		}
	}
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return TRUE;
}
#endif

static gboolean on_handle_characteristic_property_change(
	    OrgBluezGattCharacteristic1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	gattlib_connection_t* connection = user_data;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_connected(connection)) {
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return FALSE;
	}

	if (gattlib_has_valid_handler(&connection->notification)) {
		GVariantDict dict;
		g_variant_dict_init(&dict, arg_changed_properties);

		// Retrieve 'Value' from 'arg_changed_properties'
		GVariant* value = g_variant_dict_lookup_value(&dict, "Value", NULL);
		if (value != NULL) {
			uuid_t uuid;
			size_t data_length;
			const uint8_t* data = g_variant_get_fixed_array(value, &data_length, sizeof(guchar));

			// Dump the content of the notification
			//GATTLIB_LOG(GATTLIB_DEBUG, "on_handle_characteristic_property_change: %s: %s", key, g_variant_print(value, TRUE));
			GATTLIB_LOG(GATTLIB_DEBUG, "on_handle_characteristic_property_change: Value: Received %d bytes", data_length);

			gattlib_string_to_uuid(
					org_bluez_gatt_characteristic1_get_uuid(object),
					MAX_LEN_UUID_STR + 1,
					&uuid);

			gattlib_on_gatt_notification(connection, &uuid, data, data_length);

			// As per https://developer.gnome.org/glib/stable/glib-GVariant.html#g-variant-iter-loop, clean up `key` and `value`.
			g_variant_unref(value);
		}

		g_variant_dict_end(&dict);
	} else {
		GATTLIB_LOG(GATTLIB_DEBUG, "on_handle_characteristic_property_change: not a notification handler");
	}

	g_rec_mutex_unlock(&m_gattlib_mutex);
	return TRUE;
}

static gboolean on_handle_characteristic_indication(
	    OrgBluezGattCharacteristic1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	gattlib_connection_t* connection = user_data;

	if (gattlib_has_valid_handler(&connection->indication)) {
		// Retrieve 'Value' from 'arg_changed_properties'
		if (g_variant_n_children (arg_changed_properties) > 0) {
			GVariantIter *iter;
			const gchar *key;
			GVariant *value;

			g_variant_get (arg_changed_properties, "a{sv}", &iter);
			while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
				GATTLIB_LOG(GATTLIB_DEBUG, "on_handle_characteristic_indication: %s:%s",
						key, g_variant_print(value, TRUE));

				if (strcmp(key, "Value") == 0) {
					uuid_t uuid;
					size_t data_length;
					const uint8_t* data = g_variant_get_fixed_array(value, &data_length, sizeof(guchar));

					gattlib_string_to_uuid(
							org_bluez_gatt_characteristic1_get_uuid(object),
							MAX_LEN_UUID_STR + 1,
							&uuid);

					gattlib_on_gatt_notification(connection, &uuid, data, data_length);
					break;
				}
			}
			g_variant_iter_free(iter);
		}
	} else {
		GATTLIB_LOG(GATTLIB_DEBUG, "on_handle_characteristic_indication: Not a valid indication handler");
	}
	return TRUE;
}

static int connect_signal_to_characteristic_uuid(gattlib_connection_t* connection, const uuid_t* uuid, void *callback) {
	int ret = GATTLIB_SUCCESS;

	assert(callback != NULL);

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_connected(connection)) {
		ret = GATTLIB_INVALID_PARAMETER;
		goto EXIT;
	}

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		char uuid_str[MAX_LEN_UUID_STR + 1];

		gattlib_uuid_to_string(uuid, uuid_str, sizeof(uuid_str));

		GATTLIB_LOG(GATTLIB_ERROR, "GATT characteristic '%s' not found", uuid_str);
		ret = GATTLIB_NOT_FOUND;
		goto EXIT;
	}
#if BLUEZ_VERSION > BLUEZ_VERSIONS(5, 40)
	else if (dbus_characteristic.type == TYPE_BATTERY_LEVEL) {
		// Register a handle for notification
		g_signal_connect(dbus_characteristic.battery,
			"g-properties-changed",
			G_CALLBACK (on_handle_battery_level_property_change),
			connection);

		ret = GATTLIB_SUCCESS;
		goto EXIT;
	} else {
		assert(dbus_characteristic.type == TYPE_GATT);
	}
#endif

	// Register a handle for notification
	gulong signal_id = g_signal_connect(dbus_characteristic.gatt,
		"g-properties-changed",
		G_CALLBACK(callback),
		connection);
	if (signal_id == 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to connect signal to DBus GATT notification");
		ret = GATTLIB_ERROR_DBUS;
		goto EXIT;
	}

	// Add signal to the list
	struct gattlib_notification_handle *notification_handle = calloc(sizeof(struct gattlib_notification_handle), 1);
	if (notification_handle == NULL) {
		ret = GATTLIB_OUT_OF_MEMORY;
		goto EXIT;
	}
	notification_handle->gatt = dbus_characteristic.gatt;
	notification_handle->signal_id = signal_id;
	memcpy(&notification_handle->uuid, uuid, sizeof(*uuid));
	connection->backend.notified_characteristics = g_list_append(connection->backend.notified_characteristics, notification_handle);

	// Note: An optimisation could be to release mutex here after increasing reference counter of 'dbus_characteristic.gatt'

	GError *error = NULL;
	org_bluez_gatt_characteristic1_call_start_notify_sync(dbus_characteristic.gatt, NULL, &error);
	if (error) {
		ret = GATTLIB_ERROR_DBUS_WITH_ERROR(error);
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to start DBus GATT notification: %s", error->message);
		g_error_free(error);
		goto EXIT;
	}

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}

static int disconnect_signal_to_characteristic_uuid(gattlib_connection_t* connection, const uuid_t* uuid, void *callback) {
	struct gattlib_notification_handle *notification_handle = NULL;
	int ret = GATTLIB_SUCCESS;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (!gattlib_connection_is_connected(connection)) {
		ret = GATTLIB_INVALID_PARAMETER;
		goto EXIT;
	}

	// Find notification handle
	for (GList *l = connection->backend.notified_characteristics; l != NULL; l = l->next) {
		struct gattlib_notification_handle *notification_handle_ptr = l->data;
		if (gattlib_uuid_cmp(&notification_handle_ptr->uuid, uuid) == GATTLIB_SUCCESS) {
			notification_handle = notification_handle_ptr;

			connection->backend.notified_characteristics = g_list_delete_link(connection->backend.notified_characteristics, l);
			break;
		}
	}

	if (notification_handle == NULL) {
		ret = GATTLIB_NOT_FOUND;
		goto EXIT;
	}

	g_signal_handler_disconnect(notification_handle->gatt, notification_handle->signal_id);

	GError *error = NULL;
	org_bluez_gatt_characteristic1_call_stop_notify_sync(
			notification_handle->gatt, NULL, &error);

	free(notification_handle);

	if (error) {
		GATTLIB_LOG(GATTLIB_ERROR, "Failed to stop DBus GATT notification: %s", error->message);
		g_error_free(error);
		ret = GATTLIB_NOT_FOUND;
		goto EXIT;
	}

EXIT:
	g_rec_mutex_unlock(&m_gattlib_mutex);
	return ret;
}

int gattlib_notification_start(gattlib_connection_t* connection, const uuid_t* uuid) {
	return connect_signal_to_characteristic_uuid(connection, uuid, on_handle_characteristic_property_change);
}

int gattlib_notification_stop(gattlib_connection_t* connection, const uuid_t* uuid) {
	return disconnect_signal_to_characteristic_uuid(connection, uuid, on_handle_characteristic_property_change);
}

int gattlib_indication_start(gattlib_connection_t* connection, const uuid_t* uuid) {
	return connect_signal_to_characteristic_uuid(connection, uuid, on_handle_characteristic_indication);
}

int gattlib_indication_stop(gattlib_connection_t* connection, const uuid_t* uuid) {
	return disconnect_signal_to_characteristic_uuid(connection, uuid, on_handle_characteristic_indication);
}

static void end_notification(void *notified_characteristic) {
	struct gattlib_notification_handle *notification_handle = notified_characteristic;

	g_signal_handler_disconnect(notification_handle->gatt, notification_handle->signal_id);
	free(notification_handle);
}

void disconnect_all_notifications(struct _gattlib_connection_backend* backend) {
	g_list_free_full(g_steal_pointer(&backend->notified_characteristics), end_notification);
}
