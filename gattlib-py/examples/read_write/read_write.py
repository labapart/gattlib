#!/usr/bin/env python3

#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2021, Olivier Martin <olivier@labapart.org>
#

import argparse

from gattlib import device, uuid

parser = argparse.ArgumentParser(description='Gattlib read_write example')
parser.add_argument('mac', type=str, help='Mac Address of the GATT device to connect')
parser.add_argument('action', choices=['read', 'write'], help='Tell if we want to read/write the GATT characteristic')
parser.add_argument('uuid', type=str, help='UUID of the GATT Characteristic')
parser.add_argument('value', type=str, nargs='?', help='Value to write to the GATT characteristic')
args = parser.parse_args()

gatt_device = device.Device(adapter=None, addr=args.mac)
gatt_device.connect()

uuid = uuid.gattlib_uuid_str_to_int(args.uuid)
if uuid not in gatt_device.characteristics:
    raise RuntimeError("Failed to find GATT characteristic '%s'" % args.uuid)

characteristic = gatt_device.characteristics[uuid]

if args.action == "read":
    value = characteristic.read()
    print(value)
elif args.action == "write":
    characteristic.write(value)

gatt_device.disconnect()
