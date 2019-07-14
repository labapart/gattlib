#!/usr/bin/env python3

import argparse
import threading

from gattlib import adapter

parser = argparse.ArgumentParser(description='Gattlib Find Eddystone device example')
args = parser.parse_args()

EDDYSTONE_COMMON_DATA_UUID = 'FEAA'

EDDYSTONE_URL_SCHEME_PREFIX = {
    0x00: "http://www.",
    0x01: "https://www.",
    0x02: "http://",
    0x03: "https://",
}

# Use default adapter
default_adapter = adapter.Adapter()


def on_discovered_ble_device(device, user_data):
    rssi = default_adapter.get_rssi_from_mac(device.id)
    print("Find Eddystone device %s (RSSI:%d)" % (device.id, rssi))

    # Retrieve Advertisement Data
    advertisement_data, manufacturer_id, manufacturer_data = default_adapter.gattlib_get_advertisement_data_from_mac(device.id)

    # Service Data
    service_data = advertisement_data[0xFEAA]
    if service_data[0] == 0x00:
        print("Eddystone UID: TX Power:0x%x NID:%s BID:%s" % (service_data[1], service_data[2:12], service_data[12:18]))
    elif service_data[0] == 0x10:
        print("Eddystone URL: TX Power:0x%x URL:%s%s" % (service_data[1], EDDYSTONE_URL_SCHEME_PREFIX[service_data[2]], service_data[3:].decode("utf-8")))
    elif service_data[0] == 0x20:
        print("Eddystone TLM")
    elif service_data[0] == 0x30:
        print("Eddystone EID")
    else:
        print("Eddystone frame not supported: 0x%x" % service_data[0])


# Scan for 30 seconds
default_adapter.open()
default_adapter.scan_enable(on_discovered_ble_device, 30, uuids=[EDDYSTONE_COMMON_DATA_UUID])
