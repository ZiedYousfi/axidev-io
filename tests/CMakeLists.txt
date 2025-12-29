# CMake configuration for unit tests (Google Test)
#
# This file is intended to be included via `add_subdirectory(tests)` from the
# top-level CMake when AXIDEV_IO_BUILD_TESTS=ON.
#
# It fetches Google Test via FetchContent and builds a small test executable
# that links against the `axidev::io` target produced by the main project.

cmake_minimum_required(VERSION 3.15)

# If tests are disabled at configure time, do nothing here. The top-level
# project typically checks this, but being defensive makes the subdir usable
# standalone too.
if(NOT AXIDEV_IO_BUILD_TESTS)
    message(STATUS "AXIDEV_IO_BUILD_TESTS is OFF; skipping unit tests subdirectory")
    return()
endif()

include(FetchContent)

# Prefer an already-available Google Test; otherwise fetch a known release.
if(NOT TARGET GTest::gtest_main)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # This makes the Google Test targets available (GTest::gtest_main)
    FetchContent_MakeAvailable(googletest)
endif()

# Optional: integration tests are interactive / long-running and should be
# separated from the regular unit test binary. Enable them with:
#   -DAXIDEV_IO_BUILD_INTEGRATION_TESTS=ON
option(AXIDEV_IO_BUILD_INTEGRATION_TESTS "Build integration tests (interactive / long-running)" OFF)

# Unit test executable (only lightweight, non-interactive tests)
add_executable(axidev-io-unit-tests
    test_key_utils.cpp
    test_c_api.cpp
)

target_link_libraries(axidev-io-unit-tests
    PRIVATE
        axidev::io
        GTest::gtest_main
)

# Ensure modern C++ standard (project uses C++20)
target_compile_features(axidev-io-unit-tests PRIVATE cxx_std_20)

# Fix MinGW libwinpthread bug on Windows (GCC 15.2.0 incompatibility)
if(WIN32)
    target_link_options(axidev-io-unit-tests PRIVATE -lpthread -static-libgcc -static-libstdc++)
endif()

# Register with CTest
enable_testing()
include(GoogleTest)
gtest_discover_tests(axidev-io-unit-tests)
# Label the unit test so runners can select it explicitly if desired
set_property(TARGET axidev-io-unit-tests PROPERTY LABELS unit)

# Optional integration test executable (built when AXIDEV_IO_BUILD_INTEGRATION_TESTS=ON)
if(AXIDEV_IO_BUILD_INTEGRATION_TESTS)
    add_executable(axidev-io-integration-tests
        test_integration_sender.cpp
        test_integration_listener.cpp
    )

    target_link_libraries(axidev-io-integration-tests
        PRIVATE
            axidev::io
            GTest::gtest_main
    )

    # Ensure consistent language standard
    target_compile_features(axidev-io-integration-tests PRIVATE cxx_std_20)

    # Fix MinGW libwinpthread bug on Windows (GCC 15.2.0 incompatibility)
    if(WIN32)
        target_link_options(axidev-io-integration-tests PRIVATE -static-libwinpthread)
    endif()

    gtest_discover_tests(axidev-io-integration-tests)
    # Mark the test with a label so runners can select it explicitly if desired
    set_property(TARGET axidev-io-integration-tests PROPERTY LABELS integration)
endif()
