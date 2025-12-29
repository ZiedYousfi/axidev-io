/**
 * @file test_integration_sender.cpp
 * @brief Comprehensive integration tests for typr-io Sender.
 *
 * This file exercises the real OS backend by injecting events and capturing
 * them via STDIN. Ensure the terminal has focus before starting.
 */

#include <algorithm>
#include <catch2/catch_all.hpp>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <typr-io/keyboard/sender.hpp>
#include <typr-io/log.hpp>

using namespace typr::io::keyboard;
using namespace std::chrono_literals;

TEST_CASE("Sender Integration Suite", "[integration]") {
  Sender sender;
  auto caps = sender.capabilities();
  TYPR_IO_LOG_INFO("Sender Integration Suite: starting integration tests");

  std::cout << "\n====================================================\n"
            << "CORE INTEGRATION TESTS\n"
            << "Target: "
            << (caps.canInjectKeys ? "Hardware/OS Injection" : "Simulated")
            << "\n"
            << "====================================================\n"
            << "IMPORTANT: Keep this terminal window focused!\n"
            << "Press [ENTER] to begin the sequence..." << std::endl;

  std::string start_buffer;
  std::getline(std::cin, start_buffer);

  // --- SECTION 1: Layout & Mapping Consistency ---
  SECTION("Layout Mapping (Z/W & Numbers)") {
    TYPR_IO_LOG_INFO("Integration test: Layout Mapping (Z/W & Numbers)");
    std::cout << "[RUNNING] Verifying character mapping (Z, W, 1)..."
              << std::endl;

    auto task = std::async(std::launch::async, [&sender]() {
      std::this_thread::sleep_for(500ms);
      TYPR_IO_LOG_DEBUG("Integration test: sending taps Z W Num1 Enter");
      // Testing Z and W helps identify AZERTY vs QWERTY confusion.
      // Testing Num1 checks if we produce '1' or the shifted symbol.
      sender.tap(Key::Z);
      sender.tap(Key::W);
      sender.tap(Key::Num1);
      sender.tap(Key::Enter);
      return true;
    });

    std::string received;
    std::getline(std::cin, received);
    task.get();
    TYPR_IO_LOG_INFO("Integration test: received sequence: %s",
                     received.c_str());

    std::transform(received.begin(), received.end(), received.begin(),
                   ::tolower);

    INFO("Received sequence: " << received);
    // We expect these characters to appear regardless of physical layout
    CHECK(received.find('z') != std::string::npos);
    CHECK(received.find('w') != std::string::npos);
    CHECK(received.find('1') != std::string::npos);
  }

  // --- SECTION 2: Modifiers (Shift) ---
  SECTION("Modifiers & Shift State") {
    TYPR_IO_LOG_INFO("Integration test: Modifiers & Shift State");
    std::cout << "[RUNNING] Verifying Shift modifier (producing 'HELLO')..."
              << std::endl;

    auto task = std::async(std::launch::async, [&sender]() {
      std::this_thread::sleep_for(500ms);
      TYPR_IO_LOG_DEBUG("Integration test: holding shift and sending HELLO");

      sender.holdModifier(Modifier::Shift);
      sender.tap(Key::H);
      sender.tap(Key::E);
      sender.tap(Key::L);
      sender.tap(Key::L);
      sender.tap(Key::O);
      sender.releaseModifier(Modifier::Shift);

      sender.tap(Key::Enter);
      return true;
    });

    std::string received;
    std::getline(std::cin, received);
    task.get();
    TYPR_IO_LOG_INFO("Integration test: Received string: %s", received.c_str());

    INFO("Received string: " << received);
    CHECK(received.find("HELLO") != std::string::npos);
  }

  // --- SECTION 3: Key Repeat ---
  SECTION("Long Press & Repeat") {
    if (!caps.supportsKeyRepeat) {
      SKIP("Key repeat not supported on this platform.");
    }

    TYPR_IO_LOG_INFO("Integration test: Long Press & Repeat");
    std::cout << "[RUNNING] Testing key repeat (Holding 'X')..." << std::endl;

    auto task = std::async(std::launch::async, [&sender]() {
      std::this_thread::sleep_for(500ms);
      TYPR_IO_LOG_DEBUG("Integration test: keyDown(Key::X)");

      sender.keyDown(Key::X);
      std::this_thread::sleep_for(1500ms); // Hold long enough for OS repeat
      sender.keyUp(Key::X);

      std::this_thread::sleep_for(100ms);
      sender.tap(Key::Enter);
      return true;
    });

    std::string received;
    std::getline(std::cin, received);
    task.get();

    size_t count = std::count(received.begin(), received.end(), 'x') +
                   std::count(received.begin(), received.end(), 'X');
    TYPR_IO_LOG_INFO("Integration test: Repeat count=%zu", count);

    INFO("Repeat count: " << count);
    // Usually, 1.5s should produce at least 5-10 chars depending on OS
    // settings.
    CHECK(count >= 3);
  }
}
