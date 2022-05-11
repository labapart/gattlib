#!/usr/bin/env python3

#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2022, Olivier Martin <olivier@labapart.org>
#

import argparse
import threading

from gattlib import adapter

parser = argparse.ArgumentParser(description='Gattlib BLE scan example')
args = parser.parse_args()

# We only use a lock to not mixed printing statements of various devices
lock = threading.Lock()


def connect_ble_device(device):
    device.connect()

    lock.acquire()

    print("---------------------------------")
    print(f"Found BLE Device {device.mac_address}. Connection tentative...")
    device.discover()

    for key, val in device.characteristics.items():
        print("- GATTCharacteristic: 0x%x" % key)

    lock.release()

    device.disconnect()


def on_discovered_ble_device(device, user_data):
    threading.Thread(target=connect_ble_device, args=(device,)).start()


# Use default adapter
default_adapter = adapter.Adapter()

# Scan for 30 seconds
default_adapter.open()
default_adapter.scan_enable(on_discovered_ble_device, 30)
