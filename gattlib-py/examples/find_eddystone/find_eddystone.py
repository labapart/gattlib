#!/usr/bin/env python3

import argparse
import threading

from gattlib import adapter

parser = argparse.ArgumentParser(description='Gattlib Find Eddystone device example')
args = parser.parse_args()

# Use default adapter
default_adapter = adapter.Adapter()


def on_eddystone_device_found(device, advertisement_data, manufacturer_id, manufacturer_data, user_data):
    rssi = default_adapter.get_rssi_from_mac(device.id)
    print("Find Eddystone device %s (RSSI:%d)" % (device.id, rssi))

    # Service Data
    eddystone_data = advertisement_data[adapter.EDDYSTONE_COMMON_DATA_UUID]
    if eddystone_data[0] == adapter.EDDYSTONE_TYPE_UID:
        print("Eddystone UID: TX Power:0x%x NID:%s BID:%s" % (eddystone_data[1], eddystone_data[2:12], eddystone_data[12:18]))
    elif eddystone_data[0] == adapter.EDDYSTONE_TYPE_URL:
        print("Eddystone URL: TX Power:0x%x URL:%s%s" % (eddystone_data[1], adapter.EDDYSTONE_URL_SCHEME_PREFIX[eddystone_data[2]], eddystone_data[3:].decode("utf-8")))
    elif eddystone_data[0] == adapter.EDDYSTONE_TYPE_TLM:
        print("Eddystone TLM")
    elif eddystone_data[0] == adapter.EDDYSTONE_TYPE_EID:
        print("Eddystone EID")
    else:
        print("Eddystone frame not supported: 0x%x" % eddystone_data[0])


# Scan for 30 seconds
default_adapter.open()
default_adapter.scan_eddystone_enable(on_eddystone_device_found,
                                      adapter.GATTLIB_EDDYSTONE_TYPE_UID | adapter.GATTLIB_EDDYSTONE_TYPE_URL | adapter.GATTLIB_EDDYSTONE_TYPE_TLM | adapter.GATTLIB_EDDYSTONE_TYPE_EID,
                                      30)  # Look for 30 seconds
