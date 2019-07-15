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
		fprintf(stderr, "Warning: Manufacturer Data not supported: %s\n",
				g_variant_print(manufacturer_data_variant, TRUE));
	}

	service_data_variant = org_bluez_device1_get_service_data(bluez_device1);
	if (service_data_variant != NULL) {
		GVariantIter *iter;
		const gchar *key;
		GVariant *value;
		size_t index = 0;

		*advertisement_data_count = g_variant_n_children(service_data_variant);

		*advertisement_data = calloc(sizeof(gattlib_advertisement_data_t), *advertisement_data_count);
		if (*advertisement_data == NULL) {
			return GATTLIB_OUT_OF_MEMORY;
		}

		g_variant_get(service_data_variant, "a{sv}", &iter);
		while (g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
			gattlib_string_to_uuid(key, strlen(key), &advertisement_data[index]->uuid);

			gsize n_elements = 0;
			gconstpointer const_buffer = g_variant_get_fixed_array(value, &n_elements, sizeof(guchar));
			if (const_buffer) {
				advertisement_data[index]->data = malloc(n_elements);
				if (advertisement_data[index]->data == NULL) {
					return GATTLIB_OUT_OF_MEMORY;
				}

				advertisement_data[index]->data_length = n_elements;
				memcpy(advertisement_data[index]->data, const_buffer, n_elements);
			} else {
				advertisement_data[index]->data_length = 0;
			}

			index++;
		}
	} else {
		*advertisement_data_count = 0;
	}

	return GATTLIB_SUCCESS;
}

int gattlib_get_advertisement_data(gatt_connection_t *connection,
		gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
		uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size)
{
	gattlib_context_t* conn_context = connection->context;

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
		return ret;
	}

	return get_advertisement_data_from_device(bluez_device1,
			advertisement_data, advertisement_data_count,
			manufacturer_id, manufacturer_data, manufacturer_data_size);
}

#endif /* #if BLUEZ_VERSION < BLUEZ_VERSIONS(5, 40) */
