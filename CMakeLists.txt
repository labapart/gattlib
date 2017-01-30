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

include_directories(include bluez4/attrib bluez4/btio bluez4/src bluez4/lib)

# Bluez specific files
set(gattlib_SRCS bluez4/attrib/att.c
                 bluez4/attrib/gatt.c
                 bluez4/attrib/gattrib.c
                 bluez4/btio/btio.c
                 bluez4/lib/bluetooth.c
                 bluez4/lib/hci.c
                 bluez4/lib/sdp.c
                 bluez4/lib/uuid.c
                 bluez4/src/log.c)

# Gattlib files
list(APPEND gattlib_SRCS src/gattlib_connect.c
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

# gattlib
add_library(gattlib SHARED ${gattlib_SRCS})
target_link_libraries(gattlib ${gattlib_LIBS})

# Examples
add_subdirectory(examples/ble_scan)
add_subdirectory(examples/discover)
add_subdirectory(examples/read_write)
add_subdirectory(examples/gatttool)

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
configure_file(gattlib.pc.in gattlib.pc @ONLY)
install(TARGETS gattlib LIBRARY DESTINATION lib)
install(FILES include/gattlib.h DESTINATION include)
install(FILES ${PROJECT_BINARY_DIR}/gattlib.pc DESTINATION lib/pkgconfig)

include(CPack)
