#
#  GattLib - GATT Library
#
#  Copyright (C) 2016-2019  Olivier Martin <olivier@labapart.org>
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

cmake_minimum_required(VERSION 3.4)

# Add Cross-Compilation support when the environment variables
# CROSS_COMPILE and SYSROOT are defined
include(CrossCompilation.cmake)

project(gattlib)

option(GATTLIB_BUILD_EXAMPLES "Build GattLib examples" YES)
option(GATTLIB_SHARED_LIB "Build GattLib as a shared library" YES)

find_package(PkgConfig REQUIRED)

# Define 'Debug' as the default build type
if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Debug")
endif()

# Show all the warnings
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
# Expose 'gattlib.h' to all sub-directories
include_directories(include)

if (NOT BLUEZ_VERSION)
	# Check version of Bluez to know which backend we use (ie: Bluez code or DBus)
	pkg_search_module(BLUEZ bluez)
	if (NOT BLUEZ_FOUND)
		message(FATAL_ERROR "Please install 'libbluetooth-dev'")
	endif()
endif()

# Extract Bluez version
string(REPLACE "." ";" BLUEZ_VERSIONS "${BLUEZ_VERSION}")
list(GET BLUEZ_VERSIONS 0 BLUEZ_VERSION_MAJOR)
list(GET BLUEZ_VERSIONS 1 BLUEZ_VERSION_MINOR)
add_definitions(-DBLUEZ_VERSION_MAJOR=${BLUEZ_VERSION_MAJOR} -DBLUEZ_VERSION_MINOR=${BLUEZ_VERSION_MINOR})

set(GATTLIB_FORCE_DBUS FALSE CACHE BOOLEAN "Build gattlib with D-Bus support on Bluez version < v5.42")

if (BLUEZ_VERSION_MAJOR LESS 5)
  set(GATTLIB_DBUS FALSE)
elseif (BLUEZ_VERSION_MINOR LESS 42)
  if (GATTLIB_FORCE_DBUS)
    set(GATTLIB_DBUS TRUE)
  else()
    set(GATTLIB_DBUS FALSE)
  endif()
else()
  set(GATTLIB_DBUS TRUE)
endif()

if (GATTLIB_DBUS)
  # Build dbus-based gattlib
  add_subdirectory(dbus)

  # Add the Gattlib build directory for the examples
  link_directories(${PROJECT_BINARY_DIR}/dbus)
else()
  # Build bluez-based gattlib
  add_subdirectory(bluez)

  # Add the Gattlib build directory for the examples
  link_directories(${PROJECT_BINARY_DIR}/bluez)
endif()

add_subdirectory(docs)

# Generate pkg-config file before building the examples
configure_file(dbus/gattlib.pc.in ${PROJECT_BINARY_DIR}/gattlib.pc @ONLY)

# Add the build directory to PKG_CONFIG_PATH
set(ENV{PKG_CONFIG_PATH} "${PROJECT_BINARY_DIR}:$ENV{PKG_CONFIG_PATH}")

if(GATTLIB_BUILD_EXAMPLES)
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
endif()

#
# Packaging
#
set(CPACK_PACKAGE_INSTALL_DIRECTORY /usr CACHE STRING "Install directory (default: /usr).")
if (ENV{TRAVIS_TAG} AND (NOT "ENV{TRAVIS_TAG}" STREQUAL "dev"))
  message("Package Gattlib for tagged version $ENV{TRAVIS_TAG}")
  set(CPACK_PACKAGE_VERSION $ENV{TRAVIS_TAG})
else()
  set(CPACK_PACKAGE_VERSION 0.2-dev)
  message("Package Gattlib for development version $ENV{CPACK_PACKAGE_VERSION}")
endif()
set(CPACK_PACKAGE_CONTACT "Olivier Martin <olivier@labapart.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Library to access GATT information from Bluetooth Low Energy (BLE) devices")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}_${CPACK_PACKAGE_VERSION}_${CPACK_PACKAGE_ARCHITECTURE}")

#
# Debian, RPM and ZIP packages
#
if (CMAKE_SYSROOT)
  # CPack does like RPM package in a cross-toolchain context
  set(CPACK_GENERATOR "DEB;ZIP")
else()
  set(CPACK_GENERATOR "DEB;RPM;ZIP")
endif()

if (NOT CMAKE_SYSROOT)
# Detect platform architecture to use it for the Debian package
  execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE OUTPUT_QUIET)
endif()

set(CPACK_DEBIAN_PACKAGE_DEPENDS "libglib2.0-0")

# Bluez DBus API changed from v5.40
if (GATTLIB_DBUS AND (BLUEZ_VERSION_MAJOR EQUAL 5) AND (BLUEZ_VERSION_MINOR GREATER 39))
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, bluez (>= 5.40)")
endif()

#
# List of file to install
#
install(FILES include/gattlib.h DESTINATION include)
install(FILES ${PROJECT_BINARY_DIR}/gattlib.pc DESTINATION lib/pkgconfig)

include(CPack)
