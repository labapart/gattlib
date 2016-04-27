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
#include <stdio.h>

#include "gattlib.h"

typedef enum { READ, WRITE} operation_t;
operation_t g_operation;

static bt_uuid_t g_uuid;
long int value_data;

static void usage(char *argv[]) {
	printf("%s <device_address> <read|write> <uuid> [<hex-value-to-write>]\n", argv[0]);
}

int main(int argc, char *argv[]) {
	uint8_t buffer[100];
	int i, len, ret;
	gatt_connection_t* connection;

	if ((argc != 4) && (argc != 5)) {
		usage(argv);
		return 1;
	}

	if (strcmp(argv[2], "read") == 0) {
		g_operation = READ;
	} else if ((strcmp(argv[2], "write") == 0) && (argc == 5)) {
		g_operation = WRITE;

		if ((strlen(argv[4]) >= 2) && (argv[4][0] == '0') && (argv[4][0] == 'x')) {
			value_data = strtol(argv[4], NULL, 0);
		} else {
			value_data = strtol(argv[4], NULL, 16);
		}
		printf("Value to write: 0x%lx\n", value_data);
	} else {
		usage(argv);
		return 1;
	}

	if (bt_string_to_uuid(&g_uuid, argv[3]) < 0) {
		usage(argv);
		return 1;
	}

	connection = gattlib_connect(NULL, argv[1], BDADDR_LE_PUBLIC, BT_IO_SEC_LOW, 0, 0);
	if (connection == NULL) {
		fprintf(stderr, "Fail to connect to the bluetooth device.\n");
		return 1;
	}

	if (g_operation == READ) {
		len = gattlib_read_char_by_uuid(connection, &g_uuid, buffer, sizeof(buffer));

		printf("Read UUID completed: ");
		for (i = 0; i < len; i++)
			printf("%02x ", buffer[i]);
		printf("\n");
	} else {
		uint16_t handle = 0; //TODO: FIXME
		ret = gattlib_write_char_by_handle(connection, handle, buffer, sizeof(buffer));
		assert(ret == 0);
	}

	gattlib_disconnect(connection);
	return 0;
}
