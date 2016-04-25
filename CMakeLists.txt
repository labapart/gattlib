#
#
#  GattLib - GATT Library
#
#  Copyright (C) 2016  Olivier Martin <olivier@labapart.org>
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

project(gattlib)

find_package(PkgConfig REQUIRED)

include_directories(include bluez/attrib bluez/btio bluez/src)

# Bluez specific files
set(gattlib_SRCS bluez/attrib/att.c
                 bluez/attrib/gatt.c
                 bluez/attrib/gattrib.c
                 bluez/btio/btio.c
                 bluez/lib/bluetooth.c
                 bluez/lib/hci.c
                 bluez/lib/sdp.c
                 bluez/lib/uuid.c
                 bluez/src/log.c)

add_definitions(-DVERSION="4.101")

# Gattlib files
list(APPEND gattlib_SRCS src/gattlib_connect.c
                         src/gattlib_discover.c
                         src/gattlib_read_write.c)

# Added Glib support
pkg_search_module(GLIB REQUIRED glib-2.0)
include_directories(${GLIB_INCLUDE_DIRS})
list(APPEND gattlib_LIBS ${GLIB_LIBRARIES})

# gattlib
add_library(gattlib SHARED ${gattlib_SRCS})
target_link_libraries(gattlib ${gattlib_LIBS})

# Examples
add_subdirectory(examples/discover)
add_subdirectory(examples/read_write)
add_subdirectory(examples/gatttool)

#
# Packaging
#
set(CPACK_PACKAGE_INSTALL_DIRECTORY /usr CACHE STRING "Install directory (default: /usr).")
set(CPACK_PACKAGE_VERSION 0.1)
set(CPACK_PACKAGE_CONTACT "Olivier Martin <olivier@labapart.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Library to access GATT information from Bluetooth Low Power devices")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}")

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
