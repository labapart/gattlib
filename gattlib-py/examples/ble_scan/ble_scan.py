#!/usr/bin/env python3

#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

import argparse
import threading
import time

from gattlib import adapter, mainloop

SCAN_TIMEOUT_SEC = 60

parser = argparse.ArgumentParser(description='Gattlib BLE scan example')
parser.add_argument('--duration', default=SCAN_TIMEOUT_SEC, type=int, help='Duration of the BLE scanning')
args = parser.parse_args()

# We only use a lock to not mixed printing statements of various devices
lock = threading.Lock()

def connect_ble_device(device):
    lock.acquire()
    try:
        print("---------------------------------")
        print(f"Found BLE Device {device.mac_address}. Connection tentative...")
        device.connect()

        device.discover()

        for key, val in device.characteristics.items():
            print("- GATTCharacteristic: 0x%x" % key)
        device.disconnect()
    except Exception as e:
        print(f"EXCEPTION: {type(e)}: {str(e)}")
    lock.release()

def on_discovered_ble_device(device, user_data):
    threading.Thread(target=connect_ble_device, args=(device,)).start()

# Use default adapter
default_adapter = adapter.Adapter()

def scan_ble_devices():
    default_adapter.open()
    # Scan for 'args.duration' seconds
    default_adapter.scan_enable(on_discovered_ble_device, timeout=args.duration)

    # Because scan_enable() is not blocking, we need to wait for the same duration
    time.sleep(args.duration)

mainloop.run_mainloop_with(scan_ble_devices)
