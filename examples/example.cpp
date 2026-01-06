/*
 * Simple example for axidev-io showing basic usage.
 *
 * Build with:
 *   cmake -DAXIDEV_IO_BUILD_EXAMPLES=ON ..
 *   cmake --build .
 *
 * Run:
 *   ./axidev-io-example --help
 *
 * Note: Some functionality (global listening, text injection) may require
 * platform permissions (Accessibility / Input Monitoring on macOS, /dev/uinput
 * access on Linux, etc.)
 */

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <axidev-io/keyboard/listener.hpp>
#include <axidev-io/keyboard/sender.hpp>
#include <axidev-io/log.hpp>

int main(int argc, char **argv) {
  using namespace std::chrono_literals;

  axidev::io::keyboard::Sender sender;
  auto caps = sender.capabilities();
  AXIDEV_IO_LOG_INFO(
      "example: sender constructed; type=%d canInjectKeys=%d canInjectText=%d",
      static_cast<int>(sender.type()), static_cast<int>(caps.canInjectKeys),
      static_cast<int>(caps.canInjectText));

  std::cout << "axidev-io example\n";
  AXIDEV_IO_LOG_INFO("example: startup argc=%d", argc);
  std::cout << "  sender type (raw): " << static_cast<int>(sender.type())
            << "\n";
  std::cout << "  capabilities:\n";
  std::cout << "    canInjectKeys:   " << (caps.canInjectKeys ? "yes" : "no")
            << "\n";
  std::cout << "    canInjectText:   " << (caps.canInjectText ? "yes" : "no")
            << "\n";
  std::cout << "    canSimulateHID:  " << (caps.canSimulateHID ? "yes" : "no")
            << "\n";
  std::cout << "    supportsKeyRepeat: "
            << (caps.supportsKeyRepeat ? "yes" : "no") << "\n\n";

  if (argc <= 1) {
    std::cout
        << "Usage:\n"
        << "  --type \"text\"    : inject text (if supported by backend)\n"
        << "  --tap KEYNAME     : tap the named key (e.g., A, Enter, F1)\n"
        << "  --combo COMBO     : parse and execute a key combo (e.g., "
           "Shift+A, Ctrl+C)\n"
        << "  --parse COMBO     : parse a key combo string and show its "
           "components\n"
        << "  --listen N        : listen for global key events for N seconds\n"
        << "  --help            : show this text\n";
    return 0;
  }

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help") {
      // Already printed basic usage above
      continue;

    } else if (arg == "--type") {
      if (i + 1 >= argc) {
        std::cerr << "--type requires an argument\n";
        return 1;
      }
      std::string text = argv[++i];
      AXIDEV_IO_LOG_INFO("example: attempting to type: \"%s\"", text.c_str());
      if (!caps.canInjectText) {
        std::cerr << "Backend cannot inject arbitrary text on this "
                     "platform/back-end\n";
      } else {
        std::cout << "Attempting to type: \"" << text << "\"\n";
        bool ok = sender.typeText(text);
        AXIDEV_IO_LOG_INFO("example: typeText result=%u",
                         static_cast<unsigned>(ok));
        std::cout << (ok ? "-> Success\n" : "-> Failed\n");
      }

    } else if (arg == "--tap") {
      if (i + 1 >= argc) {
        std::cerr << "--tap requires a key combo (e.g., A, Shift+A, Ctrl+C)\n";
        return 1;
      }
      std::string keyCombo = argv[++i];
      auto kwm = axidev::io::keyboard::stringToKeyWithModifier(keyCombo);
      if (kwm.key == axidev::io::keyboard::Key::Unknown) {
        std::cerr << "Unknown key: " << keyCombo << "\n";
        continue;
      }
      if (!caps.canInjectKeys) {
        std::cerr << "Sender cannot inject physical keys on this platform\n";
        continue;
      }
      std::cout << "Tapping: "
                << axidev::io::keyboard::keyToStringWithModifier(
                       kwm.key, kwm.requiredMods)
                << "\n";
      AXIDEV_IO_LOG_INFO(
          "example: tapping key=%s mods=0x%02x",
          axidev::io::keyboard::keyToString(kwm.key).c_str(),
          static_cast<int>(static_cast<uint8_t>(kwm.requiredMods)));
      bool ok = sender.tap(kwm);
      AXIDEV_IO_LOG_INFO("example: tap result=%u", static_cast<unsigned>(ok));
      std::cout << (ok ? "-> Success\n" : "-> Failed\n");

    } else if (arg == "--combo") {
      // --combo is now an alias for --tap since tap accepts KeyWithModifier
      if (i + 1 >= argc) {
        std::cerr
            << "--combo requires a combo string (e.g., Shift+A, Ctrl+C)\n";
        return 1;
      }
      std::string comboStr = argv[++i];
      auto kwm = axidev::io::keyboard::stringToKeyWithModifier(comboStr);
      if (kwm.key == axidev::io::keyboard::Key::Unknown) {
        std::cerr << "Unknown key in combo: " << comboStr << "\n";
        continue;
      }
      if (!caps.canInjectKeys) {
        std::cerr << "Sender cannot inject physical keys on this platform\n";
        continue;
      }
      std::cout << "Executing combo: "
                << axidev::io::keyboard::keyToStringWithModifier(
                       kwm.key, kwm.requiredMods)
                << "\n";
      AXIDEV_IO_LOG_INFO(
          "example: combo key=%s mods=0x%02x",
          axidev::io::keyboard::keyToString(kwm.key).c_str(),
          static_cast<int>(static_cast<uint8_t>(kwm.requiredMods)));
      bool ok = sender.tap(kwm);
      AXIDEV_IO_LOG_INFO("example: combo result=%u", static_cast<unsigned>(ok));
      std::cout << (ok ? "-> Success\n" : "-> Failed\n");

    } else if (arg == "--parse") {
      if (i + 1 >= argc) {
        std::cerr << "--parse requires a combo string (e.g., Shift+A, "
                     "Ctrl+Shift+C)\n";
        return 1;
      }
      std::string comboStr = argv[++i];
      // Demonstrate the stringToKeyWithModifier API
      auto kwm = axidev::io::keyboard::stringToKeyWithModifier(comboStr);
      std::cout << "Parsed \"" << comboStr << "\":\n";
      std::cout << "  Key: " << axidev::io::keyboard::keyToString(kwm.key)
                << "\n";
      std::cout << "  Modifiers: ";
      if (kwm.requiredMods == axidev::io::keyboard::Modifier::None) {
        std::cout << "None";
      } else {
        bool first = true;
        if (axidev::io::keyboard::hasModifier(
                kwm.requiredMods, axidev::io::keyboard::Modifier::Shift)) {
          std::cout << (first ? "" : "+") << "Shift";
          first = false;
        }
        if (axidev::io::keyboard::hasModifier(
                kwm.requiredMods, axidev::io::keyboard::Modifier::Ctrl)) {
          std::cout << (first ? "" : "+") << "Ctrl";
          first = false;
        }
        if (axidev::io::keyboard::hasModifier(
                kwm.requiredMods, axidev::io::keyboard::Modifier::Alt)) {
          std::cout << (first ? "" : "+") << "Alt";
          first = false;
        }
        if (axidev::io::keyboard::hasModifier(
                kwm.requiredMods, axidev::io::keyboard::Modifier::Super)) {
          std::cout << (first ? "" : "+") << "Super";
          first = false;
        }
      }
      std::cout << "\n";
      std::cout << "  Canonical form: "
                << axidev::io::keyboard::keyToStringWithModifier(
                       kwm.key, kwm.requiredMods)
                << "\n";

    } else if (arg == "--listen") {
      if (i + 1 >= argc) {
        std::cerr << "--listen requires a duration in seconds\n";
        return 1;
      }
      int seconds = std::stoi(argv[++i]);
      AXIDEV_IO_LOG_INFO("example: starting listener for %d seconds", seconds);
      axidev::io::keyboard::Listener listener;
      bool started = listener.start(
          [](char32_t /*unused*/, axidev::io::keyboard::KeyWithModifier keyMod,
             bool pressed) {
            // Use the modifier-aware key-to-string conversion to get a
            // human-readable representation that includes modifier state.
            // For example, if Shift+A is pressed, this will show "Shift+A"
            // rather than just "A" with a separate hex modifier dump.
            std::string keyWithMods =
                axidev::io::keyboard::keyToStringWithModifier(
                    keyMod.key, keyMod.requiredMods);
            std::cout << (pressed ? "[press]  " : "[release] ") << keyWithMods
                      << "\n";
            AXIDEV_IO_LOG_DEBUG("example: listener %s key=%s",
                                pressed ? "press" : "release",
                                keyWithMods.c_str());
          });
      if (!started) {
        AXIDEV_IO_LOG_ERROR("example: listener failed to start");
        std::cerr
            << "Listener failed to start (permissions / platform support?)\n";
      } else {
        AXIDEV_IO_LOG_INFO("example: listener started");
        std::cout << "Listening for " << seconds << " seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        listener.stop();
        AXIDEV_IO_LOG_INFO("example: listener stopped");
        std::cout << "Stopped listening\n";
      }

    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return 1;
    }
  }

  return 0;
}
