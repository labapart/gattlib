cmake_minimum_required(VERSION 2.6)

set(ble_scan_LIBS gattlib pthread)

set(ble_scan_SRCS ble_scan.c)

add_executable(ble_scan ${ble_scan_SRCS})
target_link_libraries(ble_scan ${ble_scan_LIBS})
