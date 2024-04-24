#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

"""Module to manipulate Gattlib UUID in Python environment."""

import re
from uuid import UUID

from gattlib import *  #pylint: disable=wildcard-import,unused-wildcard-import

SDP_UUID16 = 0x19
SDP_UUID32 = 0x1A
SDP_UUID128 = 0x1C

GATT_STANDARD_UUID_FORMAT = re.compile(r"(\S+)-0000-1000-8000-00805f9b34fb", flags=re.IGNORECASE)


def gattlib_uuid_to_uuid(gattlib_uuid) -> UUID:
    """Convert Gattlib UUID to Python UUID"""
    if gattlib_uuid.type == SDP_UUID16:
        return UUID(fields=(gattlib_uuid.value.uuid16, 0x0000, 0x1000, 0x80, 0x00, 0x00805f9b34fb))
    elif gattlib_uuid.type == SDP_UUID32:
        return UUID(fields=(gattlib_uuid.value.uuid32, 0x0000, 0x1000, 0x80, 0x00, 0x00805f9b34fb))
    elif gattlib_uuid.type == SDP_UUID128:
        data = bytes(gattlib_uuid.value.uuid128.data)
        return UUID(bytes=data)
    else:
        return ValueError(f"Gattlib UUID not recognized (type:0x{gattlib_uuid.type:02x})")


def gattlib_uuid_to_int(gattlib_uuid) -> int:
    """Convert Gattlib UUID to integer."""
    if gattlib_uuid.type == SDP_UUID16:
        return gattlib_uuid.value.uuid16
    elif gattlib_uuid.type == SDP_UUID32:
        return gattlib_uuid.value.uuid32
    elif gattlib_uuid.type == SDP_UUID128:
        data = bytes(gattlib_uuid.value.uuid128.data)
        return int.from_bytes(data, byteorder='big')
    else:
        return ValueError(f"Gattlib UUID not recognized (type:0x{gattlib_uuid.type:02x})")


def gattlib_uuid_str_to_int(uuid_str: str) -> int:
    """Convert uuid string to integer"""
    # Check if the string could already encode a UUID16 or UUID32
    if len(uuid_str) <= 8:
        return int(uuid_str, 16)

    # Check if it is a standard UUID or not
    match = GATT_STANDARD_UUID_FORMAT.search(uuid_str)
    if match:
        return int(match.group(1), 16)
    else:
        try:
            return UUID(uuid_str).int
        except ValueError:
            logger.error("Could not convert %s to a UUID", uuid_str)
            raise
