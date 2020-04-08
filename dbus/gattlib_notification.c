/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2020 Olivier Martin <olivier@labapart.org>
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
			g_variant_iter_free(iter);
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
			g_variant_iter_free(iter);
		}
	}
	return TRUE;
}

static gboolean on_handle_characteristic_indication(
	    OrgBluezGattCharacteristic1 *object,
	    GVariant *arg_changed_properties,
	    const gchar *const *arg_invalidated_properties,
	    gpointer user_data)
{
	gatt_connection_t* connection = user_data;

	if (gattlib_has_valid_handler(&connection->indication)) {
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

					gattlib_call_notification_handler(&connection->indication,
							&uuid, data, data_length);
					break;
				}
			}
			g_variant_iter_free(iter);
		}
	}
	return TRUE;
}

static int connect_signal_to_characteristic_uuid(gatt_connection_t* connection, const uuid_t* uuid, void *callback) {
	gattlib_context_t* conn_context = connection->context;

	struct dbus_characteristic dbus_characteristic = get_characteristic_from_uuid(connection, uuid);
	if (dbus_characteristic.type == TYPE_NONE) {
		puts("Not found");
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
	gulong signal_id = g_signal_connect(dbus_characteristic.gatt,
		"g-properties-changed",
		G_CALLBACK(callback),
		connection);
	if (signal_id == 0) {
		fprintf(stderr, "Failed to connect signal to DBus GATT notification\n");
		return GATTLIB_ERROR_DBUS;
	}

	// Add signal to the list
	struct gattlib_notification_handle *notification_handle = malloc(sizeof(struct gattlib_notification_handle));
	if (notification_handle == NULL) {
		return GATTLIB_OUT_OF_MEMORY;
	}
	notification_handle->gatt = dbus_characteristic.gatt;
	notification_handle->signal_id = signal_id;
	memcpy(&notification_handle->uuid, uuid, sizeof(*uuid));
	conn_context->notified_characteristics = g_list_append(conn_context->notified_characteristics, notification_handle);

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

static int disconnect_signal_to_characteristic_uuid(gatt_connection_t* connection, const uuid_t* uuid, void *callback) {
	gattlib_context_t* conn_context = connection->context;
	struct gattlib_notification_handle *notification_handle = NULL;

	// Find notification handle
	for (GList *l = conn_context->notified_characteristics; l != NULL; l = l->next) {
		struct gattlib_notification_handle *notification_handle_ptr = l->data;
		if (gattlib_uuid_cmp(&notification_handle_ptr->uuid, uuid) == GATTLIB_SUCCESS) {
			notification_handle = notification_handle_ptr;

			conn_context->notified_characteristics = g_list_delete_link(conn_context->notified_characteristics, l);
			break;
		}
	}

	if (notification_handle == NULL) {
		return GATTLIB_NOT_FOUND;
	}

	g_signal_handler_disconnect(notification_handle->gatt, notification_handle->signal_id);

	GError *error = NULL;
	org_bluez_gatt_characteristic1_call_stop_notify_sync(
			notification_handle->gatt, NULL, &error);

	free(notification_handle);

	if (error) {
		fprintf(stderr, "Failed to stop DBus GATT notification: %s\n", error->message);
		g_error_free(error);
		return GATTLIB_NOT_FOUND;
	} else {
		return GATTLIB_SUCCESS;
	}
}

int gattlib_notification_start(gatt_connection_t* connection, const uuid_t* uuid) {
	return connect_signal_to_characteristic_uuid(connection, uuid, on_handle_characteristic_property_change);
}

int gattlib_notification_stop(gatt_connection_t* connection, const uuid_t* uuid) {
	return disconnect_signal_to_characteristic_uuid(connection, uuid, on_handle_characteristic_property_change);
}

int gattlib_indication_start(gatt_connection_t* connection, const uuid_t* uuid) {
	return connect_signal_to_characteristic_uuid(connection, uuid, on_handle_characteristic_indication);
}

int gattlib_indication_stop(gatt_connection_t* connection, const uuid_t* uuid) {
	return disconnect_signal_to_characteristic_uuid(connection, uuid, on_handle_characteristic_indication);
}

void disconnect_all_notifications(gattlib_context_t* conn_context) {
	// Find notification handle
	for (GList *l = conn_context->notified_characteristics; l != NULL; l = l->next) {
		struct gattlib_notification_handle *notification_handle = l->data;

		g_signal_handler_disconnect(notification_handle->gatt, notification_handle->signal_id);
		free(notification_handle);
	}

	g_list_free_full(conn_context->notified_characteristics, g_object_unref);
}
