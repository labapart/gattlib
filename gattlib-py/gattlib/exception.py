#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

"""Gattlib Exceptions"""

GATTLIB_SUCCESS = 0
GATTLIB_INVALID_PARAMETER = 1
GATTLIB_NOT_FOUND = 2
GATTLIB_TIMEOUT = 3
GATTLIB_OUT_OF_MEMORY = 4
GATTLIB_NOT_SUPPORTED = 5
GATTLIB_DEVICE_ERROR = 6
GATTLIB_DEVICE_NOT_CONNECTED = 7
GATTLIB_NO_ADAPTER = 8
GATTLIB_BUSY = 9
GATTLIB_UNEXPECTED = 10
GATTLIB_ADAPTER_CLOSE = 11
GATTLIB_DEVICE_DISCONNECTED = 12

GATTLIB_ERROR_MODULE_MASK      = 0xF0000000
GATTLIB_ERROR_DBUS             = 0x10000000
GATTLIB_ERROR_BLUEZ            = 0x20000000
GATTLIB_ERROR_INTERNAL         = 0x80000000


class GattlibException(Exception):
    """Generic Gattlib exception."""

class NoAdapter(GattlibException):
    """Gattlib exception raised when no adapter is present."""

class Busy(GattlibException):
    """Gattlib busy exception."""

class Unexpected(GattlibException):
    """Gattlib unexpected exception."""

class AdapterNotOpened(GattlibException):
    """Gattlib exception raised when adapter is not opened yet."""

class InvalidParameter(GattlibException):
    """Gattlib invalid parameter exception."""

class NotFound(GattlibException):
    """Gattlib not found exception."""

class OutOfMemory(GattlibException):
    """Gattlib out of memory exception."""

class NotSupported(GattlibException):
    """Gattlib not supported exception."""

class NotConnected(GattlibException):
    """Gattlib exception raised when device is not connected."""

class AdapterClose(GattlibException):
    """Gattlib exception raised when the adapter is closed."""

class Disconnected(GattlibException):
    """Gattlib exception raised when the device is disconnected."""

class DeviceError(GattlibException):
    """Gattlib device exception."""
    def __init__(self, adapter: str = None, mac_address: str = None) -> None:
        self.adapter = adapter
        self.mac_address = mac_address

    def __str__(self) -> str:
        return f"Error with device {self.mac_address} on adapter {self.adapter}"

class DBusError(GattlibException):
    """Gattlib DBUS exception."""
    def __init__(self, domain: int, code: int) -> None:
        self.domain = domain
        self.code = code

    def __str__(self) -> str:
        if self.domain == 238 and self.code == 60964:
            return "DBus Error: le-connection-abort-by-local"
        elif self.domain == 238 and self.code == 60952:
            return "DBus Error: Timeout was reached"
        elif self.domain == 238 and self.code == 60964:
            return "DBus Error: Timeout was reached"
        else:
            return f"DBus Error domain={self.domain},code={self.code}"

def handle_return(ret):
    """Function to convert gattlib error to Python exception."""
    if ret == GATTLIB_INVALID_PARAMETER:
        raise InvalidParameter()
    if ret == GATTLIB_NOT_FOUND:
        raise NotFound()
    if ret == GATTLIB_OUT_OF_MEMORY:
        raise OutOfMemory()
    if ret == GATTLIB_TIMEOUT:
        raise TimeoutError()
    if ret == GATTLIB_NOT_SUPPORTED:
        raise NotSupported()
    if ret == GATTLIB_DEVICE_ERROR:
        raise DeviceError()
    if ret == GATTLIB_DEVICE_NOT_CONNECTED:
        raise NotConnected()
    if ret == GATTLIB_NO_ADAPTER:
        raise NoAdapter()
    if ret == GATTLIB_BUSY:
        raise Busy()
    if ret == GATTLIB_UNEXPECTED:
        raise Unexpected()
    if ret == GATTLIB_ADAPTER_CLOSE:
        raise AdapterClose()
    if ret == GATTLIB_DEVICE_DISCONNECTED:
        raise Disconnected()
    if (ret & GATTLIB_ERROR_MODULE_MASK) == GATTLIB_ERROR_DBUS:
        raise DBusError((ret >> 8) & 0xFFF, ret & 0xFFFF)
    if ret == -22: # From '-EINVAL'
        raise ValueError("Gattlib value error")
    if ret != 0:
        raise RuntimeError(f"Gattlib exception {ret}")
