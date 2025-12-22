#include <typr-io/typr_io.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char **argv) {
  typr::io::Sender sender;
  auto caps = sender.capabilities();

  std::cout << "typr-io consumer\n";
  std::cout << "  canInjectKeys: " << (caps.canInjectKeys ? "yes" : "no")
            << "\n";
  std::cout << "  canInjectText: " << (caps.canInjectText ? "yes" : "no")
            << "\n";
  std::cout << "  canSimulateHID: " << (caps.canSimulateHID ? "yes" : "no")
            << "\n\n";

  if (argc <= 1) {
    std::cout
        << "Usage:\n"
        << "  --type <text>    : inject text (if supported)\n"
        << "  --tap <KEYNAME>  : tap the named key (e.g., A, Enter, F1)\n"
        << "  --listen <secs>  : listen for global key events for N seconds\n"
        << "  --help           : show this help\n";
    return 0;
  }

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help") {
      // Already printed above
      continue;

    } else if (arg == "--type") {
      if (i + 1 >= argc) {
        std::cerr << "--type requires an argument\n";
        return 1;
      }
      std::string text = argv[++i];
      if (!caps.canInjectText) {
        std::cerr << "Backend cannot inject arbitrary text on this "
                     "platform/backend\n";
      } else {
        std::cout << "Attempting to type: \"" << text << "\"\n";
        bool ok = sender.typeText(text);
        std::cout << (ok ? "-> Success\n" : "-> Failed\n");
      }

    } else if (arg == "--tap") {
      if (i + 1 >= argc) {
        std::cerr << "--tap requires a key name (e.g., A, Enter, F1)\n";
        return 1;
      }
      std::string keyName = argv[++i];
      typr::io::Key k = typr::io::stringToKey(keyName);
      if (k == typr::io::Key::Unknown) {
        std::cerr << "Unknown key: " << keyName << "\n";
        continue;
      }
      if (!caps.canInjectKeys) {
        std::cerr << "Sender cannot inject physical keys on this platform\n";
        continue;
      }
      std::cout << "Tapping key: " << typr::io::keyToString(k) << "\n";
      bool ok = sender.tap(k);
      std::cout << (ok ? "-> Success\n" : "-> Failed\n");

    } else if (arg == "--listen") {
      if (i + 1 >= argc) {
        std::cerr << "--listen requires a duration in seconds\n";
        return 1;
      }
      int seconds = 0;
      try {
        seconds = std::stoi(argv[++i]);
      } catch (...) {
        std::cerr << "Invalid number for --listen\n";
        return 1;
      }

      typr::io::Listener listener;
      bool started = listener.start([](char32_t codepoint, typr::io::Key key,
                                       typr::io::Modifier mods, bool pressed) {
        std::cout << (pressed ? "[press]  " : "[release] ")
                  << "Key=" << typr::io::keyToString(key)
                  << " CP=" << static_cast<unsigned>(codepoint) << " Mods=0x"
                  << std::hex << static_cast<int>(static_cast<uint8_t>(mods))
                  << std::dec << "\n";
      });

      if (!started) {
        std::cerr
            << "Listener failed to start (permissions / platform support?)\n";
        continue;
      }
      std::cout << "Listening for " << seconds << " second(s)...\n";
      std::this_thread::sleep_for(std::chrono::seconds(seconds));
      listener.stop();
      std::cout << "Stopped listening\n";

    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return 1;
    }
  }

  return 0;
}
