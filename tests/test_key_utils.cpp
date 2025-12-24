// test_key_utils.cpp
// Comprehensive unit tests for key/string utilities, modifiers, and
// capabilities.
//
// These tests use Catch2 (v3+) and exercise:
//  - keyToString / stringToKey round-trip behavior and uniqueness of canonical
//  names
//  - alias/synonym lookups (e.g., \"esc\" -> Escape, \"kp1\" -> Numpad1)
//  - edge cases (invalid strings, whitespace handling)
//  - Modifier bit-ops and hasModifier helper
//  - Capabilities default values
//
// To run these tests enable TYPR_IO_BUILD_TESTS=ON when configuring the
// project.

#include <catch2/catch_all.hpp>

#include <typr-io/core.hpp>
#include <typr-io/log.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace typr::io;

static std::string toLowerCopy(const std::string &s) {
  std::string out = s;
  std::ranges::transform(out, out.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return out;
}

TEST_CASE("keyToString / stringToKey roundtrip and uniqueness", "[key_utils]") {
  TYPR_IO_LOG_INFO("test_key_utils: roundtrip/uniqueness start");
  std::unordered_set<std::string> seen;
  int canonicalCount = 0;

  // Precompute lowercase/uppercase canonical counts to detect case collisions
  // (e.g., X11 / XF86 aliases like "OE" vs "oe") so the test can avoid
  // impossible assertions when two canonical names differ only by case.
  std::unordered_map<std::string, int> lowerCounts;
  std::unordered_map<std::string, int> upperCounts;
  for (unsigned j = 0; j <= 255u; ++j) {
    Key kk = static_cast<Key>(j);
    std::string nm = keyToString(kk);
    if (nm == "Unknown")
      continue;
    ++lowerCounts[toLowerCopy(nm)];
    std::string up = nm;
    std::ranges::transform(up, up.begin(), [](char c) {
      return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    ++upperCounts[up];
  }

  for (unsigned i = 0; i <= 255u; ++i) {
    Key k = static_cast<Key>(i);
    std::string name = keyToString(k);

    // Canonical round-trip: when the canonical name is "Unknown" the
    // mapping should resolve to Key::Unknown. For all other canonical
    // names, the round-trip should return the same Key value.
    if (name == "Unknown") {
      REQUIRE(stringToKey(name) == Key::Unknown);
    } else {
      REQUIRE(stringToKey(name) == k);
    }

    // Count & uniqueness of non-Unknown canonical names
    if (name != "Unknown") {
      ++canonicalCount;
      auto [it, inserted] = seen.emplace(name);
      REQUIRE(inserted); // canonical names must be unique

      // Lowercased canonical should map back when unambiguous.
      // If multiple canonical names collapse to the same lowercased string
      // (for example, both \"OE\" and \"oe\" present), skip the assertion to
      // avoid impossible-to-satisfy checks (both can't round-trip from the
      // identical lowercased string).
      std::string lower = toLowerCopy(name);
      if (lowerCounts[lower] == 1) {
        REQUIRE(stringToKey(lower) == k);
      } else {
        TYPR_IO_LOG_DEBUG(
            "Skipping lowercased roundtrip for ambiguous canonical name '%s'",
            name.c_str());
      }

      // Uppercased canonical should also map back when unambiguous.
      std::string upper = name;
      std::ranges::transform(upper, upper.begin(), [](char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      });
      if (upperCounts[upper] == 1) {
        REQUIRE(stringToKey(upper) == k);
      } else {
        TYPR_IO_LOG_DEBUG(
            "Skipping uppercased roundtrip for ambiguous canonical name '%s'",
            name.c_str());
      }
    }
  }

  // Sanity: ensure we found a healthy number of canonical keys
  REQUIRE(canonicalCount > 40);
}

TEST_CASE("Recognizes helpful aliases / synonyms", "[key_utils][aliases]") {
  TYPR_IO_LOG_INFO("test_key_utils: aliases/synonyms start");
  REQUIRE(stringToKey("esc") == Key::Escape);
  REQUIRE(stringToKey("ESC") == Key::Escape);
  REQUIRE(stringToKey("return") == Key::Enter);
  REQUIRE(stringToKey("spacebar") == Key::Space);
  REQUIRE(stringToKey("space") == Key::Space);
  REQUIRE(stringToKey("ctrl") == Key::CtrlLeft);
  REQUIRE(stringToKey("control") == Key::CtrlLeft);
  REQUIRE(stringToKey("shift") == Key::ShiftLeft);
  REQUIRE(stringToKey("alt") == Key::AltLeft);
  REQUIRE(stringToKey("super") == Key::SuperLeft);
  REQUIRE(stringToKey("meta") == Key::SuperLeft);
  REQUIRE(stringToKey("win") == Key::SuperLeft);

  REQUIRE(stringToKey("num0") == Key::Num0);
  REQUIRE(stringToKey("num1") == Key::Num1);
  REQUIRE(stringToKey("num2") == Key::Num2);

  REQUIRE(stringToKey("dash") == Key::Minus);
  REQUIRE(stringToKey("hyphen") == Key::Minus);
  REQUIRE(stringToKey("minus") == Key::Minus);
  REQUIRE(stringToKey("-") == Key::Minus);

  REQUIRE(stringToKey("grave") == Key::Grave);
  REQUIRE(stringToKey("`") == Key::Grave);

  REQUIRE(stringToKey("backslash") == Key::Backslash);
  REQUIRE(stringToKey("\\") == Key::Backslash);

  REQUIRE(stringToKey("bracketleft") == Key::LeftBracket);
  REQUIRE(stringToKey("bracketright") == Key::RightBracket);

  REQUIRE(stringToKey("kp0") == Key::Numpad0);
  REQUIRE(stringToKey("kp1") == Key::Numpad1);
  REQUIRE(stringToKey("kp9") == Key::Numpad9);
  REQUIRE(stringToKey("numpad1") == Key::Numpad1);

  REQUIRE(stringToKey("dot") == Key::Period);
  REQUIRE(stringToKey("period") == Key::Period);

  // Additional symbol aliases commonly encountered on US-style keyboard
  // layouts. The implementation maps these to named symbol keys (e.g.,
  // "@" -> Key::At) rather than their shifted numeric equivalents.
  REQUIRE(stringToKey("@") == Key::At);
  REQUIRE(stringToKey("hash") == Key::Hashtag);
  REQUIRE(stringToKey("hashtag") == Key::Hashtag);
  REQUIRE(stringToKey("pound") == Key::Hashtag);
  REQUIRE(stringToKey("!") == Key::Exclamation);
  REQUIRE(stringToKey("$") == Key::Dollar);
  REQUIRE(stringToKey("percent") == Key::Percent);
  REQUIRE(stringToKey("^") == Key::Caret);
  REQUIRE(stringToKey("&") == Key::Ampersand);
  REQUIRE(stringToKey("*") == Key::Asterisk);
  REQUIRE(stringToKey("(") == Key::LeftParen);
  REQUIRE(stringToKey(")") == Key::RightParen);

  REQUIRE(stringToKey("_") == Key::Minus);
  REQUIRE(stringToKey("+") == Key::Equal);
  REQUIRE(stringToKey("|") == Key::Backslash);
  REQUIRE(stringToKey("~") == Key::Grave);
  REQUIRE(stringToKey(":") == Key::Semicolon);
  REQUIRE(stringToKey("\"") == Key::Apostrophe);
  REQUIRE(stringToKey("<") == Key::Comma);
  REQUIRE(stringToKey(">") == Key::Period);
  REQUIRE(stringToKey("?") == Key::Slash);

  // Whitespace aliases
  REQUIRE(stringToKey(" ") == Key::Space);
  REQUIRE(stringToKey("\t") == Key::Tab);
}
TEST_CASE("Recognizes X11 / XF86 / KP alias names", "[key_utils][x11]") {
  TYPR_IO_LOG_INFO("test_key_utils: x11 aliases start");

  // Modifier / control variants
  REQUIRE(stringToKey("Control_L") == Key::CtrlLeft);
  REQUIRE(stringToKey("Control_R") == Key::CtrlRight);
  REQUIRE(stringToKey("Shift_L") == Key::ShiftLeft);
  REQUIRE(stringToKey("Shift_R") == Key::ShiftRight);
  REQUIRE(stringToKey("Alt_L") == Key::AltLeft);
  REQUIRE(stringToKey("Meta_L") == Key::SuperLeft);
  REQUIRE(stringToKey("ISO_Left_Tab") == Key::Tab);
  REQUIRE(stringToKey("ISO_Level3_Shift") == Key::AltRight);

  // X11 punctuation / named symbols
  REQUIRE(stringToKey("quotedbl") == Key::Quote);
  REQUIRE(stringToKey("parenleft") == Key::LeftParen);
  REQUIRE(stringToKey("parenright") == Key::RightParen);
  REQUIRE(stringToKey("equal") == Key::Equal);
  REQUIRE(stringToKey("question") == Key::QuestionMark);
  REQUIRE(stringToKey("exclam") == Key::Exclamation);
  REQUIRE(stringToKey("section") == Key::Section);
  REQUIRE(stringToKey("degree") == Key::Degree);
  REQUIRE(stringToKey("sterling") == Key::Sterling);
  REQUIRE(stringToKey("plusminus") == Key::PlusMinus);

  // Accented/locale keys
  REQUIRE(stringToKey("eacute") == Key::E);
  REQUIRE(stringToKey("egrave") == Key::E);
  REQUIRE(stringToKey("agrave") == Key::A);
  REQUIRE(stringToKey("ugrave") == Key::U);
  REQUIRE(stringToKey("ccedilla") == Key::C);
  REQUIRE(stringToKey("oe") == Key::oe);
  REQUIRE(stringToKey("OE") == Key::OE);
  REQUIRE(stringToKey("mu") == Key::Mu);

  // Linefeed / control synonyms
  REQUIRE(stringToKey("linefeed") == Key::Enter);
  REQUIRE(stringToKey("prior") == Key::PageUp);
  REQUIRE(stringToKey("next") == Key::PageDown);

  // Numeric keypad / KP_* variants (underscore and non-underscore forms)
  REQUIRE(stringToKey("KP_Multiply") == Key::NumpadMultiply);
  REQUIRE(stringToKey("kp_multiply") == Key::NumpadMultiply);
  REQUIRE(stringToKey("KP_Divide") == Key::NumpadDivide);
  REQUIRE(stringToKey("KP_Enter") == Key::NumpadEnter);
  REQUIRE(stringToKey("KP_Equal") == Key::NumpadEqual);
  REQUIRE(stringToKey("KP_7") == Key::Numpad7);
  REQUIRE(stringToKey("KP_Up") == Key::Numpad8);
  REQUIRE(stringToKey("KP_Decimal") == Key::NumpadDecimal);

  // XF86 / multimedia / hardware keys
  REQUIRE(stringToKey("XF86AudioMute") == Key::Mute);
  REQUIRE(stringToKey("XF86AudioLowerVolume") == Key::VolumeDown);
  REQUIRE(stringToKey("XF86AudioRaiseVolume") == Key::VolumeUp);
  REQUIRE(stringToKey("XF86AudioPlay") == Key::MediaPlayPause);
  REQUIRE(stringToKey("XF86AudioNext") == Key::MediaNext);
  REQUIRE(stringToKey("XF86Eject") == Key::Eject);
  REQUIRE(stringToKey("XF86MonBrightnessDown") == Key::BrightnessDown);
  REQUIRE(stringToKey("XF86MonBrightnessUp") == Key::BrightnessUp);
  REQUIRE(stringToKey("XF86Launch1") == Key::Launch1);
  REQUIRE(stringToKey("XF86LaunchA") == Key::LaunchA);
  REQUIRE(stringToKey("XF86KbdBrightnessDown") == Key::KbdBrightnessDown);
  REQUIRE(stringToKey("XF86KbdBrightnessUp") == Key::KbdBrightnessUp);

  // Ensure case-insensitivity / underscore handling is robust
  REQUIRE(stringToKey("kp_multiply") == Key::NumpadMultiply);
  REQUIRE(stringToKey("KP_Multiply") == stringToKey("kp_multiply"));
}
TEST_CASE("Recognizes ASCII control inputs", "[key_utils][ascii]") {
  TYPR_IO_LOG_INFO("test_key_utils: ascii controls start");
  // Control characters commonly observed in terminal / listener input.
  REQUIRE(stringToKey("\x08") == Key::Backspace);
  REQUIRE(stringToKey("\x03") == Key::AsciiETX);
  REQUIRE(stringToKey("\x1B") == Key::Escape);
  REQUIRE(stringToKey("\x1D") == Key::AsciiGS);
  REQUIRE(stringToKey("\x1C") == Key::AsciiFS);
  REQUIRE(stringToKey("\x1F") == Key::AsciiUS);
  REQUIRE(stringToKey("\x1E") == Key::AsciiRS);
  REQUIRE(stringToKey("\x10") == Key::AsciiDLE);
  REQUIRE(stringToKey("\x05") == Key::AsciiENQ);
  REQUIRE(stringToKey("\x01") == Key::AsciiSOH);
  REQUIRE(stringToKey("\x0B") == Key::AsciiVT);
  REQUIRE(stringToKey("\x0C") == Key::AsciiFF);
  REQUIRE(stringToKey("\x04") == Key::AsciiEOT);
  REQUIRE(stringToKey("\x7F") == Key::Delete);
  // Common newline/whitespace control mappings
  REQUIRE(stringToKey("\n") == Key::Enter);
  REQUIRE(stringToKey("\r") == Key::Enter);
  REQUIRE(stringToKey("\t") == Key::Tab);
}
TEST_CASE("Handles invalid and edge-case inputs", "[key_utils][edge]") {
  TYPR_IO_LOG_INFO("test_key_utils: edge-case inputs start");
  REQUIRE(stringToKey("NotAKey") == Key::Unknown);
  REQUIRE(stringToKey("") == Key::Unknown);
  // whitespace is NOT trimmed by design
  REQUIRE(stringToKey(" Enter") == Key::Unknown);
  REQUIRE(stringToKey("Enter ") == Key::Unknown);
}

TEST_CASE("keyToString returns expected canonical values",
          "[key_utils][canonical]") {
  TYPR_IO_LOG_INFO("test_key_utils: canonical values start");
  REQUIRE(keyToString(Key::A) == "A");
  REQUIRE(keyToString(Key::Num1) == "1");
  REQUIRE(keyToString(Key::F5) == "F5");
  REQUIRE(keyToString(Key::Tab) == "Tab");
  REQUIRE(keyToString(Key::Period) == ".");
  REQUIRE(keyToString(Key::Backslash) == "\\");
  REQUIRE(keyToString(Key::Minus) == "-");
}

TEST_CASE("Modifier bit-ops and helpers", "[modifier]") {
  TYPR_IO_LOG_INFO("test_key_utils: modifier bit-ops start");
  Modifier m = Modifier::None;
  REQUIRE(!hasModifier(m, Modifier::Shift));

  m |= Modifier::Shift;
  REQUIRE(hasModifier(m, Modifier::Shift));

  m |= Modifier::Ctrl;
  REQUIRE(hasModifier(m, Modifier::Shift));
  REQUIRE(hasModifier(m, Modifier::Ctrl));
  REQUIRE(!hasModifier(m, Modifier::Alt));

  Modifier n = m & Modifier::Ctrl;
  REQUIRE(hasModifier(n, Modifier::Ctrl));
  REQUIRE((!hasModifier(n, Modifier::Shift) || (n == Modifier::Ctrl)));

  m &= Modifier::Shift; // keep only Shift
  REQUIRE(hasModifier(m, Modifier::Shift));
  REQUIRE(!hasModifier(m, Modifier::Ctrl));
}

TEST_CASE("Capabilities defaults to false", "[capabilities]") {
  TYPR_IO_LOG_INFO("test_key_utils: capabilities defaults start");
  Capabilities c;
  REQUIRE(!c.canInjectKeys);
  REQUIRE(!c.canInjectText);
  REQUIRE(!c.canSimulateHID);
  REQUIRE(!c.supportsKeyRepeat);
  REQUIRE(!c.needsAccessibilityPerm);
  REQUIRE(!c.needsInputMonitoringPerm);
  REQUIRE(!c.needsUinputAccess);
}
