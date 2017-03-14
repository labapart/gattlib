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

find_package(PkgConfig REQUIRED)

# Bluez specific files
set(bluez4_SRCS bluez4/attrib/att.c
                bluez4/attrib/gatt.c
                bluez4/attrib/gattrib.c
                bluez4/btio/btio.c
                bluez4/lib/bluetooth.c
                bluez4/lib/hci.c
                bluez4/lib/sdp.c
                bluez4/lib/uuid.c
                bluez4/src/log.c)

set(bluez5_SRCS bluez5/attrib/att.c
                bluez5/attrib/gatt.c
                bluez5/attrib/gattrib.c
                bluez5/btio/btio.c
                bluez5/lib/bluetooth.c
                bluez5/lib/hci.c
                bluez5/lib/sdp.c
                bluez5/lib/uuid.c
                bluez5/src/log.c
                bluez5/src/shared/queue.c
                bluez5/src/shared/util.c
                bluez5/src/shared/mgmt.c
                bluez5/src/shared/crypto.c
                bluez5/src/shared/ecc.c
                bluez5/src/shared/ringbuf.c
                bluez5/src/shared/tester.c
                bluez5/src/shared/hci.c
                bluez5/src/shared/hci-crypto.c
                bluez5/src/shared/hfp.c
                bluez5/src/shared/uhid.c
                bluez5/src/shared/pcap.c
                bluez5/src/shared/btsnoop.c
                bluez5/src/shared/ad.c
                bluez5/src/shared/att.c
                bluez5/src/shared/gatt-helpers.c
                bluez5/src/shared/gatt-client.c
                bluez5/src/shared/gatt-server.c
                bluez5/src/shared/gatt-db.c
                bluez5/src/shared/gap.c
                bluez5/src/shared/io-glib.c
                bluez5/src/shared/timeout-glib.c)

# Gattlib files
set(gattlib_SRCS src/gattlib_connect.c
                 src/gattlib_discover.c
                 src/gattlib_read_write.c)

# Added Glib support
pkg_search_module(GLIB REQUIRED glib-2.0)
include_directories(${GLIB_INCLUDE_DIRS})
list(APPEND gattlib_LIBS ${GLIB_LIBRARIES})

# Added Bluetooth support
pkg_search_module(BLUEZ REQUIRED bluez)
include_directories(${BLUEZ_INCLUDE_DIRS})
link_directories(${BLUEZ_LIBRARY_DIRS})
list(APPEND gattlib_LIBS ${BLUEZ_LIBRARIES})
# Extract Bluez version
string(REPLACE "." ";" BLUEZ_VERSIONS "${BLUEZ_VERSION}")
list(GET BLUEZ_VERSIONS 0 BLUEZ_VERSION_MAJOR)
list(GET BLUEZ_VERSIONS 1 BLUEZ_VERSION_MINOR)
add_definitions(-DBLUEZ_VERSION_MAJOR=${BLUEZ_VERSION_MAJOR} -DBLUEZ_VERSION_MINOR=${BLUEZ_VERSION_MINOR})

if(BLUEZ_VERSION_MAJOR STREQUAL "4")
  list(APPEND gattlib_SRCS ${bluez4_SRCS})
  include_directories(include bluez4/attrib bluez4/btio bluez4/src bluez4/lib)
else()
  list(APPEND gattlib_SRCS ${bluez5_SRCS})
  include_directories(include bluez5 bluez5/attrib bluez5/btio bluez5/lib)
  add_definitions(-D_GNU_SOURCE)
endif()

# Show all the warnings
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# gattlib
add_library(gattlib SHARED ${gattlib_SRCS})
target_link_libraries(gattlib ${gattlib_LIBS})

# Generate pkg-config file before building the examples
configure_file(gattlib.pc.in gattlib.pc @ONLY)
# Add the build directory to PKG_CONFIG_PATH
set(ENV{PKG_CONFIG_PATH} "${PROJECT_BINARY_DIR}:$ENV{PKG_CONFIG_PATH}")

# Examples
add_subdirectory(examples/ble_scan)
add_subdirectory(examples/discover)
add_subdirectory(examples/read_write)
add_subdirectory(examples/gatttool)
add_subdirectory(examples/nordic_uart)

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
install(TARGETS gattlib LIBRARY DESTINATION lib)
install(FILES include/gattlib.h DESTINATION include)
install(FILES ${PROJECT_BINARY_DIR}/gattlib.pc DESTINATION lib/pkgconfig)

include(CPack)
