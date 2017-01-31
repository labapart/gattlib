/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2017 Olivier Martin <olivier@labapart.org>
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

#ifndef __GATTLIB_H__
#define __GATTLIB_H__

#include <glib.h>
#include <stdint.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include "uuid.h"
#include "btio.h"

#ifndef BDADDR_BREDR
  /* GattLib note: BD Address have only been introduced into Bluez v4.100.   */
  /*               Prior to this version, only BDADDR_BREDR can be supported */

  /* BD Address type */
  #define BDADDR_BREDR           0x00
  #define BDADDR_LE_PUBLIC       0x01
  #define BDADDR_LE_RANDOM       0x02
#endif

#if BLUEZ_VERSION_MAJOR == 5
  #define ATT_MAX_MTU ATT_MAX_VALUE_LEN
#endif

#if BLUEZ_VERSION_MAJOR == 4
typedef enum {
	BT_IO_SEC_SDP = 0,
	BT_IO_SEC_LOW,
	BT_IO_SEC_MEDIUM,
	BT_IO_SEC_HIGH,
} BtIOSecLevel;
#endif

typedef struct _GAttrib GAttrib;

typedef void (*gattlib_event_handler_t)(uint16_t handle, const uint8_t* data, size_t data_length, void* user_data);

typedef struct _gatt_connection_t {
	GIOChannel *io;

	GAttrib *attrib;

	gattlib_event_handler_t notification_handler;
	void* notification_user_data;

	gattlib_event_handler_t indication_handler;
	void* indication_user_data;
} gatt_connection_t;

typedef void (*gatt_connect_cb_t)(gatt_connection_t* connection);
typedef void* (*gatt_read_cb_t)(void* buffer, size_t buffer_len);

/**
 * @param src		Local Adaptater interface
 * @param dst		Remote Bluetooth address
 * @param dst_type	Set LE address type (either BDADDR_LE_PUBLIC or BDADDR_LE_RANDOM)
 * @param sec_level	Set security level (either BT_IO_SEC_LOW, BT_IO_SEC_MEDIUM, BT_IO_SEC_HIGH)
 * @param psm       Specify the PSM for GATT/ATT over BR/EDR
 * @param mtu       Specify the MTU size
 */
gatt_connection_t *gattlib_connect(const gchar *src, const gchar *dst,
				uint8_t dest_type, BtIOSecLevel sec_level, int psm, int mtu);

gatt_connection_t *gattlib_connect_async(const gchar *src, const gchar *dst,
				uint8_t dest_type, BtIOSecLevel sec_level, int psm, int mtu,
				gatt_connect_cb_t connect_cb);

int gattlib_disconnect(gatt_connection_t* connection);

typedef struct {
	uint16_t  attr_handle_start;
	uint16_t  attr_handle_end;
	bt_uuid_t uuid;
} gattlib_primary_service_t;

typedef struct {
	uint16_t  handle;
	uint8_t   properties;
	uint16_t  value_handle;
	bt_uuid_t uuid;
} gattlib_characteristic_t;

typedef struct {
	char uuid[MAX_LEN_UUID_STR + 1];
	uint16_t handle;
	uint16_t uuid16;
} gattlib_descriptor_t;

int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count);
int gattlib_discover_char_range(gatt_connection_t* connection, int start, int end, gattlib_characteristic_t** characteristics, int* characteristics_count);
int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristic_count);
int gattlib_discover_desc_range(gatt_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptor_count);
int gattlib_discover_desc(gatt_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptor_count);

int gattlib_read_char_by_uuid(gatt_connection_t* connection, bt_uuid_t* uuid, void* buffer, size_t buffer_len);
int gattlib_read_char_by_uuid_async(gatt_connection_t* connection, bt_uuid_t* uuid, gatt_read_cb_t gatt_read_cb);

int gattlib_write_char_by_handle(gatt_connection_t* connection, uint16_t handle, void* buffer, size_t buffer_len);

void gattlib_register_notification(gatt_connection_t* connection, gattlib_event_handler_t notification_handler, void* user_data);
void gattlib_register_indication(gatt_connection_t* connection, gattlib_event_handler_t indication_handler, void* user_data);

int gattlib_uuid_to_string(const bt_uuid_t *uuid, char *str, size_t n);
int gattlib_string_to_uuid(bt_uuid_t *uuid, const char *str);

#endif
