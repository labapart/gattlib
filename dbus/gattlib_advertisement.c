/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2021, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)

int gattlib_get_advertisement_data(gattlib_connection_t *connection,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		gattlib_manufacturer_data_t** manufacturer_data, size_t* manufacturer_data_count)
{
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_get_advertisement_data_from_mac(gattlib_adapter_t* adapter, const char *mac_address,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		gattlib_manufacturer_data_t** manufacturer_data, size_t* manufacturer_data_count)
{
	return GATTLIB_NOT_SUPPORTED;
}

#else

int get_advertisement_data_from_device(OrgBluezDevice1 *bluez_device1,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		gattlib_manufacturer_data_t** manufacturer_data, size_t* manufacturer_data_count)
{
	GVariant *manufacturer_data_variant;
	GVariant *service_data_variant;

	if (advertisement_data == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	*manufacturer_data_count = 0;
	*manufacturer_data = NULL;
	manufacturer_data_variant = org_bluez_device1_get_manufacturer_data(bluez_device1);
	if (manufacturer_data_variant != NULL) {
		*manufacturer_data_count = g_variant_n_children(manufacturer_data_variant);
		*manufacturer_data = malloc(sizeof(gattlib_manufacturer_data_t) * (*manufacturer_data_count));
		if (*manufacturer_data == NULL) {
			return GATTLIB_OUT_OF_MEMORY;
		}

		for (uintptr_t i = 0; i < *manufacturer_data_count; i++) {
			GVariant* manufacturer_data_dict = g_variant_get_child_value(manufacturer_data_variant, i);
			GVariantIter *iter;
			GVariant* values;
			uint16_t manufacturer_id = 0;

			g_variant_get(manufacturer_data_dict, "{qv}", &manufacturer_id, &values);
			(*manufacturer_data)[i].manufacturer_id = manufacturer_id;
			(*manufacturer_data)[i].data_size = g_variant_n_children(values);
			(*manufacturer_data)[i].data = calloc((*manufacturer_data)[i].data_size, sizeof(guchar));
			if ((*manufacturer_data)[i].data == NULL) {
				return GATTLIB_OUT_OF_MEMORY;
			}

			// Copy manufacturer data to structure
			for (unsigned int j = 0; j < (*manufacturer_data)[i].data_size; j++)
			{
				GVariant *v = g_variant_get_child_value(values, j);

				(*manufacturer_data)[i].data[j] = g_variant_get_byte(v);
			}
		}
	}

	service_data_variant = org_bluez_device1_get_service_data(bluez_device1);
	if (service_data_variant != NULL) {
		GVariantIter *iter;
		const gchar *key;
		GVariant *value;
		size_t index = 0;

		*advertisement_data_count = g_variant_n_children(service_data_variant);

		gattlib_advertisement_data_t *advertisement_data_ptr = calloc(sizeof(gattlib_advertisement_data_t), *advertisement_data_count);
		if (advertisement_data_ptr == NULL) {
			return GATTLIB_OUT_OF_MEMORY;
		}

		g_variant_get(service_data_variant, "a{sv}", &iter);
		while (g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
			gattlib_string_to_uuid(key, strlen(key), &advertisement_data_ptr[index].uuid);

			gsize n_elements = 0;
			gconstpointer const_buffer = g_variant_get_fixed_array(value, &n_elements, sizeof(guchar));
			if (const_buffer) {
				advertisement_data_ptr[index].data = malloc(n_elements);
				if (advertisement_data_ptr[index].data == NULL) {
					return GATTLIB_OUT_OF_MEMORY;
				}

				advertisement_data_ptr[index].data_length = n_elements;
				memcpy(advertisement_data_ptr[index].data, const_buffer, n_elements);
			} else {
				advertisement_data_ptr[index].data_length = 0;
			}

			index++;
		}
		g_variant_iter_free(iter);

		*advertisement_data = advertisement_data_ptr;
	} else {
		const gchar* const* service_strs = org_bluez_device1_get_uuids(bluez_device1);
		if (service_strs && (*service_strs)) {
			uuid_t uuid;
			int ret, len = strlen((char *)*service_strs);
			if (!len) goto error_return;

			ret = gattlib_string_to_uuid((char *)*service_strs, len, &uuid);
			if (ret) goto error_return;

			*advertisement_data = calloc(sizeof(gattlib_advertisement_data_t), 1);
			if (!(*advertisement_data)) {
				*advertisement_data_count = 0;
				return GATTLIB_OUT_OF_MEMORY;
			}
			*advertisement_data_count = 1;
			memcpy(&(*advertisement_data)[0].uuid, &uuid, sizeof(uuid));
		}
		else
		{
		error_return:
			*advertisement_data_count = 0;
			*advertisement_data = NULL;
		}
	}

	return GATTLIB_SUCCESS;
}

int gattlib_get_advertisement_data(gattlib_connection_t *connection,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		gattlib_manufacturer_data_t** manufacturer_data, size_t* manufacturer_data_count)
{
	int ret;

	g_rec_mutex_lock(&m_gattlib_mutex);

	if (connection == NULL) {
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_INVALID_PARAMETER;
	}

	if (!gattlib_connection_is_valid(connection)) {
		g_rec_mutex_unlock(&m_gattlib_mutex);
		return GATTLIB_DEVICE_DISCONNECTED;
	}

	// device is actually a GObject. Increasing its reference counter prevents to
	// be freed if the connection is released.
	OrgBluezDevice1* dbus_device = connection->backend.device;
	g_object_ref(dbus_device);
	g_rec_mutex_unlock(&m_gattlib_mutex);

	ret = get_advertisement_data_from_device(dbus_device,
		advertisement_data, advertisement_data_count,
		manufacturer_data, manufacturer_data_count);

	g_object_unref(dbus_device);

	return ret;
}

int gattlib_get_advertisement_data_from_mac(gattlib_adapter_t* adapter, const char *mac_address,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		gattlib_manufacturer_data_t** manufacturer_data, size_t* manufacturer_data_count)
{
	OrgBluezDevice1 *bluez_device1;
	int ret;

	//
	// No need of locking the mutex in this function. It is already done by get_bluez_device_from_mac()
	// and get_advertisement_data_from_device() does not depend on gattlib objects
	//

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		goto EXIT;
	}

	ret = get_advertisement_data_from_device(bluez_device1,
			advertisement_data, advertisement_data_count,
			manufacturer_data, manufacturer_data_count);

	g_object_unref(bluez_device1);

EXIT:
	return ret;
}

#endif /* #if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40) */
