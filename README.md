GattLib is a library used to access Generic Attribute Profile (GATT) protocol of BLE (Bluetooth Low Energy) devices.

It is based on Bluez 4.101 GATT code (prior to Bluez D-BUS API).

It has been introduced to allow to build applications hosted on platform with a version of Bluez prior to v5.x that could easily communicate with BLE devices.

Potentially, D-BUS API could also be added to GattLib to provide an abstraction layer between different versions of BlueZ.

Latest GattLib Release packages
===============================

- ZIP: <https://github.com/labapart/gattlib/releases/download/v0.1/gattlib-0.1.zip>
- DEB: <https://github.com/labapart/gattlib/releases/download/v0.1/gattlib-0.1.deb>
- RPM: <https://github.com/labapart/gattlib/releases/download/v0.1/gattlib-0.1.rpm>

Build GattLib
=============

```
cd <gattlib-src-root>
mkdir build && cd build
cmake ..
make
```

Package GattLib
===============

From the build directory: `cpack ..`

**Note:** It generates DEB, RPM and ZIP packages. Ensure you have the expected dependencies
 installed on your system (eg: to generate RPM package on Debian-based Linux distribution
  you must have `rpm` package installed).

Default install directory is defined as /usr by CPack variable `CPACK_PACKAGE_INSTALL_DIRECTORY`.  
To change the install directory to `/usr/local` run: `cpack -DCPACK_PACKAGE_INSTALL_DIRECTORY=/usr/local ..`

Examples
========

* Demonstrate discovering of primary services and characteristics:

        ./examples/discover/discover 78:A5:04:22:45:4F

* Demonstrate characteristic read/write:

        ./examples/read_write/read_write 78:A5:04:22:45:4F read 00002a29-0000-1000-8000-00805f9b34fb
        ./examples/read_write/read_write 78:A5:04:22:45:4F write 00002a6b-0000-1000-8000-00805f9b34fb 0x1234

**Note:** `examples/gatttool` has been partially ported to gattlib. There are two reasons the laziness
 (some of the GATT functions could be replaced by their gattlib equivalent) and the completeness (there
 are still some missing functions in gattlib).

* Notification is also supported. Example:

```
void notification_cb(uint16_t handle, const uint8_t* data, size_t data_length, void* user_data) {
	printf("Notification on handle 0x%02x : ", handle);
}

main() {
	uint16_t status_handle; // Handle of the 'status' characteristic
	uint16_t enable_notification = 0x0001;

	// Enable Status Notification
	gattlib_write_char_by_handle(connection, status_handle + 1, &enable_notification, sizeof(enable_notification));
	// Register notification handler
	gattlib_register_notification(connection, notification_cb, NULL);
}
```

Known limitations
-----------------

* **gattlib and BLE**: gattlib requires at least Bluez v4.100 to work with Bluetooth Low Energy (BLE) devices. Bluez does not allow to connect to BLE device prior to this version. But gattlib can still work with Bluetooth Classic (BR/EDR) prior to Bluez v4.100.  
Debian 7 "Wheezy" (supported until 31st of May 2018) relies on Bluez v4.99 while Debian 8 "Jessie" (supported until April/May 2020) uses Bluez v5.23.

TODO List
=========

- Complete `examples/gatttool` port to GattLib to demonstrate the completeness of GattLib.
- Support Bluez v5.x GATT D-BUS API in addition to the current Bluez v4.101 support.
- Remove GLib dependencies to GattLib (mainly replacing GLib IO Channels by Unix Domain Socket).
