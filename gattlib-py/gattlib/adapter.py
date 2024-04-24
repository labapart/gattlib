#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

"""Gattlib Adapter API"""

import threading
from uuid import UUID

from gattlib import *  #pylint: disable=wildcard-import,unused-wildcard-import
from .device import Device
from .exception import handle_return
from .helpers import convert_gattlib_advertisement_c_data_to_dict

GATTLIB_DISCOVER_FILTER_USE_UUID = (1 << 0)
GATTLIB_DISCOVER_FILTER_USE_RSSI = (1 << 1)
GATTLIB_DISCOVER_FILTER_NOTIFY_CHANGE = (1 << 2)

GATTLIB_EDDYSTONE_TYPE_UID = (1 << 0)
GATTLIB_EDDYSTONE_TYPE_URL = (1 << 1)
GATTLIB_EDDYSTONE_TYPE_TLM = (1 << 2)
GATTLIB_EDDYSTONE_TYPE_EID = (1 << 3)
GATTLIB_EDDYSTONE_LIMIT_RSSI = (1 << 4)

EDDYSTONE_TYPE_UID = 0x00
EDDYSTONE_TYPE_URL = 0x10
EDDYSTONE_TYPE_TLM = 0x20
EDDYSTONE_TYPE_EID = 0x30

EDDYSTONE_COMMON_DATA_UUID = 0xFEAA

EDDYSTONE_URL_SCHEME_PREFIX = {
    0x00: "http://www.",
    0x01: "https://www.",
    0x02: "http://",
    0x03: "https://",
}


