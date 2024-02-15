#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

GATTLIB_SUCCESS = 0
GATTLIB_INVALID_PARAMETER = 1
GATTLIB_NOT_FOUND = 2
GATTLIB_ERROR_TIMEOUT = 3
GATTLIB_OUT_OF_MEMORY = 4
GATTLIB_NOT_SUPPORTED = 5
GATTLIB_DEVICE_ERROR = 6
GATTLIB_DEVICE_NOT_CONNECTED = 7

GATTLIB_ERROR_MODULE_MASK      = 0xF0000000
GATTLIB_ERROR_DBUS             = 0x10000000
GATTLIB_ERROR_BLUEZ            = 0x20000000
GATTLIB_ERROR_INTERNAL         = 0x80000000


class GattlibException(Exception):
    pass


class AdapterNotOpened(GattlibException):
    pass


class InvalidParameter(GattlibException):
    pass


class NotFound(GattlibException):
    pass


class OutOfMemory(GattlibException):
    pass


class NotSupported(GattlibException):
    pass

class NotConnected(GattlibException):
    pass

class DeviceError(GattlibException):
    def __init__(self, adapter: str = None, mac_address: str = None) -> None:
        self.adapter = adapter
        self.mac_address = mac_address

    def __str__(self) -> str:
        return f"Error with device {self.mac_address} on adapter {self.adapter}"

class DBusError(GattlibException):
    def __init__(self, domain: int, code: int) -> None:
        self.domain = domain
        self.code = code

    def __str__(self) -> str:
        return f"DBus Error domain={self.domain},code={self.code}"


def handle_return(ret):
    if ret == GATTLIB_INVALID_PARAMETER:
        raise InvalidParameter()
    elif ret == GATTLIB_NOT_FOUND:
        raise NotFound()
    elif ret == GATTLIB_OUT_OF_MEMORY:
        raise OutOfMemory()
    elif ret == GATTLIB_ERROR_TIMEOUT:
        raise TimeoutError()
    elif ret == GATTLIB_NOT_SUPPORTED:
        raise NotSupported()
    elif ret == GATTLIB_DEVICE_ERROR:
        raise DeviceError()
    elif ret == GATTLIB_DEVICE_NOT_CONNECTED:
        raise NotConnected()
    elif (ret & GATTLIB_ERROR_MODULE_MASK) == GATTLIB_ERROR_DBUS:
        raise DBusError((ret >> 8) & 0xFFF, ret & 0xFFFF)
    elif ret == -22: # From '-EINVAL'
        raise ValueError("Gattlib value error")
    elif ret != 0:
        raise RuntimeError("Gattlib exception %d" % ret)
