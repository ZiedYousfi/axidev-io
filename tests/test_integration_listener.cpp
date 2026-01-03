/**
 * @file test_integration_listener.cpp
 * @brief Integration tests for axidev-io Listener.
 *
 * These tests exercise the real OS backend by observing real keyboard events
 * while the user types into STDIN. Keep this terminal focused while the
 * test runs. If the listener cannot be started due to platform support or
 * permissions, the tests are skipped.
 */

#include <axidev-io/core.hpp>
#include <axidev-io/keyboard/listener.hpp>
#include <axidev-io/log.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using namespace axidev::io::keyboard;
using namespace std::chrono_literals;

static void appendUtf8(std::string &out, char32_t cp) {
  uint32_t u = static_cast<uint32_t>(cp);
  if (u <= 0x7F) {
    out.push_back(static_cast<char>(u));
  } else if (u <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((u >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
  } else if (u <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((u >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | ((u >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((u >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
  }
}

static void popLastUtf8Char(std::string &s) {
  if (s.empty())
    return;
  // Remove trailing continuation bytes (0b10xxxxxx)
  while (!s.empty() &&
         ((static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)) {
    s.pop_back();
  }
  if (!s.empty())
    s.pop_back();
}

class ListenerIntegrationTest : public ::testing::Test {
protected:
  void SetUp() {
    AXIDEV_IO_LOG_INFO(
        "Listener Integration Suite: starting integration tests");

    std::cout << "\n====================================================\n"
              << "LISTENER INTEGRATION TESTS\n"
              << "Target: Global keyboard observation (Listener)\n"
              << "====================================================\n"
              << "IMPORTANT: Keep this terminal window focused!\n"
              << "These tests will ask you to type into this terminal. Press "
                 "[ENTER]\n"
              << "to advance between prompts.\n\n"
              << "Press [ENTER] to begin..." << std::endl;

    std::string start_buffer;
    std::getline(std::cin, start_buffer);
  }
};

TEST_F(ListenerIntegrationTest, ExactMatchTypedInputObserved) {
  AXIDEV_IO_LOG_INFO("Integration test: Listener Exact Match");
  const std::string expected = "axidev-zw-123";
  std::cout << "[RUNNING] Type the following exact string into this terminal\n"
            << "and press [ENTER]:\n\n"
            << "  " << expected << "\n\n"
            << "Type it exactly (case-sensitive). Avoid arrow keys or complex "
               "edits."
            << std::endl;

  std::mutex mtx;
  std::condition_variable cv;
  std::string observed;
  bool saw_enter = false;

  Listener listener;
  auto cb = [&](char32_t cp, Key key, Modifier /*mods*/, bool pressed) {
    // Collect characters on key release (pressed == false) to better match
    // the character delivered to the terminal/STDIN on most platforms.
    if (pressed) {
      AXIDEV_IO_LOG_DEBUG("Listener test cb: ignoring press event key=%s cp=%u",
                          keyToString(key).c_str(), static_cast<unsigned>(cp));
      return;
    }
    std::lock_guard<std::mutex> lk(mtx);
    if (cp != 0) {
      appendUtf8(observed, cp);
      AXIDEV_IO_LOG_DEBUG(
          "Listener test cb: appended cp=%u key=%s observed='%s'",
          static_cast<unsigned>(cp), keyToString(key).c_str(),
          observed.c_str());
    } else if (key == Key::Backspace) {
      popLastUtf8Char(observed);
      AXIDEV_IO_LOG_DEBUG("Listener test cb: backspace - observed='%s'",
                          observed.c_str());
    } else if (key == Key::Enter) {
      saw_enter = true;
      AXIDEV_IO_LOG_DEBUG("Listener test cb: enter - observed='%s'",
                          observed.c_str());
      cv.notify_one();
    } else {
      AXIDEV_IO_LOG_DEBUG(
          "Listener test cb: non-printable key=%s cp=0 observed='%s'",
          keyToString(key).c_str(), observed.c_str());
    }
  };

  bool ok = listener.start(cb);
  if (!ok) {
    AXIDEV_IO_LOG_INFO("Listener could not start - skipping integration test");
    GTEST_SKIP()
        << "Listener not available on this platform or permission denied.";
  }

  std::string typed;
  std::getline(std::cin, typed);

  // Wait for the listener to observe Enter (timeout).
  {
    std::unique_lock<std::mutex> lk(mtx);
    cv.wait_for(lk, 2s, [&] { return saw_enter; });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  listener.stop();

  {
    std::lock_guard<std::mutex> lk(mtx);
    AXIDEV_IO_LOG_INFO(
        "Integration test: expected='%s' typed='%s' observed='%s'",
        expected.c_str(), typed.c_str(), observed.c_str());
    ASSERT_EQ(typed, expected)
        << "Expected: " << expected << ", Typed: " << typed;
    EXPECT_EQ(observed, expected)
        << "Expected: " << expected << ", Observed: " << observed;
  }
}

TEST_F(ListenerIntegrationTest, BackspaceHandling) {
  AXIDEV_IO_LOG_INFO("Integration test: Listener Backspace Handling");
  const std::string expected = "abd";
  std::cout << "[RUNNING] Please perform the following sequence:\n"
            << "  1) Type 'abc'\n"
            << "  2) Press BACKSPACE once\n"
            << "  3) Type 'd'\n"
            << "  4) Press [ENTER]\n\n"
            << "The final string should be:\n\n"
            << "  " << expected << "\n\n"
            << "Follow the sequence exactly (avoid additional edits)."
            << std::endl;

  std::mutex mtx;
  std::condition_variable cv;
  std::string observed;
  bool saw_enter = false;

  Listener listener;
  auto cb = [&](char32_t cp, Key key, Modifier /*mods*/, bool pressed) {
    // Use key release events to observe characters and edits.
    if (pressed) {
      AXIDEV_IO_LOG_DEBUG("Listener test cb: ignoring press event key=%s cp=%u",
                          keyToString(key).c_str(), static_cast<unsigned>(cp));
      return;
    }
    std::lock_guard<std::mutex> lk(mtx);
    if (cp != 0) {
      appendUtf8(observed, cp);
      AXIDEV_IO_LOG_DEBUG(
          "Listener test cb: appended cp=%u key=%s observed='%s'",
          static_cast<unsigned>(cp), keyToString(key).c_str(),
          observed.c_str());
    } else if (key == Key::Backspace) {
      popLastUtf8Char(observed);
      AXIDEV_IO_LOG_DEBUG("Listener test cb: backspace - observed='%s'",
                          observed.c_str());
    } else if (key == Key::Enter) {
      saw_enter = true;
      AXIDEV_IO_LOG_DEBUG("Listener test cb: enter - observed='%s'",
                          observed.c_str());
      cv.notify_one();
    } else {
      AXIDEV_IO_LOG_DEBUG(
          "Listener test cb: non-printable key=%s cp=0 observed='%s'",
          keyToString(key).c_str(), observed.c_str());
    }
  };

  bool ok = listener.start(cb);
  if (!ok) {
    AXIDEV_IO_LOG_INFO("Listener could not start - skipping backspace test");
    GTEST_SKIP()
        << "Listener not available on this platform or permission denied.";
  }

  std::string typed;
  std::getline(std::cin, typed);

  {
    std::unique_lock<std::mutex> lk(mtx);
    cv.wait_for(lk, 2s, [&] { return saw_enter; });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  listener.stop();

  {
    std::lock_guard<std::mutex> lk(mtx);
    AXIDEV_IO_LOG_INFO(
        "Integration test: expected='%s' typed='%s' observed='%s'",
        expected.c_str(), typed.c_str(), observed.c_str());
    ASSERT_EQ(typed, expected)
        << "Expected: " << expected << ", Typed: " << typed;
    EXPECT_EQ(observed, expected)
        << "Expected: " << expected << ", Observed: " << observed;
  }
}

TEST_F(ListenerIntegrationTest, ModifiersAndShiftState) {
  AXIDEV_IO_LOG_INFO("Integration test: Listener Modifiers & Shift State");
  const std::string expected = "HELLO";
  std::cout << "[RUNNING] Type the following exact string using SHIFT for "
               "uppercase\n"
            << "and press [ENTER]:\n\n"
            << "  " << expected << "\n\n"
            << "Type it exactly (use SHIFT to produce uppercase letters)."
            << std::endl;

  std::mutex mtx;
  std::condition_variable cv;
  std::string observed;
  bool saw_enter = false;

  Listener listener;
  auto cb = [&](char32_t cp, Key key, Modifier /*mods*/, bool pressed) {
    // Prefer release events for observed characters to avoid mismatches
    // between low-level hook timing and terminal input.
    if (pressed) {
      AXIDEV_IO_LOG_DEBUG("Listener test cb: ignoring press event key=%s cp=%u",
                          keyToString(key).c_str(), static_cast<unsigned>(cp));
      return;
    }
    std::lock_guard<std::mutex> lk(mtx);
    if (cp != 0) {
      appendUtf8(observed, cp);
      AXIDEV_IO_LOG_DEBUG(
          "Listener test cb: appended cp=%u key=%s observed='%s'",
          static_cast<unsigned>(cp), keyToString(key).c_str(),
          observed.c_str());
    } else if (key == Key::Backspace) {
      popLastUtf8Char(observed);
      AXIDEV_IO_LOG_DEBUG("Listener test cb: backspace - observed='%s'",
                          observed.c_str());
    } else if (key == Key::Enter) {
      saw_enter = true;
      AXIDEV_IO_LOG_DEBUG("Listener test cb: enter - observed='%s'",
                          observed.c_str());
      cv.notify_one();
    } else {
      AXIDEV_IO_LOG_DEBUG(
          "Listener test cb: non-printable key=%s cp=0 observed='%s'",
          keyToString(key).c_str(), observed.c_str());
    }
  };

  bool ok = listener.start(cb);
  if (!ok) {
    AXIDEV_IO_LOG_INFO("Listener could not start - skipping modifiers test");
    GTEST_SKIP()
        << "Listener not available on this platform or permission denied.";
  }

  std::string typed;
  std::getline(std::cin, typed);

  {
    std::unique_lock<std::mutex> lk(mtx);
    cv.wait_for(lk, 2s, [&] { return saw_enter; });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  listener.stop();

  {
    std::lock_guard<std::mutex> lk(mtx);
    AXIDEV_IO_LOG_INFO(
        "Integration test: expected='%s' typed='%s' observed='%s'",
        expected.c_str(), typed.c_str(), observed.c_str());
    ASSERT_EQ(typed, expected)
        << "Expected: " << expected << ", Typed: " << typed;
    EXPECT_EQ(observed, expected)
        << "Expected: " << expected << ", Observed: " << observed;
  }
}
