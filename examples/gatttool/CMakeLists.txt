cmake_minimum_required(VERSION 2.6)

set(gatttool_LIBS gattlib readline)

set(gatttool_SRCS gatttool.c interactive.c utils.c)

add_executable(gatttool ${gatttool_SRCS})
target_link_libraries(gatttool ${gatttool_LIBS})
