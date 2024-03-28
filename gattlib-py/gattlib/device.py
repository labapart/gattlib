#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

from __future__ import annotations
import logging
import uuid
import threading
from typing import TYPE_CHECKING

from gattlib import *
from .exception import handle_return, DeviceError, InvalidParameter
from .gatt import GattService, GattCharacteristic
from .uuid import gattlib_uuid_to_int

if TYPE_CHECKING:
    from .adapter import Adapter

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

    def __init__(self, adapter: Adapter, addr: str, name: str = None):
        self._adapter = adapter
        if type(addr) == str:
            self._addr = addr.encode("utf-8")
        else:
            self._addr = addr
        self._name = name
        self._connection = None
        # We use a lock because on disconnection, we will set self._connection to None
        self._connection_lock = threading.Lock()
        # We use a lock on disconnection to ensure the memory is safely freed
        self._disconnection_lock = threading.Lock()

        self.on_connection_callback = None
        self.on_connection_error_callback = None

        # Keep track if notification handler has been initialized
        self._is_notification_init = False

        # Dictionnary for GATT characteristic callback
        self._gatt_characteristic_callbacks = {}

        # Memory that could be allocated by native gattlib
        self._services_ptr = None
        self._characteristics_ptr = None

    @property
    def mac_address(self) -> str:
        """Return Device MAC Address"""
        return self._addr.decode("utf-8")

    @property
    def connection(self):
        if self._connection:
            return self._connection
        else:
            return c_void_p(None)

    @property
    def is_connected(self) -> bool:
        return (self._connection is not None)

    def connect(self, options=CONNECTION_OPTIONS_LEGACY_DEFAULT):
        def _on_connection(adapter: c_void_p, mac_address: c_char_p, connection: c_void_p, error: c_int, user_data: py_object):
            if error:
                self._connection = None
                self.on_connection_error(error, user_data)
            else:
                self._connection = connection
                self.on_connection(user_data)

        if self._adapter is None:
            adapter = None
        else:
            adapter = self._adapter._adapter

        ret = gattlib_connect(adapter, self._addr, options,
                              gattlib_connected_device_python_callback,
                              gattlib_python_callback_args(_on_connection, self))
        handle_return(ret)

    def on_connection(self, user_data: py_object):
        if self.on_connection_callback:
            self.on_connection_callback(self, user_data)

    def on_connection_error(self, error: c_int, user_data: py_object):
        logger.error("Failed to connect due to error '0x%x'", error)
        if self.on_connection_error_callback:
            self.on_connection_error_callback(self, error, user_data)

    @property
    def rssi(self):
        _rssi = c_int16(0)
        if self._connection:
            ret = gattlib_get_rssi(self._connection, byref(_rssi))
            handle_return(ret)
            return _rssi.value
        else:
            return self._adapter.get_rssi_from_mac(self._addr)

    def register_on_disconnect(self, callback, user_data=None):
        self.disconnection_callback = callback

        def on_disconnection(user_data):
            self._disconnection_lock.acquire()

            if self.disconnection_callback:
                self.disconnection_callback()

            # On disconnection, we do not need the list of GATT services and GATT characteristics
            if self._services_ptr:
                gattlib_free_mem(self._services_ptr)
                self._services_ptr = None
            if self._characteristics_ptr:
                gattlib_free_mem(self._characteristics_ptr)
                self._characteristics_ptr = None

            # Reset the connection handler
            self._connection = None

            self._disconnection_lock.release()

        gattlib_register_on_disconnect(self.connection,
                                       gattlib_disconnected_device_python_callback,
                                       gattlib_python_callback_args(on_disconnection, user_data))

    def disconnect(self, wait_disconnection: bool = False):
        self._connection_lock.acquire()
        try:
            if self._connection:
                ret = gattlib_disconnect(self.connection, wait_disconnection)
                handle_return(ret)
            self._connection = None
        finally:
            self._connection_lock.release()

    def discover(self):
        #
        # Discover GATT Services
        #
        self._services_ptr = POINTER(GattlibPrimaryService)()
        _services_count = c_int(0)
        ret = gattlib_discover_primary(self.connection, byref(self._services_ptr), byref(_services_count))
        handle_return(ret)

        self._services = {}
        for i in range(0, _services_count.value):
            service = GattService(self, self._services_ptr[i])
            self._services[service.short_uuid] = service

            logger.debug("Service UUID:0x%x" % service.short_uuid)

        #
        # Discover GATT Characteristics
        #
        self._characteristics_ptr = POINTER(GattlibCharacteristic)()
        _characteristics_count = c_int(0)
        ret = gattlib_discover_char(self.connection, byref(self._characteristics_ptr), byref(_characteristics_count))
        handle_return(ret)

        self._characteristics = {}
        for i in range(0, _characteristics_count.value):
            characteristic = GattCharacteristic(self, self._characteristics_ptr[i])
            self._characteristics[characteristic.short_uuid] = characteristic

            logger.debug("Characteristic UUID:0x%x" % characteristic.short_uuid)

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

        gattlib_free_mem(_advertisement_data)
        gattlib_free_mem(_manufacturer_data)

        return advertisement_data, _manufacturer_id.value, manufacturer_data

    @property
    def services(self):
        if not hasattr(self, '_services'):
            logger.warning("Start GATT discovery implicitly")
            self.discover()

        return self._services

    @property
    def characteristics(self):
        if not hasattr(self, '_characteristics'):
            logger.warning("Start GATT discovery implicitly")
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

        gattlib_register_notification(self._connection,
                                      gattlib_notification_device_python_callback,
                                      gattlib_python_callback_args(Device.notification_callback, self))

    def _notification_add_gatt_characteristic_callback(self, gatt_characteristic, callback, user_data):
        if not callable(callback):
            raise InvalidParameter("Notification callback is not callable.")

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
