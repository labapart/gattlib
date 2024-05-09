#
# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

cmake_minimum_required(VERSION 3.22.0)

# Add Cross-Compilation support when the environment variables
# CROSS_COMPILE and SYSROOT are defined
include(CrossCompilation.cmake)

project(gattlib)

#TODO: Gattlib examples must be ported to new gattlib_connect()
option(GATTLIB_BUILD_EXAMPLES "Build GattLib examples" YES)
option(GATTLIB_SHARED_LIB "Build GattLib as a shared library" YES)
option(GATTLIB_BUILD_DOCS "Build GattLib docs" NO)
option(GATTLIB_PYTHON_INTERFACE "Build GattLib Python Interface" NO)
option(GATTLIB_ENABLE_ADDRESS_SANITIZER "Enable address sanitizer" NO)

find_package(PkgConfig REQUIRED)
find_package(Doxygen)

# Define 'Debug' as the default build type
if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Debug")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG)
endif()

# Show all the warnings
if (MSVC)
  # warning level 4
  add_compile_options(/W4)
else()
  # additional warnings
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-variadic-macros)
endif()
# Add stack protector
add_compile_options(-fstack-protector-strong)

if (GATTLIB_ENABLE_ADDRESS_SANITIZER)
  add_compile_options(-fsanitize=address -fsanitize=bounds -fsanitize=undefined -fsanitize-recover=address)
  add_link_options(-fsanitize=address -fsanitize=bounds -fsanitize=undefined -fsanitize-recover=address -static-libasan)
endif()

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

set(GATTLIB_FORCE_DBUS OFF CACHE BOOL "Build gattlib with D-Bus support on Bluez version < v5.42")

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

# With 'syslog' backend, we enable all logs (ie: up to level debug) and we leave the
# application to set the level using 'setlogmask()'
set(GATTLIB_LOG_LEVEL 3 CACHE STRING "Define the minimum logging level for Gattlib (0=error, 1=warning, 2=info, 3=debug)")
set(GATTLIB_LOG_BACKEND syslog CACHE STRING "Define logging backend: syslog, printf, python (default: syslog)")

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

if(GATTLIB_BUILD_DOCS)
  if (NOT Doxygen_FOUND)
    message(FATAL_ERROR "Gattlib documentation requires Doxygen. Or disable doc generation with '-DGATTLIB_BUILD_DOCS=OFF'")
  endif()

  add_subdirectory(docs)
endif()

# Generate pkg-config file before building the examples
configure_file(dbus/gattlib.pc.in ${PROJECT_BINARY_DIR}/gattlib.pc @ONLY)

# Add the build directory to PKG_CONFIG_PATH
set(ENV{PKG_CONFIG_PATH} "${PROJECT_BINARY_DIR}:$ENV{PKG_CONFIG_PATH}")

if(GATTLIB_BUILD_EXAMPLES)
  # Examples
  add_subdirectory(examples/advertisement_data)
  add_subdirectory(examples/ble_scan)
  add_subdirectory(examples/discover)
  add_subdirectory(examples/find_eddystone)
  add_subdirectory(examples/read_write)
  #add_subdirectory(examples/read_write_memory)
  add_subdirectory(examples/notification)
  add_subdirectory(examples/nordic_uart)
  add_subdirectory(tests/test_continuous_connection)

  # Some examples require Bluez code and other DBus support
  if (NOT GATTLIB_DBUS)
    add_subdirectory(examples/gatttool)
  endif()
endif()

#
# Packaging
#
set(CPACK_PACKAGE_INSTALL_DIRECTORY /usr CACHE STRING "Install directory (default: /usr).")
if (ENV{PACKAGE_VERSION} AND (NOT "ENV{PACKAGE_VERSION}" STREQUAL "dev"))
  message("Package Gattlib for tagged version $ENV{PACKAGE_VERSION}")

  # Transform 'v0.3' into '0.3' and 'v0.3-rc1' into '0.3-rc1'
  string(REGEX REPLACE "v([0-9]+).([0-9]+)(.*)" "\\1.\\2\\3" CPACK_PACKAGE_VERSION $ENV{PACKAGE_VERSION})
else()
  set(CPACK_PACKAGE_VERSION 0.4-dev)
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
include(GNUInstallDirs)
install(FILES include/gattlib.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${PROJECT_BINARY_DIR}/gattlib.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

include(CPack)
