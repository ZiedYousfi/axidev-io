#include <typr-io/keyboard/listener.hpp>
#include <typr-io/keyboard/sender.hpp>
#include <typr-io/log.hpp>

#include <chrono>
#include <csignal>

#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static volatile sig_atomic_t g_sigint_received = 0;
static void playground_sig_handler(int) { g_sigint_received = 1; }
int main(int argc, char **argv) {
  typr::io::keyboard::Sender sender;
  auto caps = sender.capabilities();
  TYPR_IO_LOG_INFO("test_consumer: started argc=%d", argc);

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
        << "  --type <text>         : inject text (if supported)\n"
        << "  --tap <KEYNAME>       : tap the named key (e.g., A, Enter, F1)\n"
        << "  --listen <secs>       : listen for global key events for N "
           "seconds\n"
        << "  --request-permissions : attempt to request runtime platform "
           "permissions (e.g., macOS Accessibility)\n"
        << "  --playground send [--wait <secs>] [--repeat <N>] [--interval "
           "<secs>] (--type <text> | --tap <KEYNAME>)\n"
        << "      : send input after optional wait; can repeat (useful for "
           "background testing)\n"
        << "  --playground listen [--duration <secs>]\n"
        << "      : start a listener that collects events and prints them when "
           "stopped; omit --duration to run until Ctrl+C\n"
        << "  --help                : show this help\n";
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
        TYPR_IO_LOG_INFO("test_consumer: attempting to type text len=%zu",
                         text.size());
        std::cout << "Attempting to type: \"" << text << "\"\n";
        bool ok = sender.typeText(text);
        TYPR_IO_LOG_INFO("test_consumer: typeText result=%u",
                         static_cast<unsigned>(ok));
        std::cout << (ok ? "-> Success\n" : "-> Failed\n");
      }

    } else if (arg == "--tap") {
      if (i + 1 >= argc) {
        std::cerr << "--tap requires a key name (e.g., A, Enter, F1)\n";
        return 1;
      }
      std::string keyName = argv[++i];
      typr::io::keyboard::Key k = typr::io::keyboard::stringToKey(keyName);
      if (k == typr::io::keyboard::Key::Unknown) {
        std::cerr << "Unknown key: " << keyName << "\n";
        continue;
      }
      if (!caps.canInjectKeys) {
        std::cerr << "Sender cannot inject physical keys on this platform\n";
        continue;
      }
      TYPR_IO_LOG_INFO("test_consumer: tapping key=%s", keyName.c_str());
      std::cout << "Tapping key: " << typr::io::keyboard::keyToString(k) << "\n";
      bool ok = sender.tap(k);
      TYPR_IO_LOG_INFO("test_consumer: tap result=%u",
                       static_cast<unsigned>(ok));
      std::cout << (ok ? "-> Success\n" : "-> Failed\n");

    } else if (arg == "--request-permissions") {
      std::cout << "Requesting runtime permissions (may prompt the OS)...\n";
      bool permOk = sender.requestPermissions();
      TYPR_IO_LOG_INFO("test_consumer: requestPermissions -> %u",
                       static_cast<unsigned>(permOk));
      std::cout
          << (permOk
                  ? "-> Sender reports ready to inject\n"
                  : "-> Sender reports not ready (permission not granted?)\n");
      auto newCaps = sender.capabilities();
      TYPR_IO_LOG_DEBUG("test_consumer: newCaps canInjectKeys=%u "
                        "canInjectText=%u canSimulateHID=%u",
                        static_cast<unsigned>(newCaps.canInjectKeys),
                        static_cast<unsigned>(newCaps.canInjectText),
                        static_cast<unsigned>(newCaps.canSimulateHID));
      std::cout << "  canInjectKeys: " << (newCaps.canInjectKeys ? "yes" : "no")
                << "\n";
      std::cout << "  canInjectText: " << (newCaps.canInjectText ? "yes" : "no")
                << "\n";
      std::cout << "  canSimulateHID: "
                << (newCaps.canSimulateHID ? "yes" : "no") << "\n\n";

      std::cout << "Attempting to start a Listener to check Input Monitoring "
                   "permission...\n";
      TYPR_IO_LOG_INFO("test_consumer: attempting to start temporary listener "
                       "to check input-monitoring permission");
      {
        typr::io::keyboard::Listener tmpListener;
        bool started = tmpListener.start(
            [](char32_t, typr::io::keyboard::Key, typr::io::keyboard::Modifier, bool) {});
        TYPR_IO_LOG_INFO("test_consumer: temporary listener started=%u",
                         static_cast<unsigned>(started));
        if (!started) {
          std::cout << "-> Listener failed to start (Input Monitoring "
                       "permission may be required on macOS).\n";
          TYPR_IO_LOG_WARN("test_consumer: temporary listener failed to start");
        } else {
          std::cout << "-> Listener started successfully.\n";
          tmpListener.stop();
          TYPR_IO_LOG_INFO("test_consumer: temporary listener stopped");
        }
      }

    } else if (arg == "--playground") {
      if (i + 1 >= argc) {
        std::cerr << "--playground requires an action: send|listen\n";
        return 1;
      }
      std::string action = argv[++i];

      if (action == "send") {
        int waitSec = 0;
        int repeat = 1;
        int interval = 0;
        bool doType = false;
        bool doTap = false;
        std::string text;
        typr::io::keyboard::Key tapKey = typr::io::keyboard::Key::Unknown;

        while (i + 1 < argc) {
          std::string sub = argv[i + 1];
          if (sub == "--wait") {
            if (i + 2 >= argc) {
              std::cerr << "--wait requires a duration in seconds\n";
              return 1;
            }
            i += 2;
            try {
              waitSec = std::stoi(argv[i]);
            } catch (...) {
              std::cerr << "Invalid number for --wait\n";
              return 1;
            }
          } else if (sub == "--type") {
            if (i + 2 >= argc) {
              std::cerr << "--type requires an argument\n";
              return 1;
            }
            i += 2;
            text = argv[i];
            doType = true;
          } else if (sub == "--tap") {
            if (i + 2 >= argc) {
              std::cerr << "--tap requires a key name (e.g., A, Enter, F1)\n";
              return 1;
            }
            i += 2;
            std::string keyName = argv[i];
            typr::io::keyboard::Key k = typr::io::keyboard::stringToKey(keyName);
            if (k == typr::io::keyboard::Key::Unknown) {
              std::cerr << "Unknown key: " << keyName << "\n";
              return 1;
            }
            tapKey = k;
            doTap = true;
          } else if (sub == "--repeat") {
            if (i + 2 >= argc) {
              std::cerr << "--repeat requires a number\n";
              return 1;
            }
            i += 2;
            try {
              repeat = std::stoi(argv[i]);
            } catch (...) {
              std::cerr << "Invalid number for --repeat\n";
              return 1;
            }
          } else if (sub == "--interval") {
            if (i + 2 >= argc) {
              std::cerr << "--interval requires seconds\n";
              return 1;
            }
            i += 2;
            try {
              interval = std::stoi(argv[i]);
            } catch (...) {
              std::cerr << "Invalid number for --interval\n";
              return 1;
            }
          } else {
            break;
          }
        }

        if (!doType && !doTap) {
          std::cerr << "--playground send requires --type or --tap\n";
          return 1;
        }

        if (waitSec > 0) {
          TYPR_IO_LOG_INFO(
              "test_consumer: playground will wait %d second(s) before sending",
              waitSec);
          std::cout << "Waiting for " << waitSec
                    << " second(s) before sending...\n";
          std::this_thread::sleep_for(std::chrono::seconds(waitSec));
        }

        // Allow Ctrl+C to stop playground send; support repeat==0 as an
        // infinite loop. Save previous signal handlers so we can restore them
        // when done.
        g_sigint_received = 0;
        auto oldSigInt = std::signal(SIGINT, playground_sig_handler);
        auto oldSigTerm = std::signal(SIGTERM, playground_sig_handler);

        if (repeat == 0) {
          TYPR_IO_LOG_INFO("test_consumer: playground sending indefinitely");
          std::cout
              << "Playground: repeating indefinitely. Press Ctrl+C to stop.\n";
          while (!g_sigint_received) {
            if (doType) {
              if (!caps.canInjectText) {
                std::cerr << "Backend cannot inject arbitrary text on this "
                             "platform/backend\n";
              } else {
                TYPR_IO_LOG_INFO("test_consumer: playground: attempting to "
                                 "type text len=%zu",
                                 text.size());
                std::cout << "Playground: Attempting to type: \"" << text
                          << "\"\n";
                bool ok = sender.typeText(text);
                TYPR_IO_LOG_INFO(
                    "test_consumer: playground: typeText result=%u",
                    static_cast<unsigned>(ok));
                std::cout << (ok ? "-> Success\n" : "-> Failed\n");
              }
            }

            if (doTap) {
              if (!caps.canInjectKeys) {
                std::cerr
                    << "Sender cannot inject physical keys on this platform\n";
              } else {
                TYPR_IO_LOG_INFO("test_consumer: playground: tapping key=%s",
                                 typr::io::keyboard::keyToString(tapKey).c_str());
                std::cout << "Playground: Tapping key: "
                          << typr::io::keyboard::keyToString(tapKey) << "\n";
                bool ok = sender.tap(tapKey);
                TYPR_IO_LOG_INFO("test_consumer: playground: tap result=%u",
                                 static_cast<unsigned>(ok));
                std::cout << (ok ? "-> Success\n" : "-> Failed\n");
              }
            }

            if (interval > 0) {
              for (int waited = 0; waited < interval * 10 && !g_sigint_received;
                   ++waited) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
              }
            } else {
              // avoid busy-looping when no interval is specified
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
          }
          std::cout << "Playground: stopped by user\n";
        } else {
          for (int r = 0; r < repeat && !g_sigint_received; ++r) {
            if (doType) {
              if (!caps.canInjectText) {
                std::cerr << "Backend cannot inject arbitrary text on this "
                             "platform/backend\n";
              } else {
                TYPR_IO_LOG_INFO("test_consumer: playground: attempting to "
                                 "type text len=%zu",
                                 text.size());
                std::cout << "Playground: Attempting to type: \"" << text
                          << "\"\n";
                bool ok = sender.typeText(text);
                TYPR_IO_LOG_INFO(
                    "test_consumer: playground: typeText result=%u",
                    static_cast<unsigned>(ok));
                std::cout << (ok ? "-> Success\n" : "-> Failed\n");
              }
            }

            if (doTap) {
              if (!caps.canInjectKeys) {
                std::cerr
                    << "Sender cannot inject physical keys on this platform\n";
              } else {
                TYPR_IO_LOG_INFO("test_consumer: playground: tapping key=%s",
                                 typr::io::keyboard::keyToString(tapKey).c_str());
                std::cout << "Playground: Tapping key: "
                          << typr::io::keyboard::keyToString(tapKey) << "\n";
                bool ok = sender.tap(tapKey);
                TYPR_IO_LOG_INFO("test_consumer: playground: tap result=%u",
                                 static_cast<unsigned>(ok));
                std::cout << (ok ? "-> Success\n" : "-> Failed\n");
              }
            }

            if (r + 1 < repeat && interval > 0) {
              for (int waited = 0; waited < interval * 10 && !g_sigint_received;
                   ++waited) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
              }
            }
          }

          if (g_sigint_received) {
            std::cout << "Playground: stopped by user\n";
          }
          // Restore previous signal handlers from before the playground send
          std::signal(SIGINT, oldSigInt);
          std::signal(SIGTERM, oldSigTerm);
        }

      } else if (action == "listen") {
        int duration = -1;
        while (i + 1 < argc) {
          std::string sub = argv[i + 1];
          if (sub == "--duration") {
            if (i + 2 >= argc) {
              std::cerr << "--duration requires seconds\n";
              return 1;
            }
            i += 2;
            try {
              duration = std::stoi(argv[i]);
            } catch (...) {
              std::cerr << "Invalid number for --duration\n";
              return 1;
            }
          } else {
            break;
          }
        }

        TYPR_IO_LOG_INFO(
            "test_consumer: playground: starting listener (duration=%d)",
            duration);
        typr::io::keyboard::Listener listener;
        struct Event {
          std::chrono::steady_clock::time_point ts;
          char32_t codepoint;
          typr::io::keyboard::Key key;
          typr::io::keyboard::Modifier mods;
          bool pressed;
        };
        std::vector<Event> events;
        std::mutex events_mtx;
        g_sigint_received = 0;
        std::signal(SIGINT, playground_sig_handler);
        std::signal(SIGTERM, playground_sig_handler);

        bool started =
            listener.start([&](char32_t codepoint, typr::io::keyboard::Key key,
                               typr::io::keyboard::Modifier mods, bool pressed) {
              TYPR_IO_LOG_DEBUG("test_consumer: playground listener event %s "
                                "key=%s cp=%u mods=0x%02x",
                                pressed ? "press" : "release",
                                typr::io::keyboard::keyToString(key).c_str(),
                                static_cast<unsigned>(codepoint),
                                static_cast<int>(static_cast<uint8_t>(mods)));
              std::lock_guard<std::mutex> lg(events_mtx);
              events.push_back({std::chrono::steady_clock::now(), codepoint,
                                key, mods, pressed});
            });

        if (!started) {
          TYPR_IO_LOG_ERROR("test_consumer: playground listener failed to "
                            "start (permissions / platform support?)");
          std::cerr
              << "Listener failed to start (permissions / platform support?)\n";
          continue;
        }

        if (duration >= 0) {
          std::cout << "Playground listening for " << duration
                    << " second(s)...\n";
          std::this_thread::sleep_for(std::chrono::seconds(duration));
        } else {
          std::cout << "Playground listener started. Press Ctrl+C to stop and "
                       "print observed events.\n";
          while (!g_sigint_received) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
        }

        listener.stop();
        TYPR_IO_LOG_INFO(
            "test_consumer: playground listener stopped. Observed %zu event(s)",
            events.size());
        std::cout << "Playground listener stopped. Observed " << events.size()
                  << " event(s):\n";

        if (!events.empty()) {
          auto start_ts = events.front().ts;
          for (const auto &e : events) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          e.ts - start_ts)
                          .count();
            std::cout << "[" << ms << "ms] "
                      << (e.pressed ? "[press]  " : "[release] ")
                      << "Key=" << typr::io::keyboard::keyToString(e.key)
                      << " CP=" << static_cast<unsigned>(e.codepoint)
                      << " Mods=0x" << std::hex
                      << static_cast<int>(static_cast<uint8_t>(e.mods))
                      << std::dec << "\n";
          }
        }

      } else {
        std::cerr << "Unknown playground action: " << action << "\n";
        return 1;
      }

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

      TYPR_IO_LOG_INFO("test_consumer: starting listener for %d seconds",
                       seconds);
      typr::io::keyboard::Listener listener;
      bool started = listener.start([](char32_t codepoint, typr::io::keyboard::Key key,
                                       typr::io::keyboard::Modifier mods, bool pressed) {
        TYPR_IO_LOG_DEBUG(
            "test_consumer: listener event %s key=%s cp=%u mods=0x%02x",
            pressed ? "press" : "release", typr::io::keyboard::keyToString(key).c_str(),
            static_cast<unsigned>(codepoint),
            static_cast<int>(static_cast<uint8_t>(mods)));
        std::cout << (pressed ? "[press]  " : "[release] ")
                  << "Key=" << typr::io::keyboard::keyToString(key)
                  << " CP=" << static_cast<unsigned>(codepoint) << " Mods=0x"
                  << std::hex << static_cast<int>(static_cast<uint8_t>(mods))
                  << std::dec << "\n";
      });

      if (!started) {
        TYPR_IO_LOG_ERROR("test_consumer: listener failed to start "
                          "(permissions / platform support?)");
        std::cerr
            << "Listener failed to start (permissions / platform support?)\n";
        continue;
      }
      TYPR_IO_LOG_INFO("test_consumer: listener started");
      std::cout << "Listening for " << seconds << " second(s)...\n";
      std::this_thread::sleep_for(std::chrono::seconds(seconds));
      listener.stop();
      TYPR_IO_LOG_INFO("test_consumer: listener stopped");
      std::cout << "Stopped listening\n";

    } else {
      TYPR_IO_LOG_WARN("test_consumer: unknown argument: %s", arg.c_str());
      std::cerr << "Unknown argument: " << arg << "\n";
      return 1;
    }
  }

  TYPR_IO_LOG_INFO("test_consumer: exiting");
  return 0;
}
