#!/usr/bin/env python3

import argparse

from gattlib import adapter

parser = argparse.ArgumentParser(description='Gattlib BLE Advertising Data example')
args = parser.parse_args()


def on_discovered_ble_device(device, user_data):
    advertisement_data, manufacturer_id, manufacturer_data = device.get_advertisement_data()
    print("Device Advertisement Data: %s" % manufacturer_data)


# Use default adapter
default_adapter = adapter.Adapter()

# Scan indefinitely
default_adapter.open()
default_adapter.scan_enable(on_discovered_ble_device, 0, notify_change=True)
