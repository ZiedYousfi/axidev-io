cmake_minimum_required(VERSION 3.15)
project(typr-io
    VERSION 0.1.0
    DESCRIPTION "Cross-platform layout-aware input/output library"
    LANGUAGES CXX
)

# Options
option(TYPR_IO_BUILD_SHARED "Build typr-io as a shared library" OFF)
option(TYPR_IO_BUILD_EXAMPLES "Build the example executables (if present)" OFF)
option(TYPR_IO_BUILD_TESTS "Build tests for typr-io (lightweight smoke tests)" OFF)
# Convenience options for local development
option(TYPR_IO_BUILD_TEST_CONSUMER "Build a small in-tree consumer demo executable for manual testing (if test_consumer/main.cpp exists)" ON)
option(TYPR_IO_EXPORT_COMPILE_COMMANDS "Enable generation of compile_commands.json for clang tooling" ON)

if(TYPR_IO_BUILD_SHARED)
    # Respect a per-project request to build shared library
    set(BUILD_SHARED_LIBS ON)
endif()

if(TYPR_IO_EXPORT_COMPILE_COMMANDS)
    # Allow easy generation of a compile_commands.json for clang tooling.
    # This can be helpful for editors/linters (e.g. clangd) to pick up flags.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Collect sources - platform-specific files are appended conditionally so
# cross-platform configure works even if some platforms lack Objective-C++ or
# Linux headers.
set(TYPR_SOURCES
    src/common/key_utils.cpp
)

if(APPLE)
    list(APPEND TYPR_SOURCES
    src/sender/sender_macos.mm
    src/listener/listener_macos.mm
  )
elseif(WIN32)
    list(APPEND TYPR_SOURCES
    src/sender/sender_windows.cpp
    src/listener/listener_windows.cpp
  )
elseif(UNIX)
    list(APPEND TYPR_SOURCES
    src/sender/sender_uinput.cpp
    src/listener/listener_x11.cpp
  )
endif()

# Create the library target (use an underscore in the target name to avoid
# hyphen-related identifier subtleties in CMake)
if(TYPR_IO_BUILD_SHARED)
    add_library(typr_io SHARED ${TYPR_SOURCES})
else()
    add_library(typr_io STATIC ${TYPR_SOURCES})
endif()

# Public and private include directories
target_include_directories(typr_io
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(typr_io PUBLIC cxx_std_20)

# When building as a shared library on Windows we want to export symbols
if(WIN32 AND TYPR_IO_BUILD_SHARED)
    target_compile_definitions(typr_io PRIVATE typr_io_EXPORTS)
endif()

# Platform specific linkages
if(APPLE)
    # Objective-C++ files and macOS frameworks
    target_link_libraries(typr_io PUBLIC
    "-framework ApplicationServices"
    "-framework Carbon"
    "-framework Foundation"
    "-framework CoreGraphics"
  )
elseif(WIN32)
    target_link_libraries(typr_io PUBLIC
    user32
  )
elseif(UNIX)
    # Optional: bring in X11/XInput when available for the X11 output listener.
    find_package(X11)
    if(X11_FOUND)
        target_include_directories(typr_io PRIVATE ${X11_INCLUDE_DIR})
        # Link X11 core and XInput (XI) - Xi is needed for XInput2 / XI_Raw*
        target_link_libraries(typr_io PRIVATE ${X11_LIBRARIES} Xi)
    endif()
endif()

# Threads are used by various backends / listeners
find_package(Threads REQUIRED)
target_link_libraries(typr_io PRIVATE Threads::Threads)

# Versioning and public headers so `install(TARGETS ... PUBLIC_HEADER ...)` works
set_target_properties(typr_io PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  PUBLIC_HEADER "include/typr-io/core.hpp;include/typr-io/sender.hpp;include/typr-io/listener.hpp;include/typr-io/typr_io.hpp"
)

# Preferred public alias for consumers
add_library(typr::io ALIAS typr_io)

# ------ Installation & CMake package support ------

include(GNUInstallDirs)

# Install the library and its public headers
install(TARGETS typr_io
  EXPORT typr-ioTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/typr-io
)

# Also install all headers from include/ so consumers can `#include <typr-io/...>`
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

# Export CMake targets for downstream use
install(EXPORT typr-ioTargets
  FILE typr-ioTargets.cmake
  NAMESPACE typr::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/typr-io
)

include(CMakePackageConfigHelpers)

# Write a basic version file
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/typr-ioConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

# Create a minimal config file that simply includes the exported targets file
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/typr-ioConfig.cmake"
"include(\"\${CMAKE_CURRENT_LIST_DIR}/typr-ioTargets.cmake\")\n"
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/typr-ioConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/typr-ioConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/typr-io
)

