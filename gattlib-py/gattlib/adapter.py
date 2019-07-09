from gattlib import *
from .device import Device
from .exception import handle_return

GATTLIB_DISCOVER_FILTER_USE_UUID = (1 << 0)
GATTLIB_DISCOVER_FILTER_USE_RSSI = (1 << 1)


class Adapter:

    def __init__(self, name=c_char_p(None)):
        self._name = name
        self._adapter = c_void_p(None)

    @property
    def name(self):
        return self._name

    @staticmethod
    def list():
        # TODO: Add support
        return []

    def open(self):
        return gattlib_adapter_open(self._name, byref(self._adapter))

    def close(self):
        return gattlib.gattlib_adapter_close(self._adapter)

    def on_discovered_device(self, addr, name):
        device = Device(self, addr, name)
        self.on_discovered_device_callback(device)

    def scan_enable(self, on_discovered_device_callback, timeout, uuids=None, rssi_threshold=None):
        assert on_discovered_device_callback != None
        self.on_discovered_device_callback = on_discovered_device_callback

        enabled_filters = 0
        uuid_list = None
        rssi = 0

        if uuids is not None:
            enabled_filters |= GATTLIB_DISCOVER_FILTER_USE_UUID

            # We add 1 to make sure the array finishes with a NULL pointer
            uuid_list = (POINTER(GattlibUuid) * (len(uuids) + 1))()
            index = 0
            for uuid in uuids:
                gattlib_uuid = GattlibUuid()

                uuid_ascii = uuid.encode("utf-8")
                ret = gattlib.gattlib_string_to_uuid(uuid_ascii, len(uuid_ascii), byref(gattlib_uuid))
                handle_return(ret)

                uuid_list[index] = cast(byref(gattlib_uuid), POINTER(GattlibUuid))
                index += 1

        if rssi_threshold is not None:
            enabled_filters |= GATTLIB_DISCOVER_FILTER_USE_RSSI
            rssi = int(rssi_threshold)

        ret = gattlib_adapter_scan_enable_with_filter(self._adapter,
                                                      uuid_list, rssi, enabled_filters,
                                                      gattlib_discovered_device_type(self.on_discovered_device), timeout)
        handle_return(ret)

    def scan_disable(self):
        ret = gattlib.gattlib_adapter_scan_disable(self._adapter)
        handle_return(ret)
