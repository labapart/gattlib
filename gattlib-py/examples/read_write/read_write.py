#!/usr/bin/env python3

#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

#
# Example: Read battery level: ./gattlib-py/examples/read_write/read_write.py D2:9A:97:1E:86:C8 read 00002A19–0000–1000–8000–00805f9b34fb
#

import argparse
import logging
import time

from gattlib import adapter, device, mainloop, uuid

SCAN_TIMEOUT_SEC = 30

parser = argparse.ArgumentParser(description='Gattlib read_write example')
parser.add_argument('mac', type=str, help='Mac Address of the GATT device to connect')
parser.add_argument('action', choices=['read', 'write'], help='Tell if we want to read/write the GATT characteristic')
parser.add_argument('uuid', type=str, help='UUID of the GATT Characteristic')
parser.add_argument('value', type=str, nargs='?', help='Value to write to the GATT characteristic')
args = parser.parse_args()

default_adapter = adapter.Adapter()

def on_connected_ble_device(device: device.Device, user_data):
    uuid_value = uuid.gattlib_uuid_str_to_int(args.uuid)
    if uuid_value not in device.characteristics:
        exception_str = f"Failed to find GATT characteristic '{args.uuid}' in {device.characteristics}"
        device.disconnect()
        raise RuntimeError(exception_str)

    characteristic = device.characteristics[uuid_value]

    if args.action == "read":
        value = characteristic.read()
        print(value)
    elif args.action == "write":
        characteristic.write(args.value)

    device.disconnect()

def on_connection_error_callback(device: device.Device, error: int, user_data):
    logging.error("Device '%s' connection error: %d", device, error)

def on_discovered_ble_device(device: device.Device, user_data):
    if device.mac_address != args.mac:
        return

    default_adapter.scan_disable()

    device.on_connection_callback = on_connected_ble_device
    device.on_connection_error_callback = on_connection_error_callback
    device.connect()

def scan_ble_devices():
    default_adapter.open()
    default_adapter.scan_enable(on_discovered_ble_device, timeout=SCAN_TIMEOUT_SEC)
    time.sleep(SCAN_TIMEOUT_SEC)

mainloop.run_mainloop_with(scan_ble_devices)
