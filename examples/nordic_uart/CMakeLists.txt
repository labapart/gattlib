#
#
#  GattLib - GATT Library
#
#  Copyright (C) 2017  Olivier Martin <olivier@labapart.org>
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

pkg_search_module(GATTLIB REQUIRED gattlib)
pkg_search_module(PCRE REQUIRED libpcre)

set(nordic_uart_SRCS nordic_uart.c)

add_executable(nordic_uart ${nordic_uart_SRCS})
target_link_libraries(nordic_uart ${GATTLIB_LIBRARIES} ${GATTLIB_LDFLAGS} ${PCRE_LIBRARIES} pthread)
