/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2021  Olivier Martin <olivier@labapart.org>
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
#include <glib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
#include <syslog.h>
#endif

#include "gattlib.h"

static uuid_t g_notify_uuid;
static uuid_t g_write_uuid;

static GMainLoop *m_main_loop;

void notification_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
	int i;

	printf("Notification Handler: ");

	for (i = 0; i < data_length; i++) {
		printf("%02x ", data[i]);
	}
	printf("\n");
}

static void on_user_abort(int arg) {
	g_main_loop_quit(m_main_loop);
}

static void usage(char *argv[]) {
	printf("%s <device_address> <notification_characteristic_uuid> [<write_characteristic_uuid> <write_characteristic_hex_data> ...]\n", argv[0]);
}


int main(int argc, char *argv[]) {
	int ret;
	int argid;
	gatt_connection_t* connection;

	if (argc < 3) {
		usage(argv);
		return 1;
	}

	if (gattlib_string_to_uuid(argv[2], strlen(argv[2]) + 1, &g_notify_uuid) < 0) {
		usage(argv);
		return 1;
	}

	if (argc > 3) {
		if (gattlib_string_to_uuid(argv[3], strlen(argv[3]) + 1, &g_write_uuid) < 0) {
			usage(argv);
			return 1;
		}
	}

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
	openlog("gattlib_notification", LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);
	setlogmask(LOG_UPTO(LOG_DEBUG));
#endif

	connection = gattlib_connect(NULL, argv[1], GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT);
	if (connection == NULL) {
		GATTLIB_LOG(GATTLIB_ERROR, "Fail to connect to the bluetooth device.");
		return 1;
	}

	gattlib_register_notification(connection, notification_handler, NULL);

#ifdef GATTLIB_LOG_BACKEND_SYSLOG
	openlog("gattlib_notification", LOG_CONS | LOG_NDELAY | LOG_PERROR, LOG_USER);
	setlogmask(LOG_UPTO(LOG_DEBUG));
#endif

	ret = gattlib_notification_start(connection, &g_notify_uuid);
	if (ret) {
		GATTLIB_LOG(GATTLIB_ERROR, "Fail to start notification.");
		goto DISCONNECT;
	}

	// Optional byte writes to make to trigger notifications
	for (argid = 4; argid < argc; argid ++) {
		unsigned char data[256];
		char * charp;
		unsigned char * datap;
		for (charp = argv[4], datap = data; charp[0] && charp[1]; charp += 2, datap ++) {
			sscanf(charp, "%02hhx", datap);
		}
		ret = gattlib_write_char_by_uuid(connection, &g_write_uuid, data, datap - data);

		if (ret != GATTLIB_SUCCESS) {
			if (ret == GATTLIB_NOT_FOUND) {
				GATTLIB_LOG(GATTLIB_ERROR, "Could not find GATT Characteristic with UUID %s.", argv[3]);
			} else {
				GATTLIB_LOG(GATTLIB_ERROR, "Error while writing GATT Characteristic with UUID %s (ret:%d)",
					argv[3], ret);
			}
			goto DISCONNECT;
		}
	}

	// Catch CTRL-C
	signal(SIGINT, on_user_abort);

	m_main_loop = g_main_loop_new(NULL, 0);
	g_main_loop_run(m_main_loop);

	// In case we quit the main loop, clean the connection
	gattlib_notification_stop(connection, &g_notify_uuid);
	g_main_loop_unref(m_main_loop);

DISCONNECT:
	gattlib_disconnect(connection);
	puts("Done");
	return ret;
}
