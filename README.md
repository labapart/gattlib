GattLib is a library used to access Generic Attribute Profile (GATT) protocol of BLE (Bluetooth Low Energy) devices.
It has been introduced to allow to build applications that could easily communicate with BLE devices.

It supports Bluez v4 and v5.

Latest GattLib Release packages
===============================

* The latest release can be found [here](https://github.com/labapart/gattlib/releases/latest). It contains:

- Prebuilt Debian, RPM and ZIP packages for x86_64 and Bluez v5.x
- Packages for ARM 32bit and 64bit would have to be built by the developer - see section [Package GattLib](#package-gattlib).

- Prebuilt Python packages are available on [Pypi repository](https://pypi.org/project/gattlib-py/).

Build GattLib
=============

* Gattlib requires the following packages: `libbluetooth-dev`, `libreadline-dev`.  
On Debian based system (such as Ubuntu), you can installed these packages with the
following command: `sudo apt install libbluetooth-dev libreadline-dev`

```
cd <gattlib-src-root>
mkdir build && cd build
cmake ..
make
```

* Gattlib can also be built for a specific version of Bluez by specifying its version at build time:

```
mkdir build && cd build
cmake -DBLUEZ_VERSION=5.50 ..
make
```


* **On Bluez versions prior to v5.42**, gattlib used Bluez source code while it uses D-Bus API 
from v5.42. D-Bus API can be used on version prior to Bluez v5.42 by using the CMake flag `-DGATTLIB_FORCE_DBUS=TRUE`:

```
mkdir build && cd build
cmake -DGATTLIB_FORCE_DBUS=TRUE ..
make
```

### Cross-Compilation

To cross-compile GattLib, you must provide the following environment variables:

- `CROSS_COMPILE`: prefix of your cross-compile toolchain
- `SYSROOT`: an existing system root that contains the libraries and include files required by your application

Example:

```
cd <gattlib-src-root>
mkdir build && cd build
export CROSS_COMPILE=~/Toolchains/gcc-linaro-4.9-2015.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-
export SYSROOT=~/Distributions/debian-wheezy
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

* [Demonstrate discovering of primary services and characteristics](/examples/discover/discover.c):

        ./examples/discover/discover 78:A5:04:22:45:4F

* [Demonstrate characteristic read/write](/examples/read_write/read_write.c):

        ./examples/read_write/read_write 78:A5:04:22:45:4F read 00002a29-0000-1000-8000-00805f9b34fb
        ./examples/read_write/read_write 78:A5:04:22:45:4F write 00002a6b-0000-1000-8000-00805f9b34fb 0x1234

* [Demonstrate BLE scanning and connection](/examples/ble_scan/ble_scan.c):

        ./examples/ble_scan/ble_scan

* [Demonstrate GATT notification using GATT Battery service](/examples/notification/notification.c):

        ./examples/notification/notification

* [Demonstrate GATT Write Without Response](/examples/nordic_uart/nordic_uart.c):

        ./examples/nordic_uart/nordic_uart

**Note 1:** [The example 'read/write mem'](/examples/read_write_mem/read_write.c) is similar to
[the example 'read/write'](/examples/read_write/read_write.c) except a GLib loop is used to allows
the memory to be freed by Glib. Without this loop, some memory could be locked.

**Note 2:** `examples/gatttool` has been partially ported to gattlib. There are two reasons: the laziness
 (some of the GATT functions could be replaced by their gattlib equivalent) and the completeness (there
 are still some missing functions in gattlib).

* Notification is also supported. Example:

```
void notification_cb(uint16_t handle, const uint8_t* data, size_t data_length, void* user_data) {
	printf("Notification on handle 0x%02x\n", handle);
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
- Remove GLib dependencies to GattLib (mainly replacing GLib IO Channels by Unix Domain Socket).

License
=======

Gattlib with Bluez Legacy support (for Bluez v4) has a GPL v2.0 or later license.  
While Gattlib for recent version of Bluez (v5.40+) has a BSD-3-Clause license - except `dbus/bluez5/lib/uuid.c`
and `dbus/bluez5/lib/uuid.h` that have a GPL v2.0 or later license.

Support
=======

Commercial Support can be obtained through [Lab A Part](https://labapart.com). Please contact us: [https://labapart.com/about/](https://labapart.com/about/).
