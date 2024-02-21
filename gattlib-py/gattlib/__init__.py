#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

from ctypes import *
import logging

logger = logging.getLogger(__name__)

gattlib = CDLL("libgattlib.so")

def native_logging(level: int, string: str):
    if level == 3:
        logger.debug(string)
    elif level == 2:
        logger.info(string)
    elif level == 1:
        logger.warning(string)
    elif level == 0:
        logger.error(string)
    else:
        logger.debug(string)

try:
    # void gattlib_log_init(PyObject* logging_func)
    gattlib_log_init = gattlib.gattlib_log_init
    gattlib_log_init.argtypes = [py_object]

    # Declare Python function for logging native string
    gattlib_log_init(native_logging)
except AttributeError:
    # Excepted when using a Gattlib logging backend without 'gattlib_log_init'
    pass

# typedef struct {
#    uint8_t data[16];
# } uint128_t;
class GattlibUuid128(Structure):
    _fields_ = [("data", c_byte * 16)]


# typedef struct {
#    uint8_t type;
#    union {
#        uint16_t  uuid16;
#        uint32_t  uuid32;
#        uint128_t uuid128;
#    } value;
# } uuid_t;
class GattlibUuidValue(Union):
    _fields_ = [("uuid16", c_ushort), ("uuid32", c_uint), ("uuid128", GattlibUuid128)]


class GattlibUuid(Structure):
    _fields_ = [("type", c_byte), ("value", GattlibUuidValue)]


# typedef struct {
#    uint16_t  attr_handle_start;
#    uint16_t  attr_handle_end;
#    uuid_t    uuid;
# } gattlib_primary_service_t;
class GattlibPrimaryService(Structure):
    _fields_ = [("attr_handle_start", c_ushort),
                ("attr_handle_end", c_ushort),
                ("uuid", GattlibUuid)]


# typedef struct {
#    uint16_t  handle;
#    uint8_t   properties;
#    uint16_t  value_handle;
#    uuid_t    uuid;
# } gattlib_characteristic_t;
class GattlibCharacteristic(Structure):
    _fields_ = [("handle", c_ushort),
                ("properties", c_byte),
                ("value_handle", c_ushort),
                ("uuid", GattlibUuid)]


# typedef struct {
#     uuid_t   uuid;
#     uint8_t* data;
#     size_t   data_length;
# } gattlib_advertisement_data_t;
class GattlibAdvertisementData(Structure):
    _fields_ = [("uuid", GattlibUuid),
                ("data", c_void_p),
                ("data_length", c_size_t)]


# int gattlib_adapter_open(const char* adapter_name, void** adapter);
gattlib_adapter_open = gattlib.gattlib_adapter_open
gattlib_adapter_open.argtypes = [c_char_p, POINTER(c_void_p)]

# const char *gattlib_adapter_get_name(void* adapter)
gattlib_adapter_get_name = gattlib.gattlib_adapter_get_name
gattlib_adapter_get_name.argtypes = [c_void_p]
gattlib_adapter_get_name.restype = c_char_p

# void gattlib_discovered_device_python_callback(void *adapter, const char* addr, const char* name, void *user_data)
gattlib_discovered_device_python_callback = gattlib.gattlib_discovered_device_python_callback
gattlib_discovered_device_python_callback.argtypes = [c_void_p, c_char_p, c_char_p, py_object]
gattlib_discovered_device_python_callback.restype = c_void_p

# void gattlib_connected_device_python_callback(void *adapter, const char *dst, gatt_connection_t* connection, int error, void* user_data);
gattlib_connected_device_python_callback = gattlib.gattlib_connected_device_python_callback
gattlib_connected_device_python_callback.argtypes = [c_void_p, c_char_p, c_void_p, c_int, py_object]
gattlib_connected_device_python_callback.restype = c_void_p

# void gattlib_disconnected_device_python_callback(void *user_data)
gattlib_disconnected_device_python_callback = gattlib.gattlib_disconnected_device_python_callback
gattlib_disconnected_device_python_callback.argtypes = [py_object]
gattlib_disconnected_device_python_callback.restype = c_void_p

# void gattlib_notification_device_python_callback(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data);
gattlib_notification_device_python_callback = gattlib.gattlib_notification_device_python_callback
gattlib_notification_device_python_callback.argtypes = [c_void_p, c_void_p, c_int, c_void_p]
gattlib_notification_device_python_callback.restype = c_void_p

# void* gattlib_python_callback_args(PyObject* python_callback, PyObject* python_args) {
gattlib_python_callback_args = gattlib.gattlib_python_callback_args
gattlib_python_callback_args.argtypes = [py_object, py_object]
gattlib_python_callback_args.restype = c_void_p

# int gattlib_adapter_scan_enable_with_filter_non_blocking(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
#        gattlib_discovered_device_t discovered_device_cb, size_t timeout, void *user_data)
gattlib_adapter_scan_enable_with_filter_non_blocking = gattlib.gattlib_adapter_scan_enable_with_filter_non_blocking
gattlib_adapter_scan_enable_with_filter_non_blocking.argtypes = [c_void_p, POINTER(POINTER(GattlibUuid)), c_int16, c_uint32, c_void_p, c_size_t, c_void_p]

# int gattlib_adapter_scan_eddystone(void *adapter, int16_t rssi_threshold, uint32_t eddsytone_types,
#        gattlib_discovered_device_with_data_t discovered_device_cb, size_t timeout, void *user_data)
gattlib_adapter_scan_eddystone = gattlib.gattlib_adapter_scan_eddystone
gattlib_adapter_scan_eddystone.argtypes = [c_void_p, c_int16, c_uint32, c_void_p, c_size_t, c_void_p]

