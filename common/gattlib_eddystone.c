/*
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
 *
 * Copyright (c) 2021, Olivier Martin <olivier@labapart.org>
 */

#include <string.h>

#include "gattlib_internal.h"

#define EDDYSTONE_SERVICE_UUID	"0000FEAA-0000-1000-8000-00805F9B34FB"

const uuid_t gattlib_eddystone_common_data_uuid = CREATE_UUID16(0xFEAA);

const char *gattlib_eddystone_url_scheme_prefix[] = {
    "http://www.",
    "https://www.",
    "http://",
    "https://"
};


struct on_eddystone_discovered_device_arg {
	gattlib_discovered_device_with_data_t discovered_device_cb;
	void *user_data;
};

static void on_eddystone_discovered_device(gattlib_adapter_t* adapter, const char* addr, const char* name, void *user_data)
{
	struct on_eddystone_discovered_device_arg *callback_data = user_data;
	gattlib_advertisement_data_t *advertisement_data = NULL;
	size_t advertisement_data_count;
	gattlib_manufacturer_data_t* manufacturer_data = NULL;
	size_t manufacturer_data_count = 0;
	int ret;

	ret = gattlib_get_advertisement_data_from_mac(adapter, addr,
			&advertisement_data, &advertisement_data_count,
			&manufacturer_data, &manufacturer_data_count);
	if (ret != 0) {
		return;
	}

	callback_data->discovered_device_cb(adapter, addr, name,
			advertisement_data, advertisement_data_count,
			manufacturer_data, manufacturer_data_count,
			callback_data->user_data);

	if (advertisement_data != NULL) {
		free(advertisement_data);
	}
	if (manufacturer_data != NULL) {
		for (uintptr_t i = 0; i < manufacturer_data_count; i++) {
			free(manufacturer_data[i].data);
		}
		free(manufacturer_data);
	}
}

int gattlib_adapter_scan_eddystone(gattlib_adapter_t* adapter, int16_t rssi_threshold, uint32_t eddystone_types,
		gattlib_discovered_device_with_data_t discovered_device_cb, size_t timeout, void *user_data)
{
	uuid_t eddystone_uuid;
	uint32_t enabled_filters = GATTLIB_DISCOVER_FILTER_USE_UUID;
	int ret;

	ret = gattlib_string_to_uuid(EDDYSTONE_SERVICE_UUID, strlen(EDDYSTONE_SERVICE_UUID) + 1, &eddystone_uuid);
	if (ret != 0) {
		GATTLIB_LOG(GATTLIB_ERROR, "Fail to convert characteristic TX to UUID.");
		return GATTLIB_ERROR_INTERNAL;
	}

	uuid_t *uuid_filter_list[] = { &eddystone_uuid, NULL };

	if (eddystone_types & GATTLIB_EDDYSTONE_LIMIT_RSSI) {
		enabled_filters |= GATTLIB_DISCOVER_FILTER_USE_RSSI;
	}

	struct on_eddystone_discovered_device_arg callback_data = {
			.discovered_device_cb = discovered_device_cb,
			.user_data = user_data
	};

	return gattlib_adapter_scan_enable_with_filter(adapter, uuid_filter_list, rssi_threshold, enabled_filters,
			on_eddystone_discovered_device, timeout, &callback_data);
}
