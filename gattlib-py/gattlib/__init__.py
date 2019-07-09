from ctypes import *

gattlib = CDLL("libgattlib.so")


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


# int gattlib_adapter_open(const char* adapter_name, void** adapter);
gattlib_adapter_open = gattlib.gattlib_adapter_open
gattlib_adapter_open.argtypes = [c_char_p, POINTER(c_void_p)]

# typedef void (*gattlib_discovered_device_t)(const char* addr, const char* name)
gattlib_discovered_device_type = CFUNCTYPE(None, c_char_p, c_char_p)

# int gattlib_adapter_scan_enable_with_filter(void *adapter, uuid_t **uuid_list, int16_t rssi_threshold, uint32_t enabled_filters,
#        gattlib_discovered_device_t discovered_device_cb, int timeout)
gattlib_adapter_scan_enable_with_filter = gattlib.gattlib_adapter_scan_enable_with_filter
gattlib_adapter_scan_enable_with_filter.argtypes = [c_void_p, POINTER(POINTER(GattlibUuid)), c_int16, c_uint32, gattlib_discovered_device_type, c_int]

# int gattlib_discover_primary(gatt_connection_t* connection, gattlib_primary_service_t** services, int* services_count);
gattlib_discover_primary = gattlib.gattlib_discover_primary
gattlib_discover_primary.argtypes = [c_void_p, POINTER(POINTER(GattlibPrimaryService)), POINTER(c_int)]

# int gattlib_discover_char(gatt_connection_t* connection, gattlib_characteristic_t** characteristics, int* characteristic_count);
gattlib_discover_char = gattlib.gattlib_discover_char
gattlib_discover_char.argtypes = [c_void_p, POINTER(POINTER(GattlibCharacteristic)), POINTER(c_int)]

# int gattlib_read_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, void** buffer, size_t* buffer_len);
gattlib_read_char_by_uuid = gattlib.gattlib_read_char_by_uuid
gattlib_read_char_by_uuid.argtypes = [c_void_p, POINTER(GattlibUuid), POINTER(c_void_p), POINTER(c_size_t)]

# int gattlib_write_char_by_uuid(gatt_connection_t* connection, uuid_t* uuid, const void* buffer, size_t buffer_len)
gattlib_write_char_by_uuid = gattlib.gattlib_write_char_by_uuid
gattlib_write_char_by_uuid.argtypes = [c_void_p, POINTER(GattlibUuid), c_void_p, c_size_t]

# int gattlib_notification_start(gatt_connection_t* connection, const uuid_t* uuid);
gattlib_notification_start = gattlib.gattlib_notification_start
gattlib_notification_start.argtypes = [c_void_p, POINTER(GattlibUuid)]

# int gattlib_notification_stop(gatt_connection_t* connection, const uuid_t* uuid);
gattlib_notification_stop = gattlib.gattlib_notification_stop
gattlib_notification_stop.argtypes = [c_void_p, POINTER(GattlibUuid)]

# void gattlib_register_notification_python(gatt_connection_t* connection, PyObject *notification_handler, PyObject *user_data)
gattlib_register_notification = gattlib.gattlib_register_notification_python
gattlib_register_notification.argtypes = [c_void_p, py_object, py_object]

# void gattlib_register_on_disconnect_python(gatt_connection_t *connection, PyObject *handler, PyObject *user_data)
gattlib_register_on_disconnect = gattlib.gattlib_register_on_disconnect_python
gattlib_register_on_disconnect.argtypes = [c_void_p, py_object, py_object]

# int gattlib_get_rssi(gatt_connection_t *connection, int16_t *rssi)
gattlib_get_rssi = gattlib.gattlib_get_rssi
gattlib_get_rssi.argtypes = [c_void_p, POINTER(c_int16)]

# int gattlib_get_rssi_from_mac(void *adapter, const char *mac_address, int16_t *rssi)
gattlib_get_rssi_from_mac = gattlib.gattlib_get_rssi_from_mac
gattlib_get_rssi_from_mac.argtypes = [c_void_p, c_char_p, POINTER(c_int16)]
