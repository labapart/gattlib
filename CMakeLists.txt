#
#  GattLib - GATT Library
#
#  Copyright (C) 2016-2017  Olivier Martin <olivier@labapart.org>
#
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#

cmake_minimum_required(VERSION 2.6)

# Add Cross-Compilation support when the environment variables
# CROSS_COMPILE and SYSROOT are defined
include(CrossCompilation.cmake)

project(gattlib)

set(GATTLIB_DBUS TRUE CACHE BOOLEAN "Build gattlib with D-Bus support")

find_package(PkgConfig REQUIRED)

# Show all the warnings
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
# Expose 'gattlib.h' to all sub-directories
include_directories(include)

if (GATTLIB_DBUS)
  # Build dbus-based gattlib
  add_subdirectory(dbus)
else()
  # Build bluez-based gattlib
  add_subdirectory(bluez)
endif()

# Generate pkg-config file before building the examples
configure_file(dbus/gattlib.pc.in ${PROJECT_BINARY_DIR}/gattlib.pc @ONLY)

# Add the build directory to PKG_CONFIG_PATH
set(ENV{PKG_CONFIG_PATH} "${PROJECT_BINARY_DIR}:$ENV{PKG_CONFIG_PATH}")

# Examples
add_subdirectory(examples/ble_scan)
add_subdirectory(examples/discover)
add_subdirectory(examples/read_write)
add_subdirectory(examples/notification)
add_subdirectory(examples/nordic_uart)

# Some examples require Bluez code and other DBus support
if (NOT GATTLIB_DBUS)
  add_subdirectory(examples/gatttool)
endif()

#
# Packaging
#
set(CPACK_PACKAGE_INSTALL_DIRECTORY /usr CACHE STRING "Install directory (default: /usr).")
set(CPACK_PACKAGE_VERSION 0.2)
set(CPACK_PACKAGE_CONTACT "Olivier Martin <olivier@labapart.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Library to access GATT information from Bluetooth Low Energy (BLE) devices")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}_${CPACK_PACKAGE_VERSION}_${CPACK_PACKAGE_ARCHITECTURE}")

#
# Debian package
#
set(CPACK_GENERATOR "DEB;RPM;ZIP")

# Detect platform architecture to use it for the Debian package
execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE OUTPUT_QUIET)

set(CPACK_DEBIAN_PACKAGE_DEPENDS "libglib2.0-0")

#
# List of file to install
#
install(FILES include/gattlib.h DESTINATION include)
install(FILES ${PROJECT_BINARY_DIR}/gattlib.pc DESTINATION lib/pkgconfig)

include(CPack)
