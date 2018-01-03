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

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

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

/* GATT Characteristic Properties Bitfield values */
#define GATTLIB_CHARACTERISTIC_BROADCAST			0x01
#define GATTLIB_CHARACTERISTIC_READ					0x02
#define GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP	0x04
#define GATTLIB_CHARACTERISTIC_WRITE				0x08
#define GATTLIB_CHARACTERISTIC_NOTIFY				0x10
#define GATTLIB_CHARACTERISTIC_INDICATE				0x20

#define CREATE_UUID16(value16) { .type=SDP_UUID16, .value.uuid16=(value16) }

typedef enum {
	BT_SEC_SDP = 0,
	BT_SEC_LOW,
	BT_SEC_MEDIUM,
	BT_SEC_HIGH,
} gattlib_bt_sec_level_t;

typedef struct _GAttrib GAttrib;


typedef void (*gattlib_event_handler_t)(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data);

typedef struct _gatt_connection_t {
	void* context;

	gattlib_event_handler_t notification_handler;
	void* notification_user_data;

	gattlib_event_handler_t indication_handler;
	void* indication_user_data;
} gatt_connection_t;

typedef void (*gattlib_discovered_device_t)(const char* addr, const char* name);
typedef void (*gatt_connect_cb_t)(gatt_connection_t* connection);
typedef void* (*gatt_read_cb_t)(const void* buffer, size_t buffer_len);



/*
 *  **** Asynchronous Operations ****
 *
 *  Asynchronous operations support added by Pat Deegan, psychogenic.com
 *  Two NOTES:
 *  	1)  they're async, but only use them "one at a time" -- meaning wait
 *  	    until operation A completes before starting operation B, and
 *  	2)  you need to allocate time to handle events/trigger callbacks,
 *  	    using gattlib_async_process/gattlib_async_process_all, as described
 *  	    below.
 *
 *  Now have async versions of the worst-offenders (slowest/longest blocking)
 *  are now available, including:
 *
 *  	- gattlib_adapter_scan_enable_async
 *  	- gattlib_adapter_scan_disable_async
 *  	- gattlib_async_connect (yes, I know the naming is weird, see below)
 *  	- gattlib_disconnect_async
 *  	- gattlib_read_char_by_uuid_async
 *  	- gattlib_write_char_by_uuid_async
 *
 *  These operations can be launched while you keep doing other things,
 *  e.g. handling user events. However, they need some time allocated
 *  in order to process events, trigger callbacks, etc.  The simplest
 *  way to do this is to periodically give them a moment in your main
 *  loop, eg
 *
 *  while (doKeepGoing) {
 *  	// handle user events
 *  	// do processing, etc.
 *
 *  	// process any async events
 *  	gattlib_async_process();
 *
 *  }
 *
 * See ble_scan_async example and/or gattlib_async.c, here.
 *
 */





/* gattlib_async_completed_cb_t -- generic "async is done" callback
 * signature: void X(void);
 */
typedef void (*gattlib_async_completed_cb_t)(void);
typedef gattlib_async_completed_cb_t gattlib_async_error_cb_t;
/*
 * async process:  the async calls (*_async()) need slots in which to process
 * pending events and do their thing.  This can be in a thread,
 * as part of your main loop, or whatever... just need to be called
 * periodically.
 *
 * gattlib_async_process() -- does one iteration of event processing
 *
 * gattlib_async_process_all() -- processes all pending events in one go.
 */
int gattlib_async_process();
int gattlib_async_process_all();






/**
 * Open Bluetooth adapter
 *
 * @adapter_name    With value NULL, the default adapter will be selected.
 */
int gattlib_adapter_open(const char* adapter_name, void** adapter);

/* gattlib_adapter_powered return gboolean TRUE (1) if powered, 0 otherwise */
int gattlib_adapter_powered(void* adapter);


