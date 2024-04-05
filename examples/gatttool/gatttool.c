/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2010  Nokia Corporation
 *  Copyright (C) 2010  Marcel Holtmann <marcel@holtmann.org>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#if BLUEZ_VERSION_MAJOR == 5
  #include "lib/sdp.h"
#endif
#include "uuid.h"

#include "att.h"
#include "btio.h"
#include "gattrib.h"
#include "gatt.h"
#include "gattlib.h"
#include "gatttool.h"

#include "gattlib_internal_defs.h"

static gchar *opt_src = NULL;
static gchar *opt_dst = NULL;
static gchar *opt_dst_type = NULL;
static gchar *opt_value = NULL;
static gchar *opt_sec_level = NULL;
static bt_uuid_t *opt_uuid = NULL;
static int opt_start = 0x0001;
static int opt_end = 0xffff;
static int opt_handle = -1;
static int opt_mtu = 0;
static int opt_psm = 0;
static int opt_offset = 0;
static gboolean opt_primary = FALSE;
static gboolean opt_characteristics = FALSE;
static gboolean opt_char_read = FALSE;
static gboolean opt_listen = FALSE;
static gboolean opt_char_desc = FALSE;
static gboolean opt_char_write = FALSE;
static gboolean opt_char_write_req = FALSE;
static gboolean opt_interactive = FALSE;
static GMainLoop *event_loop;
static gboolean got_error = FALSE;
static GSourceFunc operation;

struct characteristic_data {
	GAttrib *attrib;
	uint16_t start;
	uint16_t end;
};

void notification_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	char uuid_str[MAX_LEN_UUID_STR + 1];
	int i;

	gattlib_uuid_to_string(uuid, uuid_str, MAX_LEN_UUID_STR + 1);
	g_print("Notification uuid = %s value: ", uuid_str);

	for (i = 0; i < data_length; i++)
		g_print("%02x ", data[i]);

	g_print("\n");
	rl_forced_update_display();
}

void indication_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	char uuid_str[MAX_LEN_UUID_STR + 1];
	int i;

	gattlib_uuid_to_string(uuid, uuid_str, MAX_LEN_UUID_STR + 1);
	g_print("Indication   uuid = %s value: ", uuid_str);

	for (i = 0; i < data_length; i++)
		g_print("%02x ", data[i]);

	g_print("\n");
	rl_forced_update_display();
}

static void connect_cb(gattlib_connection_t* connection, void* user_data)
{
	if (connection == NULL) {
		got_error = TRUE;
		g_main_loop_quit(event_loop);
	} else {
		if (opt_listen) {
			gattlib_register_notification(connection, notification_handler, NULL);
			gattlib_register_indication(connection, indication_handler, NULL);
		}

		operation(connection);
	}
}

#if BLUEZ_VERSION_MAJOR == 4
static void primary_by_uuid_cb(GSList *ranges, guint8 status,
							gpointer user_data)
#else
static void primary_by_uuid_cb(uint8_t status, GSList *ranges, void *user_data)
#endif
{
	GSList *l;

	if (status != 0) {
		g_printerr("Discover primary services by UUID failed: %s\n",
							att_ecode2str(status));
		goto done;
	}

	for (l = ranges; l; l = l->next) {
		struct att_range *range = l->data;
		g_print("Starting handle: %04x Ending handle: %04x\n",
						range->start, range->end);
	}

done:
	g_main_loop_quit(event_loop);
}

static gboolean primary(gpointer user_data)
{
	gattlib_device_t* connection = (gattlib_device_t* )user_data;
	GAttrib *attrib = connection->backend.attrib;
	char uuid_str[MAX_LEN_UUID_STR + 1];

	if (opt_uuid)
		gatt_discover_primary(attrib, opt_uuid, primary_by_uuid_cb,
									NULL);
	else {
		gattlib_primary_service_t* services;
		int services_count, i;

		int ret = gattlib_discover_primary(connection, &services, &services_count);
		if (ret == GATTLIB_SUCCESS) {
			for (i = 0; i < services_count; i++) {
				gattlib_uuid_to_string(&services[i].uuid, uuid_str, sizeof(uuid_str));

				g_print("attr handle = 0x%04x, end grp handle = 0x%04x uuid: %s\n",
						services[i].attr_handle_start, services[i].attr_handle_end, uuid_str);
			}
		}
	}

	return FALSE;
}

