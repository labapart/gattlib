#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -ex

# Install dependencies
yum -y install wget dbus-devel

#
#
#
wget http://www.kernel.org/pub/linux/bluetooth/bluez-5.66.tar.xz
tar -xf bluez-5.66.tar.xz
pushd bluez-5.66
./configure --prefix=/usr/local --disable-obex --disable-udev --disable-cups --disable-client --disable-manpages --disable-tools \
    --disable-obex --disable-monitor --disable-hog --disable-hid --disable-network --disable-a2dp --disable-avrcp --disable-bap \
    --disable-mcp --disable-vcp --enable-library
make
make install
popd
rm -Rf bluez-5.66
