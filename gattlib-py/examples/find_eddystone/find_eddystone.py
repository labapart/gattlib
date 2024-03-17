#!/usr/bin/env python3

#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

import argparse
import time

from gattlib import adapter, mainloop

SCAN_TIMEOUT_SEC = 60

parser = argparse.ArgumentParser(description='Gattlib Find Eddystone device example')
parser.add_argument('--duration', default=SCAN_TIMEOUT_SEC, type=int, help='Duration of the BLE scanning')
args = parser.parse_args()


def on_eddystone_device_found(device, advertisement_data, manufacturer_id, manufacturer_data, user_data):
    rssi = default_adapter.get_rssi_from_mac(device.mac_address)
    print("Find Eddystone device %s (RSSI:%d)" % (device.mac_address, rssi))

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

# Use default adapter
default_adapter = adapter.Adapter()

def scan_ble_devices():
    default_adapter.open()
    # Scan for 'args.duration' seconds
    default_adapter.scan_eddystone_enable(on_eddystone_device_found,
                                          adapter.GATTLIB_EDDYSTONE_TYPE_UID | adapter.GATTLIB_EDDYSTONE_TYPE_URL | adapter.GATTLIB_EDDYSTONE_TYPE_TLM | adapter.GATTLIB_EDDYSTONE_TYPE_EID,
                                          args.duration)

    # Because scan_enable() is not blocking, we need to wait for the same duration
    time.sleep(args.duration)

mainloop.run_mainloop_with(scan_ble_devices)
