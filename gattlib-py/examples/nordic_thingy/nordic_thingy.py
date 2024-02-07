#!/usr/bin/env python3

#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

import argparse

from gattlib import adapter, device

from environment_service import environment_service
from sound_service import sound_service

# Use default adapter
default_adapter = adapter.Adapter()


def on_thingy_device_found(device, user_data):
    print("Found Nordic Thingy!")

    # We pick-up the first device. Disable scanning
    default_adapter.scan_disable()

    # Connect to the found device
    device.connect()
    device.discover()

    args = user_data
    args.func(args, device)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Gattlib example for Nordic Thingy')
    parser.add_argument('--mac', type=str, help='Mac Address of the GATT device to connect')
    subparsers = parser.add_subparsers(help='sub-command help')

    environment_parser = subparsers.add_parser('environment', help='Environment Command')
    environment_parser.set_defaults(func=environment_service)

    sound_parser = subparsers.add_parser('sound', help='Sound Command')
    sound_parser.add_argument('--wav', type=str, help='WAV file to play')
    sound_parser.set_defaults(func=sound_service)

    args = parser.parse_args()

    if not hasattr(args, 'func'):
        raise RuntimeError("Please specify the command to launch: 'environment', 'sound'")

    if args.mac:
        gatt_device = device.Device(adapter=None, addr=mac)
        gatt_device.connect()
        gatt_device.discover()

        # Launch the sub-command specific function
        args.func(args, gatt_device)
    else:
        default_adapter.open()

        NORDIC_THINGY_CONFIGURATION_SERVICE = "EF680100-9B35-4933-9B10-52FFA9740042"

        default_adapter.scan_enable(on_thingy_device_found, timeout=10,
                                    uuids=[NORDIC_THINGY_CONFIGURATION_SERVICE],
                                    user_data=args)
