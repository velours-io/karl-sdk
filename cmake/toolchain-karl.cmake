# Cross compiling for KARL (Raspberry Pi CM4 / CM5, 64-bit, Debian 12).
# Set by the build image (docker/); use it outside only with the same
# compiler and the same OpenSceneGraph as KARL.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# The libraries as they are on KARL: OpenSceneGraph, OpenThreads, nlohmann
# json and the GL headers under usr/local.
set(KARL_SYSROOT /opt/karl-sysroot CACHE PATH "OpenSceneGraph and friends as on KARL")
set(CMAKE_FIND_ROOT_PATH ${KARL_SYSROOT} /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_PREFIX_PATH /usr/local)

# Runs on both modules: the CM4 is ARMv8.0.
set(CMAKE_C_FLAGS_INIT "-march=armv8-a")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a")
