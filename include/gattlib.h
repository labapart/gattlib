/*
 *
 *  GattLib - GATT Library
 *
 *  Copyright (C) 2016-2019 Olivier Martin <olivier@labapart.org>
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

/* Gattlib errors */
#define GATTLIB_SUCCESS             0
#define GATTLIB_INVALID_PARAMETER   1
#define GATTLIB_NOT_FOUND           2
#define GATTLIB_OUT_OF_MEMORY       3
#define GATTLIB_NOT_SUPPORTED       4
#define GATTLIB_DEVICE_ERROR        5
#define GATTLIB_ERROR_DBUS          6

/* GATT Characteristic Properties Bitfield values */
#define GATTLIB_CHARACTERISTIC_BROADCAST			0x01
#define GATTLIB_CHARACTERISTIC_READ					0x02
#define GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP	0x04
#define GATTLIB_CHARACTERISTIC_WRITE				0x08
#define GATTLIB_CHARACTERISTIC_NOTIFY				0x10
#define GATTLIB_CHARACTERISTIC_INDICATE				0x20

#define CREATE_UUID16(value16) { .type=SDP_UUID16, .value.uuid16=(value16) }

//
// @brief Options for gattlib_connect()
//
// @note Options with the prefix `GATTLIB_CONNECTION_OPTIONS_LEGACY_`
//       is for Bluez prior to v5.42 (before Bluez) support
//
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC  (1 << 0)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM  (1 << 1)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW        (1 << 2)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_MEDIUM     (1 << 3)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_HIGH       (1 << 4)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_PSM(value)        (((value) & 0x3FF) << 11) //< We encode PSM on 10 bits (up to 1023)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_MTU(value)        (((value) & 0x3FF) << 21) //< We encode MTU on 10 bits (up to 1023)

#define GATTLIB_CONNECTION_OPTIONS_LEGACY_GET_PSM(options)  (((options) >> 11) && 0x3FF)
#define GATTLIB_CONNECTION_OPTIONS_LEGACY_GET_MTU(options)  (((options) >> 21) && 0x3FF)

#define GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT \
		GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC | \
		GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM | \
		GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW

typedef struct _GAttrib GAttrib;

typedef void (*gattlib_event_handler_t)(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data);

/**
 * @brief Handler called on disconnection
 *
 * @param connection Connection that is disconnecting
 * @param user_data  Data defined when calling `gattlib_register_on_disconnect()`
 */
typedef void (*gattlib_disconnection_handler_t)(void* user_data);

typedef struct _gatt_connection_t {
	void* context;

	gattlib_event_handler_t notification_handler;
	void* notification_user_data;

	gattlib_event_handler_t indication_handler;
	void* indication_user_data;

	gattlib_disconnection_handler_t disconnection_handler;
	void* disconnection_user_data;
} gatt_connection_t;

typedef void (*gattlib_discovered_device_t)(const char* addr, const char* name);
typedef void (*gatt_connect_cb_t)(gatt_connection_t* connection, void* user_data);

/**
 * @brief Callback called when GATT characteristic read value has been received
 *
 * @param buffer contains the value to read.
 * @param buffer_len Length of the read data
 *
 */
typedef void* (*gatt_read_cb_t)(const void *buffer, size_t buffer_len);

/**
 * @brief Open Bluetooth adapter
 *
 * @param adapter_name    With value NULL, the default adapter will be selected.
 */
int gattlib_adapter_open(const char* adapter_name, void** adapter);
int gattlib_adapter_scan_enable(void* adapter, gattlib_discovered_device_t discovered_device_cb, int timeout);
int gattlib_adapter_scan_disable(void* adapter);
int gattlib_adapter_close(void* adapter);

/**
 * @brief Function to connect to a BLE device
 *
 * @param src		Local Adaptater interface
 * @param dst		Remote Bluetooth address
 * @param options	Options to connect to BLE device. See `GATTLIB_CONNECTION_OPTIONS_*`
 */
gatt_connection_t *gattlib_connect(const char *src, const char *dst, unsigned long options);