int gattlib_adapter_scan_enable(void* adapter, gattlib_discovered_device_t discovered_device_cb, int timeout);
int gattlib_adapter_scan_enable_async(void* adapter, gattlib_discovered_device_t discovered_device_cb,
										int timeout, gattlib_async_completed_cb_t done_cb);



int gattlib_adapter_scan_disable(void* adapter);
int gattlib_adapter_scan_disable_async(void* adapter,
										gattlib_async_completed_cb_t done_cb);


int gattlib_adapter_close(void* adapter);

/**
 * @param src		Local Adaptater interface
 * @param dst		Remote Bluetooth address
 * @param dst_type	Set LE address type (either BDADDR_LE_PUBLIC or BDADDR_LE_RANDOM)
 * @param sec_level	Set security level (either BT_IO_SEC_LOW, BT_IO_SEC_MEDIUM, BT_IO_SEC_HIGH)
 * @param psm       Specify the PSM for GATT/ATT over BR/EDR
 * @param mtu       Specify the MTU size
 */
gatt_connection_t *gattlib_connect(const char *src, const char *dst,
				uint8_t dest_type, gattlib_bt_sec_level_t sec_level, int psm, int mtu);

/* original version of this function name, used under older bluez implementation,
 * not sure how it works or how it's async...
 *
 */
gatt_connection_t *gattlib_connect_async(const char *src, const char *dst,
				uint8_t dest_type, gattlib_bt_sec_level_t sec_level, int psm, int mtu,
				gatt_connect_cb_t connect_cb);
/*
 * oddly named async with callback connect(), see above
 */
int gattlib_async_connect(const char *src, const char *dst,
				uint8_t dest_type, gattlib_bt_sec_level_t sec_level, int psm, int mtu,
				gatt_connect_cb_t connect_cb);



int gattlib_disconnect(gatt_connection_t* connection);
int gattlib_disconnect_async(gatt_connection_t* connection,
								gattlib_async_completed_cb_t done_cb);

typedef struct {
	uint16_t  attr_handle_start;
	uint16_t  attr_handle_end;
	uuid_t    uuid;
} gattlib_primary_service_t;

typedef struct {
	uint16_t  handle;
	uint8_t   properties;
	uint16_t  value_handle;
	uuid_t    uuid;
} gattlib_characteristic_t;

typedef struct {
	uint16_t handle;
	uint16_t uuid16;
	uuid_t   uuid;
} gattlib_descriptor_t;

int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count);
int gattlib_discover_char_range(gatt_connection_t* connection, int start, int end, gattlib_characteristic_t** characteristics, int* characteristics_count);
int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristic_count);
int gattlib_discover_desc_range(gatt_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptor_count);
int gattlib_discover_desc(gatt_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptor_count);

int gattlib_read_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, void* buffer, size_t* buffer_len);
int gattlib_read_char_by_uuid_async(gatt_connection_t* connection, uuid_t* uuid, gatt_read_cb_t gatt_read_cb);

int gattlib_write_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len);
int gattlib_write_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len);

int gattlib_write_char_by_uuid_async(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len,
		gattlib_async_completed_cb_t done_cb, gattlib_async_error_cb_t error_cb);
/*
 * @param uuid     UUID of the characteristic that will trigger the notification
 */
int gattlib_notification_start(gatt_connection_t* connection, const uuid_t* uuid);
int gattlib_notification_stop(gatt_connection_t* connection, const uuid_t* uuid);

void gattlib_register_notification(gatt_connection_t* connection,
		gattlib_event_handler_t notification_handler, void* user_data);
void gattlib_register_indication(gatt_connection_t* connection, gattlib_event_handler_t indication_handler, void* user_data);

int gattlib_uuid_to_string(const uuid_t *uuid, char *str, size_t n);
int gattlib_string_to_uuid(const char *str, size_t n, uuid_t *uuid);
int gattlib_uuid_cmp(const uuid_t *uuid1, const uuid_t *uuid2);

#ifdef __cplusplus
}
#endif

#endif
