/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2021 Olivier Martin <olivier@labapart.org>
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
#include <stdlib.h>
#include <string.h>

#include "gattlib_internal.h"

#include "uuid.h"
#include "att.h"
#include "gattrib.h"
#include "gatt.h"

struct primary_all_cb_t {
	gattlib_primary_service_t* services;
	int services_count;
	int discovered;
};

#if BLUEZ_VERSION_MAJOR == 4
static void primary_all_cb(GSList *services, guint8 status, gpointer user_data) {
#else
static void primary_all_cb(uint8_t status, GSList *services, void *user_data) {
#endif
	struct primary_all_cb_t* data = user_data;
	GSList *l;
	int i;

	if (status) {
		fprintf(stderr, "Discover all primary services failed: %s\n", att_ecode2str(status));
		goto done;
	}

	// Allocate array
	data->services_count = g_slist_length(services);
	data->services = malloc(data->services_count * sizeof(gattlib_primary_service_t));
	if (data->services == NULL) {
		fprintf(stderr, "Discover all primary services failed: OutOfMemory\n");
		goto done;
	}

	for (i = 0, l = services; l; l = l->next, i++) {
		struct gatt_primary *prim = l->data;

		data->services[i].attr_handle_start = prim->range.start;
		data->services[i].attr_handle_end   = prim->range.end;
		gattlib_string_to_uuid(prim->uuid, MAX_LEN_UUID_STR + 1, &data->services[i].uuid);

		assert(i < data->services_count);
	}

done:
	data->discovered = TRUE;
}

int gattlib_discover_primary(gattlib_connection_t* connection, gattlib_primary_service_t** services, int* services_count) {
	struct primary_all_cb_t user_data;
	guint ret;

	bzero(&user_data, sizeof(user_data));
	user_data.discovered     = FALSE;

	gattlib_context_t* conn_context = connection->context;
	ret = gatt_discover_primary(conn_context->attrib, NULL, primary_all_cb, &user_data);
	if (ret == 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Fail to discover primary services.");
		return GATTLIB_ERROR_BLUEZ_WITH_ERROR(ret);
	}

	// Wait for completion
	while(user_data.discovered == FALSE) {
		g_main_context_iteration(g_gattlib_thread.loop_context, FALSE);
	}

	if (services != NULL) {
		*services = user_data.services;
	}
	if (services_count != NULL) {
		*services_count = user_data.services_count;
	}

	return GATTLIB_SUCCESS;
}

struct characteristic_cb_t {
	gattlib_characteristic_t* characteristics;
	int characteristics_count;
	int discovered;
};

#if BLUEZ_VERSION_MAJOR == 4
static void characteristic_cb(GSList *characteristics, guint8 status, gpointer user_data) {
#else
static void characteristic_cb(uint8_t status, GSList *characteristics, void *user_data) {
#endif
	struct characteristic_cb_t* data = user_data;
	GSList *l;
	int i;

	if (status) {
		fprintf(stderr, "Discover all characteristics failed: %s\n", att_ecode2str(status));
		goto done;
	}

	// Allocate array
	data->characteristics_count = g_slist_length(characteristics);
	data->characteristics = malloc(data->characteristics_count * sizeof(gattlib_characteristic_t));
	if (data->characteristics == NULL) {
		fprintf(stderr, "Discover all characteristics failed: OutOfMemory\n");
		goto done;
	}

	for (i = 0, l = characteristics; l; l = l->next, i++) {
		struct gatt_char *chars = l->data;

		data->characteristics[i].handle       = chars->handle;
		data->characteristics[i].properties   = chars->properties;
		data->characteristics[i].value_handle = chars->value_handle;
		gattlib_string_to_uuid(chars->uuid, MAX_LEN_UUID_STR + 1, &data->characteristics[i].uuid);

		assert(i < data->characteristics_count);
	}

done:
	data->discovered = TRUE;
}

int gattlib_discover_char_range(gattlib_connection_t* connection, uint16_t start, uint16_t end, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	struct characteristic_cb_t user_data;
	guint ret;

	bzero(&user_data, sizeof(user_data));
	user_data.discovered     = FALSE;

	gattlib_context_t* conn_context = connection->context;
	ret = gatt_discover_char(conn_context->attrib, start, end, NULL, characteristic_cb, &user_data);
	if (ret == 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Fail to discover characteristics.");
		return GATTLIB_ERROR_BLUEZ_WITH_ERROR(ret);
	}

	// Wait for completion
	while(user_data.discovered == FALSE) {
		g_main_context_iteration(g_gattlib_thread.loop_context, FALSE);
	}
	*characteristics       = user_data.characteristics;
	*characteristics_count = user_data.characteristics_count;

	return GATTLIB_SUCCESS;
}

int gattlib_discover_char(gattlib_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	return gattlib_discover_char_range(connection, 0x0001, 0xffff, characteristics, characteristics_count);
}

struct descriptor_cb_t {
	gattlib_descriptor_t* descriptors;
	int descriptors_count;
	int discovered;
};

#if BLUEZ_VERSION_MAJOR == 4
static void char_desc_cb(guint8 status, const guint8 *pdu, guint16 plen, gpointer user_data)
{
	struct descriptor_cb_t* data = user_data;
	struct att_data_list *list;
	guint8 format;
	int i;

	if (status) {
		fprintf(stderr, "Discover all descriptors failed: %s\n", att_ecode2str(status));
		goto done;
	}

	list = dec_find_info_resp(pdu, plen, &format);
	if (list == NULL)
		goto done;

	// Allocate array
	data->descriptors_count = list->num;
	data->descriptors = malloc(data->descriptors_count * sizeof(gattlib_descriptor_t));
	if (data->descriptors == NULL) {
		fprintf(stderr, "Discover all descriptors failed: OutOfMemory\n");
		goto done;
	}

	for (i = 0; i < list->num; i++) {
		uint8_t *value;
		bt_uuid_t uuid;

		value = list->data[i];
		data->descriptors[i].handle = att_get_u16(value);

		if (format == 0x01) {
			data->descriptors[i].uuid16 = *(uint16_t*)&value[2];
			uuid = att_get_uuid16(&value[2]);
		} else {
			uuid = att_get_uuid128(&value[2]);
		}

		bt_uuid_to_uuid(&uuid, &data->descriptors[i].uuid);

		assert(i < data->descriptors_count);
	}

	att_data_list_free(list);

done:
	data->discovered = TRUE;
}
#else
static void char_desc_cb(uint8_t status, GSList *descriptors, void *user_data)
{
	struct descriptor_cb_t* data = user_data;
	GSList *l;
	int i;

	if (status) {
		fprintf(stderr, "Discover all descriptors failed: %s\n", att_ecode2str(status));
		goto done;
	}

	// Allocate array
	data->descriptors_count = g_slist_length(descriptors);
	data->descriptors = malloc(data->descriptors_count * sizeof(gattlib_descriptor_t));
	if (data->descriptors == NULL) {
		fprintf(stderr, "Discover all descriptors failed: OutOfMemory\n");
		goto done;
	}

	for (i = 0, l = descriptors; l; l = l->next, i++) {
		struct gatt_desc *desc = l->data;

		data->descriptors[i].handle = desc->handle;
		data->descriptors[i].uuid16 = desc->uuid16;
		gattlib_string_to_uuid(desc->uuid, MAX_LEN_UUID_STR + 1, &data->descriptors[i].uuid);

		assert(i < data->descriptors_count);
	}

done:
	data->discovered = TRUE;
}
#endif

int gattlib_discover_desc_range(gattlib_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	gattlib_context_t* conn_context = connection->context;
	struct descriptor_cb_t descriptor_data;
	guint ret;

	bzero(&descriptor_data, sizeof(descriptor_data));

#if BLUEZ_VERSION_MAJOR == 4
	ret = gatt_find_info(conn_context->attrib, start, end, char_desc_cb, &descriptor_data);
#else
	ret = gatt_discover_desc(conn_context->attrib, start, end, NULL, char_desc_cb, &descriptor_data);
#endif
	if (ret == 0) {
		fprintf(stderr, "Fail to discover descriptors.\n");
		return GATTLIB_ERROR_BLUEZ_WITH_ERROR(ret);
	}

	// Wait for completion
	while(descriptor_data.discovered == FALSE) {
		g_main_context_iteration(g_gattlib_thread.loop_context, FALSE);
	}

	*descriptors      = descriptor_data.descriptors;
	*descriptor_count = descriptor_data.descriptors_count;

	return GATTLIB_SUCCESS;
}

int gattlib_discover_desc(gattlib_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptor_count) {
	return gattlib_discover_desc_range(connection, 0x0001, 0xffff, descriptors, descriptor_count);
}

/**
 * @brief Function to retrieve Advertisement Data from a MAC Address
 *
 * @param adapter is the adapter the new device has been seen
 * @param mac_address is the MAC address of the device to get the RSSI
 * @param advertisement_data is an array of Service UUID and their respective data
 * @param advertisement_data_count is the number of elements in the advertisement_data array
 * @param manufacturer_data is an array of `gattlib_manufacturer_data_t`
 * @param manufacturer_data_count is the number of entry in `gattlib_manufacturer_data_t` array
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_get_advertisement_data(gattlib_connection_t *connection,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		gattlib_manufacturer_data_t** manufacturer_data, size_t* manufacturer_data_count)
{
	return GATTLIB_NOT_SUPPORTED;
}

/**
 * @brief Function to retrieve Advertisement Data from a MAC Address
 *
 * @param adapter is the adapter the new device has been seen
 * @param mac_address is the MAC address of the device to get the RSSI
 * @param advertisement_data is an array of Service UUID and their respective data
 * @param advertisement_data_count is the number of elements in the advertisement_data array
 * @param manufacturer_data is an array of `gattlib_manufacturer_data_t`
 * @param manufacturer_data_count is the number of entry in `gattlib_manufacturer_data_t` array
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_get_advertisement_data_from_mac(gattlib_adapter_t* adapter, const char *mac_address,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		gattlib_manufacturer_data_t** manufacturer_data, size_t* manufacturer_data_count)
{
	return GATTLIB_NOT_SUPPORTED;
}
