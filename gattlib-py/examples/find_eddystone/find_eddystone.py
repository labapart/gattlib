#!/usr/bin/env python3

import argparse
import threading

from gattlib import adapter

parser = argparse.ArgumentParser(description='Gattlib Find Eddystone device example')
args = parser.parse_args()

EDDYSTONE_COMMON_DATA_UUID = 'FEAA'

# Use default adapter
default_adapter = adapter.Adapter()


def on_discovered_ble_device(device, user_data):
    rssi = default_adapter.get_rssi_from_mac(device.id)
    print("Find Eddystone device %s (RSSI:%d)" % (device.id, rssi))


# Scan for 30 seconds
default_adapter.open()
default_adapter.scan_enable(on_discovered_ble_device, 30, uuids=[EDDYSTONE_COMMON_DATA_UUID])
