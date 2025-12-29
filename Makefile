# Makefile - convenience tasks for local development
#
# Usage examples:
#   make configure                           # configure in Debug mode (exports compile_commands.json)
#   make configure CMAKE_BUILD_TYPE=Release
#   make build                               # build (after configure)
#   make test                                # build and run tests
#   make integration-test                    # run integration tests (interactive)
#   make run-unit-tests                      # run unit tests binary directly
#   make run-consumer RUN_ARGS="--help"      # run consumer with arguments
#   make export-compile-commands             # copy compile_commands.json to repo root
#   make clean
#
# You can pass additional CMake -D flags via CMAKE_ARGS:
#   make configure CMAKE_ARGS="-DTYPR_IO_BUILD_TEST_CONSUMER=ON"

CMAKE ?= cmake
CTEST ?= ctest

BUILD_DIR ?= build
CMAKE_BUILD_TYPE ?= Debug
CMAKE_ARGS ?=

# Number of parallel build jobs (try a few common probes)
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

ifeq ($(OS),Windows_NT)
EXE_EXT := .exe
else
EXE_EXT :=
endif

RUN_CONSUMER := $(BUILD_DIR)/typr_io_consumer$(EXE_EXT)
RUN_UNIT_TESTS := $(BUILD_DIR)/tests/typr-io-unit-tests$(EXE_EXT)

.PHONY: all configure configure-release build test integration-test run-unit-tests run-consumer export-compile-commands clean help

all: build

configure:
	@echo "Configuring (Build dir = $(BUILD_DIR), Type = $(CMAKE_BUILD_TYPE))..."
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_ARGS)

configure-release:
	$(MAKE) configure CMAKE_BUILD_TYPE=Release

build:
	@echo "Building (parallel jobs = $(JOBS))..."
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)

test:
	@echo "Configuring with tests enabled..."
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DTYPR_IO_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_ARGS)
	@echo "Building (parallel jobs = $(JOBS))..."
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)
	@echo "Running tests..."
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

integration-test:
	@echo "Running integration tests (interactive)..."
	$(CMAKE) -S . -B $(BUILD_DIR) -DTYPR_IO_BUILD_TESTS=ON -DTYPR_IO_BUILD_INTEGRATION_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)
	TYPR_IO_RUN_INTEGRATION_TESTS=1 TYPR_IO_INTERACTIVE=1 $(CTEST) --test-dir $(BUILD_DIR) -R typr-io-integration-tests -C Debug --output-on-failure -V

run-unit-tests:
	@if [ ! -x "$(RUN_UNIT_TESTS)" ]; then \
		echo "Unit tests binary not found. Configuring and building with tests enabled..."; \
		$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DTYPR_IO_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_ARGS); \
		$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS); \
	fi
	@echo "Running unit tests binary..."
	@if [ -x "$(RUN_UNIT_TESTS)" ]; then \
		"$(RUN_UNIT_TESTS)"; \
	else \
		echo "Unit tests binary not found: $(RUN_UNIT_TESTS)"; \
		exit 1; \
	fi

# Run the in-tree consumer (make sure you configured with TYPR_IO_BUILD_TEST_CONSUMER=ON)
# Example: make run-consumer RUN_ARGS="--tap A"
run-consumer: build
	@if [ -x "$(RUN_CONSUMER)" ]; then \
		"$(RUN_CONSUMER)" $(RUN_ARGS); \
	else \
		echo "Consumer binary not found: $(RUN_CONSUMER)"; \
		echo "Did you configure with -DTYPR_IO_BUILD_TEST_CONSUMER=ON and then run 'make build'?"; \
		exit 1; \
	fi

export-compile-commands:
	@echo "Copying compile_commands.json from $(BUILD_DIR) to repository root (if present)..."
	$(CMAKE) -E copy_if_different $(BUILD_DIR)/compile_commands.json compile_commands.json
	@echo "Done."

clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) compile_commands.json

help:
	@echo ""
	@echo "Common targets:"
	@echo "  configure                 Configure the project (Debug by default)."
	@echo "                           Pass CMAKE_ARGS to forward -D flags to CMake."
	@echo "  configure-release         Configure with Release build type."
	@echo "  build                     Build the project."
	@echo "  test                      Build and run tests (ctest)."
	@echo "  integration-test          Configure, build, and run integration tests (interactive)."
	@echo "  run-unit-tests            Run unit tests binary directly (must build first)."
	@echo "  run-consumer              Run the in-tree consumer (use RUN_ARGS to pass args)."
	@echo "  export-compile-commands   Copy build/compile_commands.json to repository root."
	@echo "  clean                     Remove build dir and compile_commands.json."
	@echo ""
