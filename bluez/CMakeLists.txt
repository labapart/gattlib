#
#  GattLib - GATT Library
#
#  Copyright (C) 2016-2024  Olivier Martin <olivier@labapart.org>
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

cmake_minimum_required(VERSION 3.22.0)

find_package(PkgConfig REQUIRED)

message("Build gattlib for Bluez v${BLUEZ_VERSION_MAJOR}.${BLUEZ_VERSION_MINOR}")

set(bluez4_DIR bluez4)
set(bluez5_DIR bluez5)

# Bluez specific files
set(bluez4_SRCS ${bluez4_DIR}/attrib/att.c
                ${bluez4_DIR}/attrib/gatt.c
                ${bluez4_DIR}/attrib/gattrib.c
                ${bluez4_DIR}/btio/btio.c
                ${bluez4_DIR}/lib/bluetooth.c
                ${bluez4_DIR}/lib/hci.c
                ${bluez4_DIR}/lib/sdp.c
                ${bluez4_DIR}/lib/uuid.c
                ${bluez4_DIR}/src/log.c)

set(bluez5_SRCS ${bluez5_DIR}/attrib/att.c
                ${bluez5_DIR}/attrib/gatt.c
                ${bluez5_DIR}/attrib/gattrib.c
                ${bluez5_DIR}/btio/btio.c
                ${bluez5_DIR}/lib/bluetooth.c
                ${bluez5_DIR}/lib/hci.c
                ${bluez5_DIR}/lib/sdp.c
                ${bluez5_DIR}/lib/uuid.c
                ${bluez5_DIR}/src/log.c
                ${bluez5_DIR}/src/shared/queue.c
                ${bluez5_DIR}/src/shared/util.c
                ${bluez5_DIR}/src/shared/mgmt.c
                ${bluez5_DIR}/src/shared/crypto.c
                ${bluez5_DIR}/src/shared/ecc.c
                ${bluez5_DIR}/src/shared/ringbuf.c
                ${bluez5_DIR}/src/shared/tester.c
                ${bluez5_DIR}/src/shared/hci.c
                ${bluez5_DIR}/src/shared/hci-crypto.c
                ${bluez5_DIR}/src/shared/hfp.c
                ${bluez5_DIR}/src/shared/uhid.c
                ${bluez5_DIR}/src/shared/pcap.c
                ${bluez5_DIR}/src/shared/btsnoop.c
                ${bluez5_DIR}/src/shared/ad.c
                ${bluez5_DIR}/src/shared/att.c
                ${bluez5_DIR}/src/shared/gatt-helpers.c
                ${bluez5_DIR}/src/shared/gatt-client.c
                ${bluez5_DIR}/src/shared/gatt-server.c
                ${bluez5_DIR}/src/shared/gatt-db.c
                ${bluez5_DIR}/src/shared/gap.c
                ${bluez5_DIR}/src/shared/io-glib.c
                ${bluez5_DIR}/src/shared/timeout-glib.c)

# Gattlib files
set(gattlib_SRCS gattlib_adapter.c
                 gattlib_connect.c
                 gattlib_discover.c
                 gattlib_read_write.c
                 ${CMAKE_SOURCE_DIR}/common/gattlib_common.c
                 ${CMAKE_SOURCE_DIR}/common/gattlib_eddystone.c
                 ${CMAKE_SOURCE_DIR}/common/logging_backend/${GATTLIB_LOG_BACKEND}/gattlib_logging.c)

# Added Glib support
pkg_search_module(GLIB REQUIRED glib-2.0)
include_directories(${GLIB_INCLUDE_DIRS})
list(APPEND gattlib_LIBS ${GLIB_LDFLAGS})

# Added Bluetooth support
include_directories(${BLUEZ_INCLUDE_DIRS})
list(APPEND gattlib_LIBS ${BLUEZ_LDFLAGS})

include_directories(. ${CMAKE_SOURCE_DIR}/common)
if(BLUEZ_VERSION_MAJOR STREQUAL "4")
  list(APPEND gattlib_SRCS ${bluez4_SRCS})
  include_directories(${bluez4_DIR}/attrib ${bluez4_DIR}/btio ${bluez4_DIR}/src ${bluez4_DIR}/lib)
else()
  list(APPEND gattlib_SRCS ${bluez5_SRCS})
  include_directories(${bluez5_DIR} ${bluez5_DIR}/attrib ${bluez5_DIR}/btio ${bluez5_DIR}/lib)
  add_definitions(-D_GNU_SOURCE)
endif()

# gattlib
target_compile_definitions(gattlib PUBLIC -DGATTLIB_LOG_LEVEL=${GATTLIB_LOG_LEVEL})
if (GATTLIB_LOG_BACKEND STREQUAL "syslog")
	target_compile_definitions(gattlib PUBLIC -DGATTLIB_LOG_BACKEND_SYSLOG)
endif()

include(GNUInstallDirs)
if(GATTLIB_SHARED_LIB)
  add_library(gattlib SHARED ${gattlib_SRCS})
  target_link_libraries(gattlib ${gattlib_LIBS})

  install(TARGETS gattlib LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
else()
  add_library(gattlib ${gattlib_SRCS})
  target_include_directories(gattlib INTERFACE ../include)
  target_link_libraries(gattlib ${gattlib_LIBS})

  install(TARGETS gattlib LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