static gboolean characteristics(gpointer user_data)
{
	gattlib_connection_t* connection = (gattlib_connection_t*)user_data;
	gattlib_characteristic_t* characteristics;
	int characteristic_count, i;

	int ret = gattlib_discover_char(connection, &characteristics, &characteristic_count);
	if (ret) {
		return FALSE;
	} else {
		char uuid_str[MAX_LEN_UUID_STR + 1];

		for (i = 0; i < characteristic_count; i++) {
			gattlib_uuid_to_string(&characteristics[i].uuid, uuid_str, sizeof(uuid_str));

			g_print("handle = 0x%04x, char properties = 0x%02x, char value "
				"handle = 0x%04x, uuid = %s\n",
				characteristics[i].handle, characteristics[i].properties, characteristics[i].value_handle, uuid_str);
		}
		free(characteristics);
		return TRUE;
	}
}

static void char_read_cb(guint8 status, const guint8 *pdu, guint16 plen,
							gpointer user_data)
{
	uint8_t value[ATT_MAX_MTU];
	int i, vlen;

	if (status != 0) {
		g_printerr("Characteristic value/descriptor read failed: %s\n",
							att_ecode2str(status));
		goto done;
	}
#if BLUEZ_VERSION_MAJOR == 4
	vlen = dec_read_resp(pdu, plen, value, &vlen);
#else
	vlen = sizeof(value);
	vlen = dec_read_resp(pdu, plen, value, vlen);
#endif
	if (vlen <= 0) {
		g_printerr("Protocol error\n");
		goto done;
	}
	g_print("Characteristic value/descriptor: ");
	for (i = 0; i < vlen; i++)
		g_print("%02x ", value[i]);
	g_print("\n");

done:
	if (opt_listen == FALSE)
		g_main_loop_quit(event_loop);
}

static void bt_uuid_to_uuid(bt_uuid_t* bt_uuid, uuid_t* uuid) {
	memcpy(&uuid->value, &bt_uuid->value, sizeof(uuid->value));
	if (bt_uuid->type == BT_UUID16) {
		uuid->type = SDP_UUID16;
	} else if (bt_uuid->type == BT_UUID32) {
		uuid->type = SDP_UUID32;
	} else if (bt_uuid->type == BT_UUID128) {
		uuid->type = SDP_UUID128;
	} else {
		uuid->type = SDP_UUID_UNSPEC;
	}
}

static gboolean characteristics_read(gpointer user_data)
{
	gattlib_connection_t* connection = (gattlib_connection_t*)user_data;
	gattlib_context_t* conn_context = connection->context;
	GAttrib *attrib = conn_context->attrib;

	if (opt_uuid != NULL) {
		uint8_t *buffer;
		uuid_t uuid;
		size_t buffer_len;
		int i, ret;

		bt_uuid_to_uuid(opt_uuid, &uuid);

		ret = gattlib_read_char_by_uuid(connection, &uuid, (void **)&buffer, &buffer_len);
		if (ret) {
			return FALSE;
		} else {
			g_print("value: ");
			for (i = 0; i < buffer_len; i++) {
				g_print("%02x ", buffer[i]);
			}
			g_print("\n");

			gattlib_characteristic_free_value(buffer);
			return TRUE;
		}
	}

	if (opt_handle <= 0) {
		g_printerr("A valid handle is required\n");
		g_main_loop_quit(event_loop);
		return FALSE;
	}

	gatt_read_char(attrib, opt_handle,
#if BLUEZ_VERSION_MAJOR == 4
			opt_offset,
#endif
			char_read_cb, attrib);

	return FALSE;
}

static void mainloop_quit(gpointer user_data)
{
	uint8_t *value = user_data;

	g_free(value);
	g_main_loop_quit(event_loop);
}

static gboolean characteristics_write(gpointer user_data)
{
	gattlib_context_t* conn_context = ((gattlib_connection_t*)user_data)->context;
	GAttrib *attrib = conn_context->attrib;
	uint8_t *value;
	size_t len;

	if (opt_handle <= 0) {
		g_printerr("A valid handle is required\n");
		goto error;
	}

	if (opt_value == NULL || opt_value[0] == '\0') {
		g_printerr("A value is required\n");
		goto error;
	}

	len = gatt_attr_data_from_string(opt_value, &value);
	if (len == 0) {
		g_printerr("Invalid value\n");
		goto error;
	}

	gatt_write_cmd(attrib, opt_handle, value, len, mainloop_quit, value);

	g_free(value);
	return FALSE;

error:
	g_main_loop_quit(event_loop);
	return FALSE;
}

