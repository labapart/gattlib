from gattlib import *
from .device import Device
from .exception import handle_return

class Adapter:
    def __init__(self, name=c_char_p(None)):
        self._name = name
        self._adapter = c_void_p(None)

    @property
    def name(self):
        return self._name

    @staticmethod
    def list():
        #TODO: Add support
        return []

    def open(self):
        return gattlib_adapter_open(self._name, byref(self._adapter))

    def close(self):
        return gattlib.gattlib_adapter_close(self._adapter)

    def on_discovered_device(self, addr, name):
        device = Device(self, addr, name)
        self.on_discovered_device_callback(device)

    def scan_enable(self, on_discovered_device_callback, timeout):
        assert on_discovered_device_callback != None
        self.on_discovered_device_callback = on_discovered_device_callback

        ret = gattlib.gattlib_adapter_scan_enable(self._adapter, gattlib_discovered_device_type(self.on_discovered_device), timeout)
        handle_return(ret)

    def scan_disable(self):
        ret = gattlib.gattlib_adapter_scan_disable(self._adapter)
        handle_return(ret)
