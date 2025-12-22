cmake_minimum_required(VERSION 3.15)
project(typr-io-consumer LANGUAGES CXX)

# Simple consumer CMake file for demonstrating how to consume an installed
# typr-io package via find_package(typr-io CONFIG REQUIRED).
#
# Usage examples:
#   # If typr-io has been installed system-wide (e.g. /usr/local), a simple:
#   cmake -S . -B build
#
#   # If typr-io is installed to a non-standard prefix (for example the local
#   # build/install tree from this repository), point CMake to it:
#   cmake -S . -B build -DTYPR_IO_ROOT=/path/to/typr-io/build/install
#
#   # Alternatively use CMAKE_PREFIX_PATH directly:
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/typr-io/build/install
#
# After configure:
#   cmake --build build
#
# Then run the produced executable:
#   ./build/typr_io_consumer
#
# This CMakeLists assumes there is a simple `main.cpp` in this directory that
# uses the public typr-io umbrella header:
#   #include <typr-io/typr_io.hpp>

option(TYPR_IO_ROOT "Root prefix where typr-io is installed (sets CMAKE_PREFIX_PATH)" "")

if (TYPR_IO_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${TYPR_IO_ROOT}")
    message(STATUS "Appended TYPR_IO_ROOT to CMAKE_PREFIX_PATH: ${TYPR_IO_ROOT}")
endif()

# Attempt to find an installed typr-io using the exported CMake config files.
# This requires typr-io to have been installed (e.g. via `cmake --install`).
find_package(typr-io CONFIG REQUIRED)

# Simple executable that links to the public target `typr::io`.
add_executable(typr_io_consumer main.cpp)

# Link against the imported target exposed by typr-io. This will transitively
# bring include directories and any link requirements into this target.
target_link_libraries(typr_io_consumer PRIVATE typr::io)

# Ensure C++20
target_compile_features(typr_io_consumer PRIVATE cxx_std_20)

message(STATUS "typr-io consumer: linked to typr::io")
