#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2022, Olivier Martin <olivier@labapart.org>
#

import logging
import uuid

from gattlib import *
from .exception import handle_return, DeviceError
from .gatt import GattService, GattCharacteristic
from .uuid import gattlib_uuid_to_int

CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC = (1 << 0)
CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM = (1 << 1)
CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW = (1 << 2)
CONNECTION_OPTIONS_LEGACY_BT_SEC_MEDIUM = (1 << 3)
CONNECTION_OPTIONS_LEGACY_BT_SEC_HIGH = (1 << 4)

CONNECTION_OPTIONS_LEGACY_DEFAULT = \
        CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC | \
        CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM | \
        CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW


class Device:

    def __init__(self, adapter, addr, name=None):
        self._adapter = adapter
        if type(addr) == str:
            self._addr = addr.encode("utf-8")
        else:
            self._addr = addr
        self._name = name
        self._connection = None

        # Keep track if notification handler has been initialized
        self._is_notification_init = False

        # Dictionnary for GATT characteristic callback
        self._gatt_characteristic_callbacks = {}

    @property
    def mac_address(self):
        """Return Device MAC Address"""
        return self._addr.decode("utf-8")

    @property
    def connection(self):
        if self._connection:
            return self._connection
        else:
            return c_void_p(None)

    def connect(self, options=CONNECTION_OPTIONS_LEGACY_DEFAULT):
        if self._adapter:
            adapter_name = self._adapter.name
        else:
            adapter_name = None

        self._connection = gattlib_connect(adapter_name, self._addr, options)
        if self._connection is None:
            raise DeviceError()

    @property
    def rssi(self):
        _rssi = c_int16(0)
        if self._connection:
            ret = gattlib_get_rssi(self._connection, byref(_rssi))
            handle_return(ret)
            return _rssi.value
        else:
            return self._adapter.get_rssi_from_mac(self._addr)

    @staticmethod
    def on_disconnection(user_data):
        this = user_data

        this.disconnection_callback(this.disconnection_user_data)

    def register_on_disconnect(self, callback, user_data):
        self.disconnection_callback = callback
        self.disconnection_user_data = user_data

        gattlib_register_on_disconnect(self.connection, Device.on_disconnection, self)

    def disconnect(self):
        ret = gattlib_disconnect(self.connection)
        handle_return(ret)

    def discover(self):
        #
        # Discover GATT Services
        #
        _services = POINTER(GattlibPrimaryService)()
        _services_count = c_int(0)
        ret = gattlib_discover_primary(self.connection, byref(_services), byref(_services_count))
        handle_return(ret)

        self._services = {}
        for i in range(0, _services_count.value):
            service = GattService(self, _services[i])
            self._services[service.short_uuid] = service

            logging.debug("Service UUID:0x%x" % service.short_uuid)

        #
        # Discover GATT Characteristics
        #
        _characteristics = POINTER(GattlibCharacteristic)()
        _characteristics_count = c_int(0)
        ret = gattlib_discover_char(self.connection, byref(_characteristics), byref(_characteristics_count))
        handle_return(ret)

        self._characteristics = {}
        for i in range(0, _characteristics_count.value):
            characteristic = GattCharacteristic(self, _characteristics[i])
            self._characteristics[characteristic.short_uuid] = characteristic

            logging.debug("Characteristic UUID:0x%x" % characteristic.short_uuid)

    def get_advertisement_data(self):
        _advertisement_data = POINTER(GattlibAdvertisementData)()
        _advertisement_data_count = c_size_t(0)
        _manufacturer_id = c_uint16(0)
        _manufacturer_data = c_void_p(None)
        _manufacturer_data_len = c_size_t(0)

        if self._connection is None:
            ret = gattlib_get_advertisement_data_from_mac(self._adapter._adapter, self._addr,
                                                          byref(_advertisement_data), byref(_advertisement_data_count),
                                                          byref(_manufacturer_id),
                                                          byref(_manufacturer_data), byref(_manufacturer_data_len))
        else:
            ret = gattlib_get_advertisement_data(self._connection,
                                                 byref(_advertisement_data), byref(_advertisement_data_count),
                                                 byref(_manufacturer_id),
                                                 byref(_manufacturer_data), byref(_manufacturer_data_len))

        handle_return(ret)

        advertisement_data = {}
        manufacturer_data = None

        for i in range(0, _advertisement_data_count.value):
            service_data = _advertisement_data[i]
            uuid = gattlib_uuid_to_int(service_data.uuid)

            pointer_type = POINTER(c_byte * service_data.data_length)
            c_bytearray = cast(service_data.data, pointer_type)

            data = bytearray(service_data.data_length)
            for i in range(service_data.data_length):
                data[i] = c_bytearray.contents[i] & 0xFF

            advertisement_data[uuid] = data

        if _manufacturer_data_len.value > 0:
            pointer_type = POINTER(c_byte * _manufacturer_data_len.value)
            c_bytearray = cast(_manufacturer_data, pointer_type)

            manufacturer_data = bytearray(_manufacturer_data_len.value)
            for i in range(_manufacturer_data_len.value):
                manufacturer_data[i] = c_bytearray.contents[i] & 0xFF

        return advertisement_data, _manufacturer_id.value, manufacturer_data

    @property
    def services(self):
        if not hasattr(self, '_services'):
            logging.warning("Start GATT discovery implicitly")
            self.discover()

        return self._services

    @property
    def characteristics(self):
        if not hasattr(self, '_characteristics'):
            logging.warning("Start GATT discovery implicitly")
            self.discover()

        return self._characteristics

    @staticmethod
    def notification_callback(uuid_str, data, data_len, user_data):
        this = user_data

        notification_uuid = uuid.UUID(uuid_str)

        short_uuid = notification_uuid.int
        if short_uuid not in this._gatt_characteristic_callbacks:
            raise RuntimeError("UUID '%s' is expected to be part of the notification list")
        else:
            characteristic_callback = this._gatt_characteristic_callbacks[short_uuid]

        # value = bytearray(data_len)
        # for i in range(data_len):
        #    value[i] = data[i]

        pointer_type = POINTER(c_ubyte * data_len)
        c_bytearray = cast(data, pointer_type)

        value = bytearray(data_len)
        for i in range(data_len):
            value[i] = c_bytearray.contents[i]

        # Call GATT characteristic Notification callback
        characteristic_callback['callback'](value, characteristic_callback['user_data'])

    def _notification_init(self):
        if self._is_notification_init:
            return

        self._is_notification_init = True

        gattlib_register_notification(self._connection, Device.notification_callback, self)

    def _notification_add_gatt_characteristic_callback(self, gatt_characteristic, callback, user_data):
        if not self._is_notification_init:
            self._notification_init()

        self._gatt_characteristic_callbacks[gatt_characteristic.short_uuid] = { 'callback': callback, 'user_data': user_data }

    def _notification_remove_gatt_characteristic_callback(self, gatt_characteristic):
        self._gatt_characteristic_callbacks[gatt_characteristic.short_uuid] = None

    def __str__(self):
        name = self._name
        if name:
            return str(name)
        else:
            return str(self._addr)
