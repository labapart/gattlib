cmake_minimum_required(VERSION 2.6)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB_PKG glib-2.0)

if (GLIB_PKG_FOUND)
    message(Found glib-2.0)
endif()
include_directories(${GLIB_PKG_INCLUDE_DIRS})

set(ble_scan_LIBS gattlib pthread)

set(ble_scan_SRCS ble_scan.c)

add_executable(ble_scan ${ble_scan_SRCS})
target_link_libraries(ble_scan ${ble_scan_LIBS})
