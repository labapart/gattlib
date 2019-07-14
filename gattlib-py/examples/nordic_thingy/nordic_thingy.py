#!/usr/bin/env python3

import argparse

from gattlib import device

from environment_service import environment_service
from sound_service import sound_service

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Gattlib example for Nordic Thingy')
    parser.add_argument('mac', type=str, help='Mac Address of the GATT device to connect')
    subparsers = parser.add_subparsers(help='sub-command help')

    environment_parser = subparsers.add_parser('environment', help='Environment Command')
    environment_parser.set_defaults(func=environment_service)

    sound_parser = subparsers.add_parser('sound', help='Sound Command')
    sound_parser.add_argument('--wav', type=str, help='WAV file to play')
    sound_parser.set_defaults(func=sound_service)

    args = parser.parse_args()

    if not hasattr(args, 'func'):
        raise RuntimeError("Please specify the command to launch: 'environment', 'sound'")

    gatt_device = device.Device(adapter=None, addr=args.mac)
    gatt_device.connect()
    gatt_device.discover()

    # Launch the sub-command specific function
    args.func(args, gatt_device)
