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

#include <stdlib.h>
#include <glib.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "uuid.h"
#include <bluetooth/sdp.h>

#include "gattlib.h"

#include "gattrib.h"
#include "btio.h"
#include "gatttool.h"

size_t gatt_attr_data_from_string(const char *str, uint8_t **data)
{
	char tmp[3];
	size_t size, i;

	size = strlen(str) / 2;
	*data = g_try_malloc0(size);
	if (*data == NULL)
		return 0;

	tmp[2] = '\0';
	for (i = 0; i < size; i++) {
		memcpy(tmp, str + (i * 2), 2);
		(*data)[i] = (uint8_t) strtol(tmp, NULL, 16);
	}

	return size;
}

uint8_t get_dest_type_from_str(const char* dst_type) {
	if (strcmp(dst_type, "random") == 0)
		return BDADDR_LE_RANDOM;
	else
		return BDADDR_LE_PUBLIC;
}

BtIOSecLevel get_sec_level_from_str(const char* sec_level) {
	if (strcasecmp(sec_level, "medium") == 0)
		return BT_IO_SEC_MEDIUM;
	else if (strcasecmp(sec_level, "high") == 0)
		return BT_IO_SEC_HIGH;
	else
		return BT_IO_SEC_LOW;
}