# Allow `find_package(typr-io CONFIG REQUIRED)` to work in the build tree too
export(EXPORT typr-ioTargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/typr-ioTargets.cmake"
  NAMESPACE typr::
)

# Optional example: only add when explicitly enabled and the source exists.
if(TYPR_IO_BUILD_EXAMPLES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/example.cpp")
        add_executable(typr-io-example examples/example.cpp)
        target_link_libraries(typr-io-example PRIVATE typr::io)
        install(TARGETS typr-io-example RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        message(WARNING "TYPR_IO_BUILD_EXAMPLES enabled but examples/example.cpp was not found")
    endif()
endif()

# Optional in-tree consumer: build a small standalone executable that links
# against the just-built library for quick local testing (controlled via
# TYPR_IO_BUILD_TEST_CONSUMER).
if(TYPR_IO_BUILD_TEST_CONSUMER)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_consumer/main.cpp")
        add_executable(typr_io_consumer test_consumer/main.cpp)
        target_link_libraries(typr_io_consumer PRIVATE typr::io)
        target_compile_features(typr_io_consumer PRIVATE cxx_std_20)
        install(TARGETS typr_io_consumer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        message(STATUS "TYPR_IO_BUILD_TEST_CONSUMER is ON but test_consumer/main.cpp was not found")
    endif()
endif()

# Optional tests: light-weight smoke tests that exercise the example binary.
# Tests are intentionally non-invasive (they only run `--help`) so they can be
# executed in CI without requiring special permissions or injecting input.
if(TYPR_IO_BUILD_TESTS)
    enable_testing()
    # Example-based tests (existing behavior)
    if(TARGET typr-io-example)
        add_test(NAME typr-io-example-help COMMAND typr-io-example --help)
    else()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/example.cpp")
            add_executable(typr-io-test examples/example.cpp)
            target_link_libraries(typr-io-test PRIVATE typr::io)
            add_test(NAME typr-io-example-help COMMAND typr-io-test --help)
        else()
            message(WARNING "TYPR_IO_BUILD_TESTS enabled but no example/test source was found")
        endif()
    endif()

    # Consumer-based tests: prefer the in-tree consumer if present,
    # otherwise attempt to build a small consumer test from the source.
    if(TARGET typr_io_consumer)
        add_test(NAME typr-io-consumer-help COMMAND typr_io_consumer --help)
    else()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_consumer/main.cpp")
            add_executable(typr-io-test-consumer test_consumer/main.cpp)
            target_link_libraries(typr-io-test-consumer PRIVATE typr::io)
            add_test(NAME typr-io-consumer-help COMMAND typr-io-test-consumer --help)
        endif()
    endif()
endif()

# Convenience target to copy compile_commands.json from the build dir to the
# source root so tools that expect it at the repository root can use it.
if(TYPR_IO_EXPORT_COMPILE_COMMANDS)
    add_custom_target(export-compile-commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Copy compile_commands.json from build/ to repository root"
    )
endif()

message(STATUS "typr-io: Building ${TYPR_IO_BUILD_SHARED} (shared = ON means shared library)")
message(STATUS "typr-io: Version ${PROJECT_VERSION}")
message(STATUS "typr-io: Build test consumer = ${TYPR_IO_BUILD_TEST_CONSUMER}")
message(STATUS "typr-io: Export compile commands = ${TYPR_IO_EXPORT_COMPILE_COMMANDS}")
