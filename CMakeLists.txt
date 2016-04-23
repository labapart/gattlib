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

find_package(PkgConfig REQUIRED)

include_directories(bluez/attrib bluez/btio bluez/src)

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

# Added Glib support
pkg_search_module(GLIB REQUIRED glib-2.0)
include_directories(${GLIB_INCLUDE_DIRS})
list(APPEND gattlib_LIBS ${GLIB_LIBRARIES})

# gattlib
add_library(gattlib ${gattlib_SRCS})
target_link_libraries(gattlib ${gattlib_LIBS})

# Examples
add_subdirectory(examples/gatttool)
