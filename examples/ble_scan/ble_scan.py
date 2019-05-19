#!/usr/bin/env python3

# export LD_LIBRARY_PATH=/home/olivier/dev/gattlib/build/dbus/:$LD_LIBRARY_PATH

from gattlib import adapter

adapters = adapter.Adapter.list()
print("BLE Adapters: %s" % adapters)


def on_discovered_device(device):
    print("Discovered '%s'" % device)
    # device.connect()
    # device.discover()


default_adapter = adapter.Adapter()

default_adapter.open()
default_adapter.scan_enable(on_discovered_device, 10)