static void char_write_req_cb(guint8 status, const guint8 *pdu, guint16 plen,
							gpointer user_data)
{
	if (status != 0) {
		g_printerr("Characteristic Write Request failed: "
						"%s\n", att_ecode2str(status));
		goto done;
	}

#if BLUEZ_VERSION_MAJOR == 4
	if (!dec_write_resp(pdu, plen)) {
#else
	if (!dec_write_resp(pdu, plen) && !dec_exec_write_resp(pdu, plen)) {
#endif
		g_printerr("Protocol error\n");
		goto done;
	}

	g_print("Characteristic value was written successfully\n");

done:
	if (opt_listen == FALSE)
		g_main_loop_quit(event_loop);
}

static gboolean characteristics_write_req(gpointer user_data)
{
	gattlib_context_t* conn_context = ((gattlib_connection_t*)user_data)->context;
	GAttrib *attrib = conn_context->attrib;
	uint8_t *value;
	size_t len;

	if (opt_handle <= 0) {
		g_printerr("A valid handle is required\n");
		goto error;
	}

	if (opt_value == NULL || opt_value[0] == '\0') {
		g_printerr("A value is required\n");
		goto error;
	}

	len = gatt_attr_data_from_string(opt_value, &value);
	if (len == 0) {
		g_printerr("Invalid value\n");
		goto error;
	}

	gatt_write_char(attrib, opt_handle, value, len, char_write_req_cb,
									NULL);

	g_free(value);
	return FALSE;

error:
	g_main_loop_quit(event_loop);
	return FALSE;
}

static gboolean characteristics_desc(gpointer user_data)
{
	gattlib_connection_t* connection = (gattlib_connection_t*)user_data;
	gattlib_descriptor_t* descriptors;
	int descriptor_count, i;

	int ret = gattlib_discover_desc(connection, &descriptors, &descriptor_count);
	if (ret) {
		return FALSE;
	} else {
		for (i = 0; i < descriptor_count; i++) {
			char uuid_str[MAX_LEN_UUID_STR + 1];

			gattlib_uuid_to_string(&descriptors[i].uuid, uuid_str, MAX_LEN_UUID_STR + 1);
			g_print("handle = 0x%04x, uuid = %s\n", descriptors[i].handle, uuid_str);
		}
		free(descriptors);
		return TRUE;
	}
}

static gboolean parse_uuid(const char *key, const char *value,
				gpointer user_data, GError **error)
{
	if (!value)
		return FALSE;

	opt_uuid = g_try_malloc(sizeof(bt_uuid_t));
	if (opt_uuid == NULL)
		return FALSE;

	if (bt_string_to_uuid(opt_uuid, value) < 0)
		return FALSE;

	return TRUE;
}

static GOptionEntry primary_char_options[] = {
	{ "start", 's' , 0, G_OPTION_ARG_INT, &opt_start,
		"Starting handle(optional)", "0x0001" },
	{ "end", 'e' , 0, G_OPTION_ARG_INT, &opt_end,
		"Ending handle(optional)", "0xffff" },
	{ "uuid", 'u', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK,
		parse_uuid, "UUID16 or UUID128(optional)", "0x1801"},
	{ NULL },
};

static GOptionEntry char_rw_options[] = {
	{ "handle", 'a' , 0, G_OPTION_ARG_INT, &opt_handle,
		"Read/Write characteristic by handle(required)", "0x0001" },
	{ "value", 'n' , 0, G_OPTION_ARG_STRING, &opt_value,
		"Write characteristic value (required for write operation)",
		"0x0001" },
	{ "offset", 'o', 0, G_OPTION_ARG_INT, &opt_offset,
		"Offset to long read characteristic by handle", "N"},
	{NULL},
};

static GOptionEntry gatt_options[] = {
	{ "primary", 0, 0, G_OPTION_ARG_NONE, &opt_primary,
		"Primary Service Discovery", NULL },
	{ "characteristics", 0, 0, G_OPTION_ARG_NONE, &opt_characteristics,
		"Characteristics Discovery", NULL },
	{ "char-read", 0, 0, G_OPTION_ARG_NONE, &opt_char_read,
		"Characteristics Value/Descriptor Read", NULL },
	{ "char-write", 0, 0, G_OPTION_ARG_NONE, &opt_char_write,
		"Characteristics Value Write Without Response (Write Command)",
		NULL },
	{ "char-write-req", 0, 0, G_OPTION_ARG_NONE, &opt_char_write_req,
		"Characteristics Value Write (Write Request)", NULL },
	{ "char-desc", 0, 0, G_OPTION_ARG_NONE, &opt_char_desc,
		"Characteristics Descriptor Discovery", NULL },
	{ "listen", 0, 0, G_OPTION_ARG_NONE, &opt_listen,
		"Listen for notifications and indications", NULL },
	{ "interactive", 'I', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE,
		&opt_interactive, "Use interactive mode", NULL },
	{ NULL },
};

static GOptionEntry options[] = {
	{ "adapter", 'i', 0, G_OPTION_ARG_STRING, &opt_src,
		"Specify local adapter interface", "hciX" },
	{ "device", 'b', 0, G_OPTION_ARG_STRING, &opt_dst,
		"Specify remote Bluetooth address", "MAC" },
	{ "addr-type", 't', 0, G_OPTION_ARG_STRING, &opt_dst_type,
		"Set LE address type. Default: public", "[public | random]"},
	{ "mtu", 'm', 0, G_OPTION_ARG_INT, &opt_mtu,
		"Specify the MTU size", "MTU" },
	{ "psm", 'p', 0, G_OPTION_ARG_INT, &opt_psm,
		"Specify the PSM for GATT/ATT over BR/EDR", "PSM" },
	{ "sec-level", 'l', 0, G_OPTION_ARG_STRING, &opt_sec_level,
		"Set security level. Default: low", "[low | medium | high]"},
	{ NULL },
};

int main(int argc, char *argv[])
{
	GOptionContext *context;
	GOptionGroup *gatt_group, *params_group, *char_rw_group;
	GError *gerr = NULL;
	gattlib_connection_t *connection;
	unsigned long conn_options = 0;
	BtIOSecLevel sec_level;
	uint8_t dest_type;

	opt_dst_type = g_strdup("public");
	opt_sec_level = g_strdup("low");

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, options, NULL);

	/* GATT commands */
	gatt_group = g_option_group_new("gatt", "GATT commands",
					"Show all GATT commands", NULL, NULL);
	g_option_context_add_group(context, gatt_group);
	g_option_group_add_entries(gatt_group, gatt_options);

	/* Primary Services and Characteristics arguments */
	params_group = g_option_group_new("params",
			"Primary Services/Characteristics arguments",
			"Show all Primary Services/Characteristics arguments",
			NULL, NULL);
	g_option_context_add_group(context, params_group);
	g_option_group_add_entries(params_group, primary_char_options);

	/* Characteristics value/descriptor read/write arguments */
	char_rw_group = g_option_group_new("char-read-write",
		"Characteristics Value/Descriptor Read/Write arguments",
		"Show all Characteristics Value/Descriptor Read/Write "
		"arguments",
		NULL, NULL);
	g_option_context_add_group(context, char_rw_group);
	g_option_group_add_entries(char_rw_group, char_rw_options);

	if (g_option_context_parse(context, &argc, &argv, &gerr) == FALSE) {
		g_printerr("%s\n", gerr->message);
		g_error_free(gerr);
	}

	if (opt_interactive) {
		interactive(opt_src, opt_dst, opt_dst_type, opt_psm);
		goto done;
	}

	if (opt_primary)
		operation = primary;
	else if (opt_characteristics)
		operation = characteristics;
	else if (opt_char_read)
		operation = characteristics_read;
	else if (opt_char_write)
		operation = characteristics_write;
	else if (opt_char_write_req)
		operation = characteristics_write_req;
	else if (opt_char_desc)
		operation = characteristics_desc;
	else {
		gchar *help = g_option_context_get_help(context, TRUE, NULL);
		g_print("%s\n", help);
		g_free(help);
		got_error = TRUE;
		goto done;
	}

	dest_type = get_dest_type_from_str(opt_dst_type);
	if (dest_type == BDADDR_LE_PUBLIC) {
		conn_options |= GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC;
	} else if (dest_type == BDADDR_LE_RANDOM) {
		conn_options |= GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM;
	}

	sec_level = get_sec_level_from_str(opt_sec_level);
	if (sec_level == BT_IO_SEC_LOW) {
		conn_options |= GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW;
	} else if (sec_level == BT_IO_SEC_MEDIUM) {
		conn_options |= GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_MEDIUM;
	} else if (sec_level == BT_IO_SEC_HIGH) {
		conn_options |= GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_HIGH;
	}

	conn_options |= GATTLIB_CONNECTION_OPTIONS_LEGACY_PSM(opt_psm);
	conn_options |= GATTLIB_CONNECTION_OPTIONS_LEGACY_PSM(opt_mtu);

	connection = gattlib_connect_async(opt_src, opt_dst, conn_options, connect_cb, NULL);
	if (connection == NULL) {
		got_error = TRUE;
		goto done;
	}

	event_loop = g_main_loop_new(NULL, FALSE);

	g_main_loop_run(event_loop);

	g_main_loop_unref(event_loop);

done:
	g_option_context_free(context);
	g_free(opt_src);
	g_free(opt_dst);
	g_free(opt_uuid);
	g_free(opt_sec_level);

	if (got_error)
		exit(EXIT_FAILURE);
	else
		exit(EXIT_SUCCESS);
}
