/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016  Olivier Martin <olivier@labapart.org>
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

#include "gattlib_internal.h"

#include "att.h"
#include "gattrib.h"
#include "gatt.h"

struct primary_all_cb_t {
	gattlib_primary_service_t* services;
	int services_count;
	int discovered;
};

static void primary_all_cb(GSList *services, guint8 status, gpointer user_data) {
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

	for (i = 0, l = services; l; l = l->next, i++) {
		struct gatt_primary *prim = l->data;

		data->services[i].attr_handle_start = prim->range.start;
		data->services[i].attr_handle_end   = prim->range.end;
		bt_string_to_uuid(&data->services[i].uuid, prim->uuid);

		assert(i < data->services_count);
	}

done:
	data->discovered = TRUE;
}

int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count) {
	struct primary_all_cb_t user_data;
	guint ret;

	bzero(&user_data, sizeof(user_data));
	user_data.discovered     = FALSE;

	ret = gatt_discover_primary(connection->attrib, NULL, primary_all_cb, &user_data);
	if (ret == 0) {
		fprintf(stderr, "Fail to discover primary services.\n");
		return 1;
	}

	// Wait for completion
	while(user_data.discovered == FALSE) {
		g_main_context_iteration(g_gattlib_thread.loop_context, FALSE);
	}

	*services       = user_data.services;
	*services_count = user_data.services_count;

	return 0;
}

struct characteristic_cb_t {
	gattlib_characteristic_t* characteristics;
	int characteristics_count;
	int discovered;
};

static void characteristic_cb(GSList *characteristics, guint8 status, gpointer user_data) {
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

	for (i = 0, l = characteristics; l; l = l->next, i++) {
		struct gatt_char *chars = l->data;

		data->characteristics[i].properties = chars->properties;
		data->characteristics[i].value_handle   = chars->value_handle;
		bt_string_to_uuid(&data->characteristics[i].uuid, chars->uuid);

		assert(i < data->characteristics_count);
	}

done:
	data->discovered = TRUE;
}

int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristics_count) {
	struct characteristic_cb_t user_data;
	const int start = 0x0001;
	const int end   = 0xffff;
	guint ret;

	bzero(&user_data, sizeof(user_data));
	user_data.discovered     = FALSE;

	ret = gatt_discover_char(connection->attrib, start, end, NULL, characteristic_cb, &user_data);
	if (ret == 0) {
		fprintf(stderr, "Fail to discover characteristics.\n");
		return 1;
	}

	// Wait for completion
	while(user_data.discovered == FALSE) {
		g_main_context_iteration(g_gattlib_thread.loop_context, FALSE);
	}

	*characteristics       = user_data.characteristics;
	*characteristics_count = user_data.characteristics_count;

	return 0;
}
