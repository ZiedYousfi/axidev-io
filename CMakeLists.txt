cmake_minimum_required(VERSION 3.15)
project(typr-io
    VERSION 0.2.1
    DESCRIPTION "Cross-platform layout-aware input/output library"
    LANGUAGES CXX
)

option(TYPR_IO_BUILD_SHARED "Build typr-io as a shared library" OFF)
option(TYPR_IO_BUILD_EXAMPLES "Build the example executables" OFF)
option(TYPR_IO_BUILD_TESTS "Build tests for typr-io" OFF)
option(TYPR_IO_BUILD_TEST_CONSUMER "Build in-tree consumer demo" OFF)
option(TYPR_IO_EXPORT_COMPILE_COMMANDS "Enable compile_commands.json" ON)

if(TYPR_IO_BUILD_SHARED)
    set(BUILD_SHARED_LIBS ON)
endif()

if(TYPR_IO_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(TYPR_SOURCES
    src/common/key_utils.cpp
    src/c_api.cpp
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
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBINPUT REQUIRED libinput)
    pkg_check_modules(LIBUDEV REQUIRED libudev)
    pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)

    list(APPEND TYPR_SOURCES
        src/sender/sender_uinput.cpp
        src/listener/listener_linux.cpp
    )
endif()

if(TYPR_IO_BUILD_SHARED)
    add_library(typr_io SHARED ${TYPR_SOURCES})
else()
    add_library(typr_io STATIC ${TYPR_SOURCES})
endif()

target_include_directories(typr_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(typr_io PUBLIC cxx_std_20)

if(TYPR_IO_BUILD_SHARED)
    if(WIN32)
        target_compile_definitions(typr_io PRIVATE typr_io_EXPORTS)
    endif()
else()
    target_compile_definitions(typr_io PUBLIC TYPR_IO_STATIC)
endif()

# Platform specific linkages
if(APPLE)
    target_link_libraries(typr_io PUBLIC
        "-framework ApplicationServices"
        "-framework Carbon"
        "-framework Foundation"
        "-framework CoreGraphics"
    )
elseif(WIN32)
    target_link_libraries(typr_io PUBLIC user32)
elseif(UNIX)
    target_include_directories(typr_io PRIVATE
        ${LIBINPUT_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${LIBUDEV_INCLUDE_DIRS}
    )
    target_link_libraries(typr_io PRIVATE
        ${LIBINPUT_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
        ${LIBUDEV_LIBRARIES}
    )
endif()

find_package(Threads REQUIRED)
target_link_libraries(typr_io PRIVATE Threads::Threads)

set_target_properties(typr_io PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "include/typr-io/core.hpp;include/typr-io/sender.hpp;include/typr-io/listener.hpp;include/typr-io/c_api.h"
)

add_library(typr::io ALIAS typr_io)

# Installation
include(GNUInstallDirs)

install(TARGETS typr_io
    EXPORT typr-ioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/typr-io
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

install(EXPORT typr-ioTargets
    FILE typr-ioTargets.cmake
    NAMESPACE typr::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/typr-io
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/typr-ioConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/typr-ioConfig.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/typr-ioTargets.cmake\")\n"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/typr-ioConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/typr-ioConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/typr-io
)

export(EXPORT typr-ioTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/typr-ioTargets.cmake"
    NAMESPACE typr::
)

# Examples
if(TYPR_IO_BUILD_EXAMPLES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/example.cpp")
        add_executable(typr-io-example examples/example.cpp)
        target_link_libraries(typr-io-example PRIVATE typr::io)
        install(TARGETS typr-io-example RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/example_c.c")
        add_executable(typr-io-example-c examples/example_c.c)
        target_link_libraries(typr-io-example-c PRIVATE typr::io)
        install(TARGETS typr-io-example-c RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

if(TYPR_IO_BUILD_TEST_CONSUMER)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_consumer/main.cpp")
        add_executable(typr_io_consumer test_consumer/main.cpp)
        target_link_libraries(typr_io_consumer PRIVATE typr::io)
        target_compile_features(typr_io_consumer PRIVATE cxx_std_20)
        install(TARGETS typr_io_consumer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

if(TYPR_IO_BUILD_TESTS)
    enable_testing()
    if(TARGET typr-io-example)
        add_test(NAME typr-io-example-help COMMAND typr-io-example --help)
    endif()
    if(TARGET typr_io_consumer)
        add_test(NAME typr-io-consumer-help COMMAND typr_io_consumer --help)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

if(TYPR_IO_EXPORT_COMPILE_COMMANDS)
    add_custom_target(export-compile-commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_BINARY_DIR}/compile_commands.json ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Copy compile_commands.json to repository root"
    )
endif()

message(STATUS "typr-io: Static build (TYPR_IO_BUILD_SHARED=${TYPR_IO_BUILD_SHARED})")
message(STATUS "typr-io: Version ${PROJECT_VERSION}")