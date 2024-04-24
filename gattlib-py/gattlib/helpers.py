#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2024, Olivier Martin <olivier@labapart.org>
#

"""Module for helper functions for Gattlib module."""

from gattlib import *  #pylint: disable=wildcard-import,unused-wildcard-import
from .uuid import gattlib_uuid_to_int

def convert_gattlib_advertisement_c_data_to_dict(advertisement_c_data, advertisement_c_data_count,
                                                 manufacturer_c_data, manufacturer_c_data_count):
    """Helper function to convert advertisement and manufacturer c-data to Python dictionary"""
    advertisement_data = {}
    manufacturer_data = {}

    for i in range(0, advertisement_c_data_count.value):
        service_data = advertisement_c_data[i]
        uuid = gattlib_uuid_to_int(service_data.uuid)

        pointer_type = POINTER(c_byte * service_data.data_length)
        c_bytearray = cast(service_data.data, pointer_type)

        data = bytearray(service_data.data_length)
        for i in range(service_data.data_length):
            data[i] = c_bytearray.contents[i] & 0xFF

        advertisement_data[uuid] = data
        gattlib_free_mem(service_data.data)

    for i in range(0, manufacturer_c_data_count.value):
        _manufacturer_c_data = manufacturer_c_data[i]

        pointer_type = POINTER(c_byte * _manufacturer_c_data.data_size.value)
        c_bytearray = cast(_manufacturer_c_data.data, pointer_type)

        data = bytearray(_manufacturer_c_data.data_size.value)
        for j in range(_manufacturer_c_data.data_size.value):
            data[j] = c_bytearray.contents[j] & 0xFF

        manufacturer_data[_manufacturer_c_data.manufacturer_id] = data

        gattlib_free_mem(_manufacturer_c_data.data)

    gattlib_free_mem(advertisement_c_data)
    gattlib_free_mem(manufacturer_c_data)

    return advertisement_data, manufacturer_data
