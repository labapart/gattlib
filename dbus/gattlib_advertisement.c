/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2016-2021, Olivier Martin <olivier@labapart.org>
 */

#include "gattlib_internal.h"

#if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40)

int gattlib_get_advertisement_data(gatt_connection_t *connection,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size)
{
	return GATTLIB_NOT_SUPPORTED;
}

int gattlib_get_advertisement_data_from_mac(void *adapter, const char *mac_address,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size)
{
	return GATTLIB_NOT_SUPPORTED;
}

#else

int get_advertisement_data_from_device(OrgBluezDevice1 *bluez_device1,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size)
{
	GVariant *manufacturer_data_variant;
	GVariant *service_data_variant;

	if (advertisement_data == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	*manufacturer_id = 0;
	*manufacturer_data_size = 0;
	manufacturer_data_variant = org_bluez_device1_get_manufacturer_data(bluez_device1);
	if (manufacturer_data_variant != NULL) {
		if (g_variant_n_children(manufacturer_data_variant) != 1) {
			GATTLIB_LOG(GATTLIB_WARNING, "Manufacturer Data with multiple children: %s",
					g_variant_print(manufacturer_data_variant, TRUE));
			return GATTLIB_NOT_SUPPORTED;
		}
		GVariant* manufacturer_data_dict = g_variant_get_child_value(manufacturer_data_variant, 0);
		GVariantIter *iter;
		GVariant* values;

		g_variant_get(manufacturer_data_dict, "{qv}", manufacturer_id, &values);
		*manufacturer_data_size = g_variant_n_children(values);

		*manufacturer_data = calloc(*manufacturer_data_size, sizeof(guchar));
		if (*manufacturer_data == NULL) {
			return GATTLIB_OUT_OF_MEMORY;
		}

		GVariant* value;
		g_variant_get(values, "ay", &iter);
		size_t index = 0;

		while ((value = g_variant_iter_next_value(iter)) != NULL) {
			g_variant_get(value, "y", &(*manufacturer_data)[index++]);
			g_variant_unref(value);
		}
		g_variant_iter_free(iter);
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
		*advertisement_data_count = 0;
		*advertisement_data = NULL;
	}

	return GATTLIB_SUCCESS;
}

int gattlib_get_advertisement_data(gatt_connection_t *connection,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size)
{
	gattlib_context_t* conn_context;

	if (connection == NULL) {
		return GATTLIB_INVALID_PARAMETER;
	}

	conn_context = connection->context;

	return get_advertisement_data_from_device(conn_context->device,
			advertisement_data, advertisement_data_count,
			manufacturer_id, manufacturer_data, manufacturer_data_size);
}

int gattlib_get_advertisement_data_from_mac(void *adapter, const char *mac_address,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size)
{
	OrgBluezDevice1 *bluez_device1;
	int ret;

	ret = get_bluez_device_from_mac(adapter, mac_address, &bluez_device1);
	if (ret != GATTLIB_SUCCESS) {
		g_object_unref(bluez_device1);
		return ret;
	}

	ret = get_advertisement_data_from_device(bluez_device1,
			advertisement_data, advertisement_data_count,
			manufacturer_id, manufacturer_data, manufacturer_data_size);

	g_object_unref(bluez_device1);

	return ret;
}

#endif /* #if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40) */
