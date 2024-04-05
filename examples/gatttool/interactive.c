/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2011  Nokia Corporation
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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <glib.h>

#if BLUEZ_VERSION_MAJOR == 5
#include <bluetooth/bluetooth.h>
#include "lib/sdp.h"
#include "src/shared/util.h"
#endif
#include "uuid.h"

#include <readline/readline.h>
#include <readline/history.h>

#include "att.h"
#include "btio.h"
#include "gattrib.h"
#include "gatt.h"
#include "gattlib.h"
#include "gatttool.h"

#include "gattlib_internal_defs.h"

static gattlib_connection_t* g_connection = NULL;
static GMainLoop *event_loop;
static GString *prompt;

static gchar *opt_src = NULL;
static gchar *opt_dst = NULL;
static gchar *opt_dst_type = NULL;
static gchar *opt_sec_level = NULL;
static int opt_psm = 0;
static int opt_mtu = 0;

struct characteristic_data {
	uint16_t orig_start;
	uint16_t start;
	uint16_t end;
	bt_uuid_t uuid;
};

static void cmd_help(int argcp, char **argvp);

enum state {
	STATE_DISCONNECTED,
	STATE_CONNECTING,
	STATE_CONNECTED
} conn_state;

#define error(fmt, arg...) \
	printf(COLOR_RED "Error: " COLOR_OFF fmt, ## arg)

#define failed(fmt, arg...) \
	printf(COLOR_RED "Command Failed: " COLOR_OFF fmt, ## arg)

static char *get_prompt(void)
{
	if (conn_state == STATE_CONNECTING) {
		g_string_assign(prompt, "Connecting... ");
		return prompt->str;
	}

	if (conn_state == STATE_CONNECTED)
		g_string_assign(prompt, "[CON]");
	else
		g_string_assign(prompt, "[   ]");

	if (opt_dst)
		g_string_append_printf(prompt, "[%17s]", opt_dst);
	else
		g_string_append_printf(prompt, "[%17s]", "");

	if (opt_psm)
		g_string_append(prompt, "[BR]");
	else
		g_string_append(prompt, "[LE]");

	g_string_append(prompt, "> ");

	return prompt->str;
}


static void set_state(enum state st)
{
	conn_state = st;
	rl_set_prompt(get_prompt());
	rl_redisplay();
}

static void connect_cb(gattlib_connection_t* connection, void* user_data)
{
	if (connection == NULL) {
		set_state(STATE_DISCONNECTED);
	} else {
		g_connection = connection;
		gattlib_register_notification(connection, notification_handler, NULL);
		gattlib_register_indication(connection, indication_handler, NULL);
		set_state(STATE_CONNECTED);
	}
}

static void disconnect_io()
{
	if (conn_state == STATE_DISCONNECTED)
		return;

	gattlib_disconnect(g_connection, false /* wait_disconnection */);
	opt_mtu = 0;

	set_state(STATE_DISCONNECTED);
}

#if BLUEZ_VERSION_MAJOR == 4
static void primary_by_uuid_cb(GSList *ranges, guint8 status,
							gpointer user_data)
#else
static void primary_by_uuid_cb(uint8_t status, GSList *ranges, void *user_data)
#endif
{
	GSList *l;

	if (status) {
		printf("Discover primary services by UUID failed: %s\n",
							att_ecode2str(status));
		return;
	}

	printf("\n");
	for (l = ranges; l; l = l->next) {
		struct att_range *range = l->data;
		g_print("Starting handle: 0x%04x Ending handle: 0x%04x\n",
						range->start, range->end);
	}

	rl_forced_update_display();
}

#if BLUEZ_VERSION_MAJOR == 4
static void char_cb(GSList *characteristics, guint8 status, gpointer user_data)
#else
static void char_cb(uint8_t status, GSList *characteristics, void *user_data)
#endif
{
	GSList *l;

	if (status) {
		printf("Discover all characteristics failed: %s\n",
							att_ecode2str(status));
		return;
	}

	printf("\n");
	for (l = characteristics; l; l = l->next) {
		struct gatt_char *chars = l->data;

		printf("handle: 0x%04x, char properties: 0x%02x, char value "
				"handle: 0x%04x, uuid: %s\n", chars->handle,
				chars->properties, chars->value_handle,
				chars->uuid);
	}

	rl_forced_update_display();
}

static void char_read_cb(guint8 status, const guint8 *pdu, guint16 plen,
							gpointer user_data)
{
	uint8_t value[ATT_MAX_MTU];
	int i, vlen;

	if (status != 0) {
		printf("Characteristic value/descriptor read failed: %s\n",
							att_ecode2str(status));
		return;
	}

#if BLUEZ_VERSION_MAJOR == 4
	vlen = dec_read_resp(pdu, plen, value, &vlen);
#else
	vlen = sizeof(value);
	vlen = dec_read_resp(pdu, plen, value, vlen);
#endif
	if (vlen <= 0) {
		printf("Protocol error\n");
		return;
	}

	printf("\nCharacteristic value/descriptor: ");
	for (i = 0; i < vlen; i++)
		printf("%02x ", value[i]);
	printf("\n");

	rl_forced_update_display();
}

static void char_read_by_uuid_cb(guint8 status, const guint8 *pdu,
					guint16 plen, gpointer user_data)
{
	struct characteristic_data *char_data = user_data;
	struct att_data_list *list;
	int i;

	if (status == ATT_ECODE_ATTR_NOT_FOUND &&
				char_data->start != char_data->orig_start)
		goto done;

	if (status != 0) {
		printf("Read characteristics by UUID failed: %s\n",
							att_ecode2str(status));
		goto done;
	}

	list = dec_read_by_type_resp(pdu, plen);
	if (list == NULL)
		goto done;

	for (i = 0; i < list->num; i++) {
		uint8_t *value = list->data[i];
		int j;

#if BLUEZ_VERSION_MAJOR == 4
		printf("\nhandle: 0x%04x \t value: ", att_get_u16(value));
#else
		printf("\nhandle: 0x%04x \t value: ", get_le16(value));
#endif
		value += 2;
		for (j = 0; j < list->len - 2; j++, value++)
			printf("%02x ", *value);
		printf("\n");
	}

	att_data_list_free(list);

	rl_forced_update_display();

done:
	g_free(char_data);
}

static void cmd_exit(int argcp, char **argvp)
{
	rl_callback_handler_remove();
	g_main_loop_quit(event_loop);
}

static gboolean channel_watcher(GIOChannel *chan, GIOCondition cond,
				gpointer user_data)
{
	disconnect_io();

	return FALSE;
}

static void cmd_connect(int argcp, char **argvp)
{
	gattlib_connection_t *connection;
	unsigned long conn_options = 0;
	BtIOSecLevel sec_level;
	uint8_t dst_type;

	if (conn_state != STATE_DISCONNECTED)
		return;

	if (argcp > 1) {
		g_free(opt_dst);
		opt_dst = g_strdup(argvp[1]);

		g_free(opt_dst_type);
		if (argcp > 2)
			opt_dst_type = g_strdup(argvp[2]);
		else
			opt_dst_type = g_strdup("public");
	}

	if (opt_dst == NULL) {
		printf("Remote Bluetooth address required\n");
		return;
	}

	set_state(STATE_CONNECTING);

	dst_type = get_dest_type_from_str(opt_dst_type);
	if (dst_type == BDADDR_LE_PUBLIC) {
		conn_options |= GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC;
	} else if (dst_type == BDADDR_LE_RANDOM) {
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
		set_state(STATE_DISCONNECTED);
	} else {
		gattlib_device_t* gatt_connection = (gattlib_device_t* )g_connection;
		g_io_add_watch(gatt_connection->backend.io, G_IO_HUP, channel_watcher, NULL);
	}
}

static void cmd_disconnect(int argcp, char **argvp)
{
	disconnect_io();
}

static void cmd_primary(int argcp, char **argvp)
{
	gattlib_context_t* conn_context = g_connection->context;
	bt_uuid_t uuid;

	if (conn_state != STATE_CONNECTED) {
		printf("Command failed: disconnected\n");
		return;
	}

	if (argcp == 1) {
		char uuid_str[MAX_LEN_UUID_STR + 1];
		gattlib_primary_service_t* services;
		int services_count, ret, i;

		ret = gattlib_discover_primary(g_connection, &services, &services_count);
		if (ret == GATTLIB_SUCCESS) {
			for (i = 0; i < services_count; i++) {
				gattlib_uuid_to_string(&services[i].uuid, uuid_str, sizeof(uuid_str));

				printf("attr handle: 0x%04x, end grp handle: 0x%04x uuid: %s\n",
						services[i].attr_handle_start, services[i].attr_handle_end, uuid_str);
			}

			rl_forced_update_display();
		}
		return;
	}

	if (bt_string_to_uuid(&uuid, argvp[1]) < 0) {
		printf("Invalid UUID\n");
		return;
	}

	gatt_discover_primary(conn_context->attrib, &uuid, primary_by_uuid_cb, NULL);
}

static int strtohandle(const char *src)
{
	char *e;
	int dst;

	errno = 0;
	dst = strtoll(src, &e, 16);
	if (errno != 0 || *e != '\0')
		return -EINVAL;

	return dst;
}

static void cmd_char(int argcp, char **argvp)
{
	gattlib_context_t* conn_context = g_connection->context;
	char uuid_str[MAX_LEN_UUID_STR + 1];
	gattlib_characteristic_t* characteristics;
	int characteristics_count, ret, i;
	int start = 0x0001;
	int end = 0xffff;

	if (conn_state != STATE_CONNECTED) {
		printf("Command failed: disconnected\n");
		return;
	}

	if (argcp > 1) {
		start = strtohandle(argvp[1]);
		if (start < 0) {
			printf("Invalid start handle: %s\n", argvp[1]);
			return;
		}
	}

	if (argcp > 2) {
		end = strtohandle(argvp[2]);
		if (end < 0) {
			printf("Invalid end handle: %s\n", argvp[2]);
			return;
		}
	}

	if (argcp > 3) {
		bt_uuid_t uuid;

		if (bt_string_to_uuid(&uuid, argvp[3]) < 0) {
			printf("Invalid UUID\n");
			return;
		}

		gatt_discover_char(conn_context->attrib, start, end, &uuid, char_cb, NULL);
		return;
	}

	ret = gattlib_discover_char_range(g_connection, start, end, &characteristics, &characteristics_count);
	if (ret == GATTLIB_SUCCESS) {
		for (i = 0; i < characteristics_count; i++) {
			gattlib_uuid_to_string(&characteristics[i].uuid, uuid_str, sizeof(uuid_str));

			printf("handle: 0x%04x, char properties: 0x%02x, char value "
					"handle: 0x%04x, uuid: %s\n", characteristics[i].handle,
					characteristics[i].properties, characteristics[i].value_handle,
					uuid_str);
		}
		free(characteristics);
	}
}

static void cmd_char_desc(int argcp, char **argvp)
{
	gattlib_descriptor_t* descriptors;
	int descriptor_count, ret, i;
	int start = 0x0001;
	int end = 0xffff;

	if (conn_state != STATE_CONNECTED) {
		printf("Command failed: disconnected\n");
		return;
	}

	if (argcp > 1) {
		start = strtohandle(argvp[1]);
		if (start < 0) {
			printf("Invalid start handle: %s\n", argvp[1]);
			return;
		}
	}

	if (argcp > 2) {
		end = strtohandle(argvp[2]);
		if (end < 0) {
			printf("Invalid end handle: %s\n", argvp[2]);
			return;
		}
	}

	ret = gattlib_discover_desc_range(g_connection, start, end, &descriptors, &descriptor_count);
	if (ret == GATTLIB_SUCCESS) {
		for (i = 0; i < descriptor_count; i++) {
			char uuid_str[MAX_LEN_UUID_STR + 1];

			gattlib_uuid_to_string(&descriptors[i].uuid, uuid_str, MAX_LEN_UUID_STR + 1);
			printf("handle: 0x%04x, uuid: %s\n", descriptors[i].handle, uuid_str);
		}
		free(descriptors);
	}
}

static void cmd_read_hnd(int argcp, char **argvp)
{
	gattlib_context_t* conn_context = g_connection->context;
	int handle;

	if (conn_state != STATE_CONNECTED) {
		printf("Command failed: disconnected\n");
		return;
	}

	if (argcp < 2) {
		printf("Missing argument: handle\n");
		return;
	}

	handle = strtohandle(argvp[1]);
	if (handle < 0) {
		printf("Invalid handle: %s\n", argvp[1]);
		return;
	}

#if BLUEZ_VERSION_MAJOR == 4
	int offset = 0;

	if (argcp > 2) {
		char *e;

		errno = 0;
		offset = strtol(argvp[2], &e, 0);
		if (errno != 0 || *e != '\0') {
			printf("Invalid offset: %s\n", argvp[2]);
			return;
		}
	}
#endif

	gatt_read_char(conn_context->attrib, handle,
#if BLUEZ_VERSION_MAJOR == 4
			offset,
#endif
			char_read_cb, conn_context->attrib);
}

static void cmd_read_uuid(int argcp, char **argvp)
{
	gattlib_context_t* conn_context = g_connection->context;
	struct characteristic_data *char_data;
	int start = 0x0001;
	int end = 0xffff;
	bt_uuid_t uuid;

	if (conn_state != STATE_CONNECTED) {
		printf("Command failed: disconnected\n");
		return;
	}

	if (argcp < 2) {
		printf("Missing argument: UUID\n");
		return;
	}

	if (bt_string_to_uuid(&uuid, argvp[1]) < 0) {
		printf("Invalid UUID\n");
		return;
	}

	if (argcp > 2) {
		start = strtohandle(argvp[2]);
		if (start < 0) {
			printf("Invalid start handle: %s\n", argvp[1]);
			return;
		}
	}

	if (argcp > 3) {
		end = strtohandle(argvp[3]);
		if (end < 0) {
			printf("Invalid end handle: %s\n", argvp[2]);
			return;
		}
	}

	char_data = g_new(struct characteristic_data, 1);
	char_data->orig_start = start;
	char_data->start = start;
	char_data->end = end;
	char_data->uuid = uuid;

	gatt_read_char_by_uuid(conn_context->attrib, start, end, &char_data->uuid,
					char_read_by_uuid_cb, char_data);
}

static void char_write_req_cb(guint8 status, const guint8 *pdu, guint16 plen,
							gpointer user_data)
{
	if (status != 0) {
		printf("Characteristic Write Request failed: "
						"%s\n", att_ecode2str(status));
		return;
	}

	if (!dec_write_resp(pdu, plen)) {
		printf("Protocol error\n");
		return;
	}

	printf("Characteristic value was written successfully\n");
}

static void cmd_char_write(int argcp, char **argvp)
{
	gattlib_context_t* conn_context = g_connection->context;
	uint8_t *value;
	size_t plen;
	int handle;

	if (conn_state != STATE_CONNECTED) {
		printf("Command failed: disconnected\n");
		return;
	}

	if (argcp < 3) {
		printf("Usage: %s <handle> <new value>\n", argvp[0]);
		return;
	}

	handle = strtohandle(argvp[1]);
	if (handle <= 0) {
		printf("A valid handle is required\n");
		return;
	}

	plen = gatt_attr_data_from_string(argvp[2], &value);
	if (plen == 0) {
		g_printerr("Invalid value\n");
		return;
	}

	if (g_strcmp0("char-write-req", argvp[0]) == 0)
		gatt_write_char(conn_context->attrib, handle, value, plen,
					char_write_req_cb, NULL);
	else
		gatt_write_char(conn_context->attrib, handle, value, plen, NULL, NULL);

	g_free(value);
}

static void cmd_sec_level(int argcp, char **argvp)
{
	gattlib_context_t* conn_context = g_connection->context;
	GError *gerr = NULL;
	BtIOSecLevel sec_level;

	if (argcp < 2) {
		printf("sec-level: %s\n", opt_sec_level);
		return;
	}

	if (strcasecmp(argvp[1], "medium") == 0)
		sec_level = BT_IO_SEC_MEDIUM;
	else if (strcasecmp(argvp[1], "high") == 0)
		sec_level = BT_IO_SEC_HIGH;
	else if (strcasecmp(argvp[1], "low") == 0)
		sec_level = BT_IO_SEC_LOW;
	else {
		printf("Allowed values: low | medium | high\n");
		return;
	}

	g_free(opt_sec_level);
	opt_sec_level = g_strdup(argvp[1]);

	if (conn_state != STATE_CONNECTED)
		return;

	if (opt_psm) {
		printf("It must be reconnected to this change take effect\n");
		return;
	}

	bt_io_set(conn_context->io,
#if BLUEZ_VERSION_MAJOR == 4
			BT_IO_L2CAP,
#endif
			&gerr,
			BT_IO_OPT_SEC_LEVEL, sec_level,
			BT_IO_OPT_INVALID);

	if (gerr) {
		printf("Error: %s\n", gerr->message);
		g_error_free(gerr);
	}
}

static void exchange_mtu_cb(guint8 status, const guint8 *pdu, guint16 plen,
							gpointer user_data)
{
	gattlib_context_t* conn_context = g_connection->context;
	uint16_t mtu;

	if (status != 0) {
		printf("Exchange MTU Request failed: %s\n",
							att_ecode2str(status));
		return;
	}

	if (!dec_mtu_resp(pdu, plen, &mtu)) {
		printf("Protocol error\n");
		return;
	}

	mtu = MIN(mtu, opt_mtu);
	/* Set new value for MTU in client */
	if (g_attrib_set_mtu(conn_context->attrib, mtu))
		printf("MTU was exchanged successfully: %d\n", mtu);
	else
		printf("Error exchanging MTU\n");
}

static void cmd_mtu(int argcp, char **argvp)
{
	gattlib_context_t* conn_context = g_connection->context;

	if (conn_state != STATE_CONNECTED) {
		printf("Command failed: not connected.\n");
		return;
	}

	if (opt_psm) {
		printf("Command failed: operation is only available for LE"
							" transport.\n");
		return;
	}

	if (argcp < 2) {
		printf("Usage: mtu <value>\n");
		return;
	}

	if (opt_mtu) {
		printf("Command failed: MTU exchange can only occur once per"
							" connection.\n");
		return;
	}

	errno = 0;
	opt_mtu = strtoll(argvp[1], NULL, 0);
	if (errno != 0 || opt_mtu < ATT_DEFAULT_LE_MTU) {
		printf("Invalid value. Minimum MTU size is %d\n",
							ATT_DEFAULT_LE_MTU);
		return;
	}

	gatt_exchange_mtu(conn_context->attrib, opt_mtu, exchange_mtu_cb, NULL);
}

static struct {
	const char *cmd;
	void (*func)(int argcp, char **argvp);
	const char *params;
	const char *desc;
} commands[] = {
	{ "help",		cmd_help,	"",
		"Show this help"},
	{ "exit",		cmd_exit,	"",
		"Exit interactive mode" },
	{ "quit",		cmd_exit,	"",
		"Exit interactive mode" },
	{ "connect",		cmd_connect,	"[address [address type]]",
		"Connect to a remote device" },
	{ "disconnect",		cmd_disconnect,	"",
		"Disconnect from a remote device" },
	{ "primary",		cmd_primary,	"[UUID]",
		"Primary Service Discovery" },
	{ "characteristics",	cmd_char,	"[start hnd [end hnd [UUID]]]",
		"Characteristics Discovery" },
	{ "char-desc",		cmd_char_desc,	"[start hnd] [end hnd]",
		"Characteristics Descriptor Discovery" },
	{ "char-read-hnd",	cmd_read_hnd,	"<handle> [offset]",
		"Characteristics Value/Descriptor Read by handle" },
	{ "char-read-uuid",	cmd_read_uuid,	"<UUID> [start hnd] [end hnd]",
		"Characteristics Value/Descriptor Read by UUID" },
	{ "char-write-req",	cmd_char_write,	"<handle> <new value>",
		"Characteristic Value Write (Write Request)" },
	{ "char-write-cmd",	cmd_char_write,	"<handle> <new value>",
		"Characteristic Value Write (No response)" },
	{ "sec-level",		cmd_sec_level,	"[low | medium | high]",
		"Set security level. Default: low" },
	{ "mtu",		cmd_mtu,	"<value>",
		"Exchange MTU for GATT/ATT" },
	{ NULL, NULL, NULL}
};

static void cmd_help(int argcp, char **argvp)
{
	int i;

	for (i = 0; commands[i].cmd; i++)
		printf("%-15s %-30s %s\n", commands[i].cmd,
				commands[i].params, commands[i].desc);
}

static void parse_line(char *line_read)
{
	gchar **argvp;
	int argcp;
	int i;

	if (line_read == NULL) {
		printf("\n");
		cmd_exit(0, NULL);
		return;
	}

	line_read = g_strstrip(line_read);

	if (*line_read == '\0')
		return;

	add_history(line_read);

	g_shell_parse_argv(line_read, &argcp, &argvp, NULL);

	for (i = 0; commands[i].cmd; i++)
		if (strcasecmp(commands[i].cmd, argvp[0]) == 0)
			break;

	if (commands[i].cmd)
		commands[i].func(argcp, argvp);
	else
		printf("%s: command not found\n", argvp[0]);

	g_strfreev(argvp);
}

static gboolean prompt_read(GIOChannel *chan, GIOCondition cond,
							gpointer user_data)
{
	if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		g_io_channel_unref(chan);
		return FALSE;
	}

	rl_callback_read_char();

	return TRUE;
}

static char *completion_generator(const char *text, int state)
{
	static int index = 0, len = 0;
	const char *cmd = NULL;

	if (state == 0) {
		index = 0;
		len = strlen(text);
	}

	while ((cmd = commands[index].cmd) != NULL) {
		index++;
		if (strncmp(cmd, text, len) == 0)
			return strdup(cmd);
	}

	return NULL;
}

static char **commands_completion(const char *text, int start, int end)
{
	if (start == 0)
		return rl_completion_matches(text, &completion_generator);
	else
		return NULL;
}

int interactive(const gchar *src, const gchar *dst,
		const gchar *dst_type, int psm)
{
	GIOChannel *pchan;
	gint events;

	opt_sec_level = g_strdup("low");

	opt_src = g_strdup(src);
	opt_dst = g_strdup(dst);
	opt_dst_type = g_strdup(dst_type);
	opt_psm = psm;

	prompt = g_string_new(NULL);

	event_loop = g_main_loop_new(NULL, FALSE);

	pchan = g_io_channel_unix_new(fileno(stdin));
	g_io_channel_set_close_on_unref(pchan, TRUE);
	events = G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL;
	g_io_add_watch(pchan, events, prompt_read, NULL);

	rl_attempted_completion_function = commands_completion;
	rl_callback_handler_install(get_prompt(), parse_line);

	g_main_loop_run(event_loop);

	rl_callback_handler_remove();
	cmd_disconnect(0, NULL);
	g_io_channel_unref(pchan);
	g_main_loop_unref(event_loop);
	g_string_free(prompt, TRUE);

	g_free(opt_src);
	g_free(opt_dst);
	g_free(opt_sec_level);

	return 0;
}
