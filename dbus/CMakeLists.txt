#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

cmake_minimum_required(VERSION 3.22.0)

find_package(PkgConfig REQUIRED)

# Added Glib support to ensure we have 'gdbus-codegen'
pkg_search_module(GLIB REQUIRED glib-2.0)

# For DBus support
pkg_search_module(GIO_UNIX REQUIRED gio-unix-2.0)

# Needed by 'bluez5/lib/uuid.c'
pkg_search_module(BLUEZ REQUIRED bluez)

message("Build DBus gattlib for Bluez v${BLUEZ_VERSION_MAJOR}.${BLUEZ_VERSION_MINOR}")

if (BLUEZ_VERSION_MINOR LESS 40)
	set(DBUS_BLUEZ_API dbus-bluez-pre-v5.40)
elseif (BLUEZ_VERSION_MINOR LESS 48)
	set(DBUS_BLUEZ_API dbus-bluez-v5.40)
else()
	set(DBUS_BLUEZ_API dbus-bluez-v5.48)
endif()

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-adaptater1.c
                   COMMAND gdbus-codegen --interface-prefix org.bluez.Adapter1. --generate-c-code ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-adaptater1 ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.Adapter1.xml
                   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.Adapter1.xml
                   COMMENT "Generate D-Bus 'org.bluez.Adapter1.xml'"
                   )

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-device1.c
                   COMMAND gdbus-codegen --interface-prefix org.bluez.Device1. --generate-c-code ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-device1 ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.Device1.xml
                   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.Device1.xml
                   COMMENT "Generate D-Bus 'org.bluez.Device1.xml'"
                   )

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-gattservice1.c
                   COMMAND gdbus-codegen --interface-prefix org.bluez.GattService1. --generate-c-code ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-gattservice1 ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.GattService1.xml
                   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.GattService1.xml
                   COMMENT "Generate D-Bus 'org.bluez.GattService1.xml'"
                   )

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-gattcharacteristic1.c
                   COMMAND gdbus-codegen --interface-prefix org.bluez.Characteristic1. --generate-c-code ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-gattcharacteristic1 ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.GattCharacteristic1.xml
                   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.GattCharacteristic1.xml
                   COMMENT "Generate D-Bus 'org.bluez.GattCharacteristic1.xml'"
                   )

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-gattdescriptor1.c
                   COMMAND gdbus-codegen --interface-prefix org.bluez.Descriptor1. --generate-c-code ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-gattdescriptor1 ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.GattDescriptor1.xml
                   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.GattDescriptor1.xml
                   COMMENT "Generate D-Bus 'org.bluez.GattDescriptor1.xml'"
                   )

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-battery1.c
                   COMMAND gdbus-codegen --interface-prefix org.bluez.Battery1. --generate-c-code ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-battery1 ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.Battery1.xml
                   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${DBUS_BLUEZ_API}/org.bluez.Battery1.xml
                   COMMENT "Generate D-Bus 'org.bluez.Battery1.xml'"
                   )

include_directories(. ${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_CURRENT_BINARY_DIR} ${GIO_UNIX_INCLUDE_DIRS} ${BLUEZ_INCLUDE_DIRS})

set(gattlib_SRCS gattlib.c
                 gattlib_adapter.c
                 gattlib_advertisement.c
                 gattlib_char.c
                 gattlib_notification.c
                 gattlib_stream.c
                 bluez5/lib/uuid.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/gattlib_common.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/gattlib_common_adapter.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/gattlib_device_state_management.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/gattlib_eddystone.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/gattlib_callback_connected_device.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/gattlib_callback_disconnected_device.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/gattlib_callback_discovered_device.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/gattlib_callback_notification_device.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/logging_backend/${GATTLIB_LOG_BACKEND}/gattlib_logging.c
                 ${CMAKE_CURRENT_LIST_DIR}/../common/mainloop/gattlib_glib_mainloop.c
                 ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-adaptater1.c
                 ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-device1.c
                 ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-gattcharacteristic1.c
                 ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-gattdescriptor1.c
                 ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-gattservice1.c)

if (GATTLIB_PYTHON_INTERFACE)
  list(APPEND gattlib_SRCS ${CMAKE_CURRENT_LIST_DIR}/../common/gattlib_callback_python.c)
endif()

if (BLUEZ_VERSION_MINOR GREATER 40)
	list(APPEND gattlib_SRCS ${CMAKE_CURRENT_BINARY_DIR}/org-bluez-battery1.c)
endif()

set(gattlib_LIBS ${GLIB_LDFLAGS} ${GIO_UNIX_LDFLAGS})

#
# Add Python Support
#
if(GATTLIB_PYTHON_INTERFACE)
  if(GATTLIB_SHARED_LIB)
    set(Python_USE_STATIC_LIBS FALSE)
  else()
    set(Python_USE_STATIC_LIBS TRUE)
  endif()

  find_package(Python3 COMPONENTS Interpreter Development)
  if (NOT Python3_Development_FOUND)
    find_package(Python3 COMPONENTS Development.Module)
    if (NOT Python3_Development.Module_FOUND)
      message(FATAL_ERROR "Could not find Python developer package")
    endif()

    # Case of mainlinux container to build Wheel Python package
    if (NOT Python3_LIBRARIES)
      set(Python3_LIBRARIES "")
    endif()
  endif()

  include_directories(${Python3_INCLUDE_DIRS})
  list(APPEND gattlib_LIBS ${Python3_LIBRARIES})

  add_definitions(-DWITH_PYTHON -DPYTHON_VERSION_MAJOR=${Python3_VERSION_MAJOR} -DPYTHON_VERSION_MINOR=${Python3_VERSION_MINOR})
endif()

# Gattlib
if(GATTLIB_SHARED_LIB)
  add_library(gattlib SHARED ${gattlib_SRCS})
else()
  add_library(gattlib ${gattlib_SRCS})
endif()

target_compile_definitions(gattlib PUBLIC -DGATTLIB_LOG_LEVEL=${GATTLIB_LOG_LEVEL})
if (GATTLIB_LOG_BACKEND STREQUAL "syslog")
	target_compile_definitions(gattlib PUBLIC -DGATTLIB_LOG_BACKEND_SYSLOG)
endif()

target_include_directories(gattlib PUBLIC ../include)
target_link_libraries(gattlib ${gattlib_LIBS})

include(GNUInstallDirs)

if(GATTLIB_SHARED_LIB)
  install(TARGETS gattlib LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
else()
  install(TARGETS gattlib ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
