#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

from gattlib import *
from .uuid import gattlib_uuid_to_uuid, gattlib_uuid_to_int
from .exception import handle_return, InvalidParameter


class GattStream():

    def __init__(self, fd, mtu):
        self._fd = fd
        self._mtu = mtu

    @property
    def mtu(self):
        # Remove ATT Header (3 bytes)
        return self._mtu - 3

    def write(self, data, mtu=None):
        if mtu is None:
            mtu = self.mtu

        while len(data) > 0:
            frame = data[0:mtu]
            data = data[mtu:]

            buffer_type = c_char * len(frame)
            buffer = frame
            buffer_len = len(frame)

            gattlib.gattlib_write_char_stream_write(self._fd, buffer_type.from_buffer_copy(buffer), buffer_len)

    def close(self):
        gattlib.gattlib_write_char_stream_close(self._fd)


class GattService():

    def __init__(self, device, gattlib_primary_service):
        self._device = device
        self._gattlib_primary_service = gattlib_primary_service

    @property
    def uuid(self):
        return gattlib_uuid_to_uuid(self._gattlib_primary_service.uuid)

    @property
    def short_uuid(self):
        return gattlib_uuid_to_int(self._gattlib_primary_service.uuid)


class GattCharacteristic():

    def __init__(self, device, gattlib_characteristic):
        self._device = device
        self._gattlib_characteristic = gattlib_characteristic

    @property
    def uuid(self):
        return gattlib_uuid_to_uuid(self._gattlib_characteristic.uuid)

    @property
    def short_uuid(self):
        return gattlib_uuid_to_int(self._gattlib_characteristic.uuid)

    @property
    def connection(self):
        return self._device.connection

    def read(self, callback=None):
        if callback:
            raise NotImplementedError()
        else:
            _buffer = c_void_p(None)
            _buffer_len = c_size_t(0)

            ret = gattlib_read_char_by_uuid(self.connection, self._gattlib_characteristic.uuid, byref(_buffer), byref(_buffer_len))
            handle_return(ret)

            pointer_type = POINTER(c_ubyte * _buffer_len.value)
            c_bytearray = cast(_buffer, pointer_type)

            value = bytearray(_buffer_len.value)
            for i in range(_buffer_len.value):
                value[i] = c_bytearray.contents[i]

            gattlib_characteristic_free_value(_buffer)
            return value

    def write(self, data, without_response=False):
        if not isinstance(data, bytes) and not isinstance(data, bytearray):
            raise TypeError("Data must be of bytes type to know its size.")

        buffer_type = c_char * len(data)
        buffer = data
        buffer_len = len(data)

        if without_response:
            ret = gattlib_write_without_response_char_by_uuid(self.connection, self._gattlib_characteristic.uuid, buffer_type.from_buffer_copy(buffer), buffer_len)
        else:
            ret = gattlib_write_char_by_uuid(self.connection, self._gattlib_characteristic.uuid, buffer_type.from_buffer_copy(buffer), buffer_len)
        handle_return(ret)

    def stream_open(self):
        _stream = c_void_p(None)
        _mtu = c_uint16(0)

        ret = gattlib_write_char_by_uuid_stream_open(self.connection, self._gattlib_characteristic.uuid, byref(_stream), byref(_mtu))
        handle_return(ret)

        return GattStream(_stream, _mtu.value)

    def register_notification(self, callback, user_data=None):
        if not callable(callback):
            raise InvalidParameter("Notification callback is not callable.")

        self._device._notification_add_gatt_characteristic_callback(self, callback, user_data)

    def unregister_notification(self):
        self._device._notification_remove_gatt_characteristic_callback(self)

    def notification_start(self):
        ret = gattlib_notification_start(self.connection, self._gattlib_characteristic.uuid)
        handle_return(ret)

    def notification_stop(self):
        """ Could raise gattlib.exception.NotFound if notification has not been registered"""
        ret = gattlib_notification_stop(self.connection, self._gattlib_characteristic.uuid)
        handle_return(ret)

    def __str__(self):
        return str(self.uuid)
