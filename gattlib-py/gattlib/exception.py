#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2022, Olivier Martin <olivier@labapart.org>
#

GATTLIB_SUCCESS = 0
GATTLIB_INVALID_PARAMETER = 1
GATTLIB_NOT_FOUND = 2
GATTLIB_OUT_OF_MEMORY = 3
GATTLIB_NOT_SUPPORTED = 4
GATTLIB_DEVICE_ERROR = 5
GATTLIB_ERROR_DBUS = 6


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


class DeviceError(GattlibException):
    pass


class DBusError(GattlibException):
    pass


def handle_return(ret):
    if ret == GATTLIB_INVALID_PARAMETER:
        raise InvalidParameter()
    elif ret == GATTLIB_NOT_FOUND:
        raise NotFound()
    elif ret == GATTLIB_OUT_OF_MEMORY:
        raise OutOfMemory()
    elif ret == GATTLIB_NOT_SUPPORTED:
        raise NotSupported()
    elif ret == GATTLIB_DEVICE_ERROR:
        raise DeviceError()
    elif ret == GATTLIB_ERROR_DBUS:
        raise DBusError()
    elif ret == -22: # From '-EINVAL'
        raise ValueError("Gattlib value error")
    elif ret != 0:
        raise RuntimeError("Gattlib exception %d" % ret)