class Adapter:
    """Bluetooth adapter."""
    def __init__(self, name=c_char_p(None)):
        self._name = name
        self._adapter = c_void_p(None)
        self._is_opened = False  # Note: 'self._adapter != c_void_p(None)' does not seem to return the expected result
        self._lock = threading.Lock()
        self._on_discovered_device_callback = None
        self._on_discovered_device_user_callback = None

    def __str__(self) -> str:
        if self._name:
            return self._name
        else:
            return f"adapter@{self._adapter}"

    @property
    def name(self):
        """Return adapter name."""
        return self._name

    #@staticmethod
    #def list():
    #    # TODO: Add support
    #    return []

    def open(self):
        """Open adapter."""
        with self._lock:
            if self._is_opened:
                return

            self._adapter = c_void_p(None)
            ret = gattlib_adapter_open(self._name, byref(self._adapter))
            if ret == 0:
                self._is_opened = True
                if self._name is None:
                    self._name = gattlib_adapter_get_name(self._adapter)
            else:
                handle_return(ret)

    def close(self):
        """Close adapter."""
        with self._lock:
            if self._adapter:
                ret = gattlib.gattlib_adapter_close(self._adapter)
                handle_return(ret)
            self._is_opened = False
            self._adapter = None

    # Use a closure to return a method that can be called by the C-library (see: https://stackoverflow.com/a/7261524/6267288)
    def get_on_discovered_device_callback(self):
        """Return a callback for newly discovered device."""
        def on_discovered_device(adapter, addr, name, user_data):
            try:
                device = Device(self, addr, name)
                self._on_discovered_device_user_callback(device, user_data)
            except Exception as e:  #pylint: disable=broad-exception-caught
                logger.exception(e)
        return on_discovered_device

    def scan_enable(self, on_discovered_device_callback, timeout, notify_change=False, uuids=None, rssi_threshold=None, user_data=None):
        """
        Scan for BLE devices

        Note: This function is not blocking

        @param adapter: is the context of the newly opened adapter
        @param uuid_list: is a NULL-terminated list of UUIDs to filter. The rule only applies to advertised UUID.
               Returned devices would match any of the UUIDs of the list.
        @param rssi_threshold: is the imposed RSSI threshold for the returned devices.
        @param enabled_filters: defines the parameters to use for filtering. There are selected by using the macros
               GATTLIB_DISCOVER_FILTER_USE_UUID and GATTLIB_DISCOVER_FILTER_USE_RSSI.
        @param discovered_device_cb: is the function callback called for each new Bluetooth device discovered
        @param timeout: defines the duration of the Bluetooth scanning. When timeout=None or 0, we scan indefinitely.
        @param user_data: is the data passed to the callback `discovered_device_cb()`
        """
        assert on_discovered_device_callback is not None
        self._on_discovered_device_user_callback = on_discovered_device_callback
        # Save callback to prevent it to be cleaned by garbage collector see comment:
        # https://stackoverflow.com/questions/7259794/how-can-i-get-methods-to-work-as-callbacks-with-python-ctypes#comment38658391_7261524
        self._on_discovered_device_callback = self.get_on_discovered_device_callback()

        # Ensure BLE adapter it opened
        if not self._is_opened:
            self.open()

        enabled_filters = 0
        uuid_list = None
        rssi = 0

        if uuids is not None:
            enabled_filters |= GATTLIB_DISCOVER_FILTER_USE_UUID

            # We add 1 to make sure the array finishes with a NULL pointer
            uuid_list = (POINTER(GattlibUuid) * (len(uuids) + 1))()
            index = 0
            for uuid in uuids:
                gattlib_uuid = GattlibUuid()

                if isinstance(uuid, UUID):
                    uuid_ascii = str(uuid).encode("utf-8")
                else:
                    uuid_ascii = uuid.encode("utf-8")

                ret = gattlib.gattlib_string_to_uuid(uuid_ascii, len(uuid_ascii), byref(gattlib_uuid))
                handle_return(ret)

                uuid_list[index] = cast(byref(gattlib_uuid), POINTER(GattlibUuid))
                index += 1

        if rssi_threshold is not None:
            enabled_filters |= GATTLIB_DISCOVER_FILTER_USE_RSSI
            rssi = int(rssi_threshold)

        if notify_change:
            enabled_filters |= GATTLIB_DISCOVER_FILTER_NOTIFY_CHANGE

        # gattlib_adapter_scan_enable_with_filter_non_blocking() assumes a 0-timeout means scanning indefintely
        if timeout is None:
            timeout = 0

        ret = gattlib_adapter_scan_enable_with_filter_non_blocking(self._adapter,
                                                      uuid_list, rssi, enabled_filters,
                                                      gattlib_discovered_device_python_callback,
                                                      timeout,
                                                      gattlib_python_callback_args(self._on_discovered_device_callback, user_data))
        handle_return(ret)

    @staticmethod
    def on_discovered_ble_device_with_details(adapter, mac_addr, name, advertisement_data_buffer, advertisement_data_count,
                                              manufacturer_data_buffer, manufacturer_data_count,
                                              user_data):
        """Callback invoked when a new device has been discovered."""
        advertisement_data, manufacturer_data = convert_gattlib_advertisement_c_data_to_dict(
                advertisement_data_buffer, advertisement_data_count,
                manufacturer_data_buffer, manufacturer_data_count)

        device = Device(user_data["adapter"], mac_addr, name)
        user_data["callback"](device, advertisement_data, manufacturer_data, user_data["user_data"])

    def scan_eddystone_enable(self, on_discovered_device_callback, eddystone_filters, timeout, rssi_threshold=None, user_data=None):
        """Enable BLE scan for Eddystone devices."""
        # Ensure BLE adapter it opened
        if not self._is_opened:
            self.open()

        rssi = 0

        if rssi_threshold is not None:
            eddystone_filters |= GATTLIB_EDDYSTONE_LIMIT_RSSI
            rssi = int(rssi_threshold)

        args = {
            "adapter": self,
            "callback": on_discovered_device_callback,
            "user_data": user_data
        }

        ret = gattlib_adapter_scan_eddystone(self._adapter, rssi, eddystone_filters,
                                             Adapter.on_discovered_ble_device_with_details,
                                             timeout, args)
        handle_return(ret)

    def scan_disable(self):
        """Disable BLE scan."""
        ret = gattlib.gattlib_adapter_scan_disable(self._adapter)
        handle_return(ret)

    def get_rssi_from_mac(self, mac_address):
        """
        Return RSSI of a device defined by its MAC address.

        Note: The RSSI is 0 when the device is connected.
        """
        if isinstance(mac_address, str):
            mac_address = mac_address.encode("utf-8")

        rssi = c_int16(0)
        gattlib_get_rssi_from_mac(self._adapter, mac_address, byref(rssi))
        return rssi.value

    def gattlib_get_advertisement_data_from_mac(self, mac_address):
        """Return advertisement and manufacturer data of the device."""
        if isinstance(mac_address, str):
            mac_address = mac_address.encode("utf-8")

        _advertisement_data = POINTER(GattlibAdvertisementData)()
        _advertisement_data_count = c_size_t(0)
        _manufacturer_data = POINTER(GattlibManufacturerData)()
        _manufacturer_data_count = c_size_t(0)

        ret = gattlib_get_advertisement_data_from_mac(self._adapter, mac_address,
                                                      byref(_advertisement_data), byref(_advertisement_data_count),
                                                      byref(_manufacturer_data), byref(_manufacturer_data_count))
        handle_return(ret)

        return convert_gattlib_advertisement_c_data_to_dict(
            _advertisement_data, _advertisement_data_count, _manufacturer_data, _manufacturer_data_count)