/**
 * @brief Function to asynchronously connect to a BLE device
 *
 * @note This function is mainly used before Bluez v5.42 (prior to D-BUS support)
 *
 * @param src		Local Adaptater interface
 * @param dst		Remote Bluetooth address
 * @param options	Options to connect to BLE device. See `GATTLIB_CONNECTION_OPTIONS_*`
 */
gatt_connection_t *gattlib_connect_async(const char *src, const char *dst,
				unsigned long options,
                                gatt_connect_cb_t connect_cb, void* data);

int gattlib_disconnect(gatt_connection_t* connection);

void gattlib_register_on_disconnect(gatt_connection_t *connection, gattlib_disconnection_handler_t handler, void* user_data);

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

/**
 * @brief Function to discover GATT Services
 *
 * @note This function can be used to force GATT services/characteristic discovery
 *
 * @param connection Active GATT connection
 * @param services array of GATT services allocated by the function. Can be NULL.
 * @param services_count Number of GATT services discovered. Can be NULL
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count);

int gattlib_discover_char_range(gatt_connection_t* connection, int start, int end, gattlib_characteristic_t** characteristics, int* characteristics_count);
int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristic_count);
int gattlib_discover_desc_range(gatt_connection_t* connection, int start, int end, gattlib_descriptor_t** descriptors, int* descriptor_count);
int gattlib_discover_desc(gatt_connection_t* connection, gattlib_descriptor_t** descriptors, int* descriptor_count);

/**
 * @brief Function to read GATT characteristic
 *
 * @note buffer is allocated by the function. It is the responsibility of the caller to free the buffer.
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the GATT characteristic to read
 * @param buffer contains the value to read. It is allocated by the function.
 * @param buffer_len Length of the read data
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_read_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, void** buffer, size_t* buffer_len);

/**
 * @brief Function to asynchronously read GATT characteristic
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the GATT characteristic to read
 * @param gatt_read_cb is the callback to read when the GATT characteristic is available
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_read_char_by_uuid_async(gatt_connection_t* connection, uuid_t* uuid, gatt_read_cb_t gatt_read_cb);

/**
 * @brief Function to write to the GATT characteristic UUID
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the GATT characteristic to read
 * @param buffer contains the values to write to the GATT characteristic
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len);

/**
 * @brief Function to write to the GATT characteristic handle
 *
 * @param connection Active GATT connection
 * @param handle is the handle of the GATT characteristic
 * @param buffer contains the values to write to the GATT characteristic
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len);

/**
 * @brief Function to write without response to the GATT characteristic UUID
 *
 * @param connection Active GATT connection
 * @param uuid UUID of the GATT characteristic to read
 * @param buffer contains the values to write to the GATT characteristic
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_without_response_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len);

/**
 * @brief Function to write without response to the GATT characteristic handle
 *
 * @param connection Active GATT connection
 * @param handle is the handle of the GATT characteristic
 * @param buffer contains the values to write to the GATT characteristic
 * @param buffer_len is the length of the buffer to write
 *
 * @return GATTLIB_SUCCESS on success or GATTLIB_* error code
 */
int gattlib_write_without_response_char_by_handle(gatt_connection_t* connection, uint16_t handle, const void* buffer, size_t buffer_len);

/*
 * @param uuid     UUID of the characteristic that will trigger the notification
 */
int gattlib_notification_start(gatt_connection_t* connection, const uuid_t* uuid);
int gattlib_notification_stop(gatt_connection_t* connection, const uuid_t* uuid);

void gattlib_register_notification(gatt_connection_t* connection, gattlib_event_handler_t notification_handler, void* user_data);
void gattlib_register_indication(gatt_connection_t* connection, gattlib_event_handler_t indication_handler, void* user_data);

int gattlib_uuid_to_string(const uuid_t *uuid, char *str, size_t n);
int gattlib_string_to_uuid(const char *str, size_t n, uuid_t *uuid);
int gattlib_uuid_cmp(const uuid_t *uuid1, const uuid_t *uuid2);

#ifdef __cplusplus
}
#endif

#endif
