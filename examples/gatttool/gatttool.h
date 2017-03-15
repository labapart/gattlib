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

#include <readline/readline.h>

void notification_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data);
void indication_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data);

int interactive(const gchar *src, const gchar *dst, const gchar *dst_type,
		gboolean le);
size_t gatt_attr_data_from_string(const char *str, uint8_t **data);
uint8_t get_dest_type_from_str(const char* dst_type);
BtIOSecLevel get_sec_level_from_str(const char* sec_level);

typedef struct {
	GIOChannel*               io;
	GAttrib*                  attrib;

	// We keep a list of characteristics to make the correspondence handle/UUID.
	gattlib_characteristic_t* characteristics;
	int                       characteristic_count;
} gattlib_context_t;
