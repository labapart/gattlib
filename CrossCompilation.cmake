#
# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
#
# Copyright (c) 2016-2021, Olivier Martin <olivier@labapart.org>
#
#
#  Version: 1.0
#  Repository: https://gist.github.com/oliviermartin/
#  Description:
#
#      Add Cross-Compilation support when the environment variables
#      CROSS_COMPILE and SYSROOT are defined

#
# Check required environment variables
#
if ("$ENV{CROSS_COMPILE}" STREQUAL "")
  # Cross compilation is not needed

  # We still set CPACK_PACKAGE_ARCHITECTURE
  if (NOT DEFINED CPACK_PACKAGE_ARCHITECTURE)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE CPACK_PACKAGE_ARCHITECTURE OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()

  return()
endif()
if ("$ENV{SYSROOT}" STREQUAL "")
  message(WARNING "Environment variable SYSROOT is not defined.")
endif()

# Trigger cross-compilation in CMake
set(CMAKE_SYSTEM_NAME Linux)

# Specify the cross compiler
set(CMAKE_C_COMPILER   $ENV{CROSS_COMPILE}gcc)
set(CMAKE_CXX_COMPILER $ENV{CROSS_COMPILE}g++)

# Specify the target environment 
set(CMAKE_FIND_ROOT_PATH $ENV{SYSROOT})
set(CMAKE_SYSROOT $ENV{SYSROOT})

# Retrieve the machine supported by the toolchain
execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine OUTPUT_VARIABLE TOOLCHAIN_MACHINE OUTPUT_STRIP_TRAILING_WHITESPACE)

# Add '--sysroot' to the compiler flags
set(ENV{CFLAGS} "--sysroot=$ENV{SYSROOT} $ENV{CFLAGS}")

# Add '-rpath' to the linker flags
set(ENV{LDFLAGS} "--sysroot=$ENV{SYSROOT} -Wl,-rpath,$ENV{SYSROOT}/lib/${TOOLCHAIN_MACHINE} $ENV{LDFLAGS}")

#
# Configure pkg-config for cross-compilation
#
if(IS_DIRECTORY "$ENV{SYSROOT}/usr/lib/pkgconfig")
  set(ENV{PKG_CONFIG_PATH} $ENV{SYSROOT}/usr/lib/pkgconfig)
endif()
if(IS_DIRECTORY "$ENV{SYSROOT}/usr/lib/${TOOLCHAIN_MACHINE}/pkgconfig")
  set(ENV{PKG_CONFIG_PATH} $ENV{SYSROOT}/usr/lib/${TOOLCHAIN_MACHINE}/pkgconfig)
endif()
if (NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
  set(ENV{PKG_CONFIG_SYSROOT_DIR} $ENV{SYSROOT})
  # Don't strip -I/usr/include out of cflags
  set(ENV{PKG_CONFIG_ALLOW_SYSTEM_CFLAGS} 1)
  # Don't strip -L/usr/lib out of libs
  set(ENV{PKG_CONFIG_ALLOW_SYSTEM_LIBS} 1)
endif()

# Workaround as some pkgconfig file forgot to add the architecture specific include folder
# such as openssl.pc
include_directories($ENV{SYSROOT}/usr/include $ENV{SYSROOT}/usr/include/${TOOLCHAIN_MACHINE})

# Workaround as some library are installed in $ENV{SYSROOT}/lib/${TOOLCHAIN_MACHINE}
# such as libpcre.so (required by glib-2.0)
link_directories($ENV{SYSROOT}/lib/${TOOLCHAIN_MACHINE})
link_directories($ENV{SYSROOT}/usr/lib/${TOOLCHAIN_MACHINE})

# search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

#
# Set architecture for CPack
#
if ("${TOOLCHAIN_MACHINE}" STREQUAL "arm-linux-gnueabihf")
  set(CPACK_PACKAGE_ARCHITECTURE        armhf)
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE armhf)
  set(CPACK_RPM_PACKAGE_ARCHITECTURE    armv7hl)
endif()
if ("${TOOLCHAIN_MACHINE}" STREQUAL "aarch64-linux-gnu")
  set(CPACK_PACKAGE_ARCHITECTURE        arm64)
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE arm64)
  set(CPACK_RPM_PACKAGE_ARCHITECTURE    aarch64)
endif()