# gatt_connection_t *gattlib_connect(const char *src, const char *dst, unsigned long options);
gattlib_connect = gattlib.gattlib_connect
gattlib_connect.restype = c_void_p
gattlib_connect.argtypes = [c_char_p, c_char_p, c_ulong]

# int gattlib_disconnect(gatt_connection_t* connection);
gattlib_disconnect = gattlib.gattlib_disconnect
gattlib_disconnect.argtypes = [c_void_p]

# int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count);
gattlib_discover_primary = gattlib.gattlib_discover_primary
gattlib_discover_primary.argtypes = [c_void_p, POINTER(POINTER(GattlibPrimaryService)), POINTER(c_int)]

# int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristic_count);
gattlib_discover_char = gattlib.gattlib_discover_char
gattlib_discover_char.argtypes = [c_void_p, POINTER(POINTER(GattlibCharacteristic)), POINTER(c_int)]

# int gattlib_read_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, void** buffer, size_t* buffer_len);
gattlib_read_char_by_uuid = gattlib.gattlib_read_char_by_uuid
gattlib_read_char_by_uuid.argtypes = [c_void_p, POINTER(GattlibUuid), POINTER(c_void_p), POINTER(c_size_t)]

# void gattlib_characteristic_free_value(void* buffer);
gattlib_characteristic_free_value = gattlib.gattlib_characteristic_free_value
gattlib_characteristic_free_value.argtypes = [c_void_p]

# int gattlib_write_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len)
gattlib_write_char_by_uuid = gattlib.gattlib_write_char_by_uuid
gattlib_write_char_by_uuid.argtypes = [c_void_p, POINTER(GattlibUuid), c_void_p, c_size_t]

# int gattlib_write_without_response_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len)
gattlib_write_without_response_char_by_uuid = gattlib.gattlib_write_without_response_char_by_uuid
gattlib_write_without_response_char_by_uuid.argtypes = [c_void_p, POINTER(GattlibUuid), c_void_p, c_size_t]

# int gattlib_write_char_by_uuid_stream_open(gatt_connection_t* connection, uuid_t* uuid, gatt_stream_t **stream, uint16_t *mtu)
gattlib_write_char_by_uuid_stream_open = gattlib.gattlib_write_char_by_uuid_stream_open
gattlib_write_char_by_uuid_stream_open.argtypes = [c_void_p, POINTER(GattlibUuid), POINTER(c_void_p), POINTER(c_uint16)]

# int gattlib_notification_start(gatt_connection_t* connection, const uuid_t* uuid);
gattlib_notification_start = gattlib.gattlib_notification_start
gattlib_notification_start.argtypes = [c_void_p, POINTER(GattlibUuid)]

# int gattlib_notification_stop(gatt_connection_t* connection, const uuid_t* uuid);
gattlib_notification_stop = gattlib.gattlib_notification_stop
gattlib_notification_stop.argtypes = [c_void_p, POINTER(GattlibUuid)]

# int gattlib_register_notification(gatt_connection_t* connection, gattlib_event_handler_t notification_handler, void* user_data);
gattlib_register_notification = gattlib.gattlib_register_notification
gattlib_register_notification.argtypes = [c_void_p, c_void_p, c_void_p]

# int gattlib_register_on_disconnect(gatt_connection_t *connection, PyObject *handler, PyObject *user_data)
gattlib_register_on_disconnect = gattlib.gattlib_register_on_disconnect
gattlib_register_on_disconnect.argtypes = [c_void_p, c_void_p, c_void_p]

# int gattlib_get_rssi(gatt_connection_t *connection, int16_t *rssi)
gattlib_get_rssi = gattlib.gattlib_get_rssi
gattlib_get_rssi.argtypes = [c_void_p, POINTER(c_int16)]

# int gattlib_get_rssi_from_mac(void *adapter, const char *mac_address, int16_t *rssi)
gattlib_get_rssi_from_mac = gattlib.gattlib_get_rssi_from_mac
gattlib_get_rssi_from_mac.argtypes = [c_void_p, c_char_p, POINTER(c_int16)]

# int gattlib_get_advertisement_data(gatt_connection_t *connection,
# 		 gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_count,
# 		 uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size)
gattlib_get_advertisement_data = gattlib.gattlib_get_advertisement_data
gattlib_get_advertisement_data.argtypes = [c_void_p, POINTER(POINTER(GattlibAdvertisementData)), POINTER(c_size_t), POINTER(c_uint16), POINTER(c_void_p), POINTER(c_size_t)]

# int gattlib_get_advertisement_data_from_mac(void *adapter, const char *mac_address,
#        gattlib_advertisement_data_t **advertisement_data, size_t *advertisement_data_length,
#        uint16_t *manufacturer_id, uint8_t **manufacturer_data, size_t *manufacturer_data_size)
gattlib_get_advertisement_data_from_mac = gattlib.gattlib_get_advertisement_data_from_mac
gattlib_get_advertisement_data_from_mac.argtypes = [c_void_p, c_char_p, POINTER(POINTER(GattlibAdvertisementData)), POINTER(c_size_t), POINTER(c_uint16), POINTER(c_void_p), POINTER(c_size_t)]

# int gattlib_mainloop_python(PyObject *handler, PyObject *user_data)
gattlib_mainloop = gattlib.gattlib_mainloop_python
gattlib_mainloop.argtypes = [py_object, py_object]
