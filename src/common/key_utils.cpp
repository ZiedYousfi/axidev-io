#include <typr-io/core.hpp>
#include <typr-io/log.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace typr {
namespace io {

namespace {

std::string toLower(std::string inputString) {
  std::ranges::transform(
      inputString, inputString.begin(), [](char character) -> char {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
      });
  return inputString;
}

// Escape input for debug logging so control characters (e.g., newline)
// don't break log lines. Non-printable characters are escaped as common
// sequences (\\n, \\t) or as \\xHH.
std::string escapeForLog(const std::string &input) {
  std::string out;
  out.reserve(input.size() * 2);
  for (unsigned char c : input) {
    switch (c) {
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (std::isprint(c)) {
        out += static_cast<char>(c);
      } else {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\x%02X", c);
        out += buf;
      }
    }
  }
  return out;
}

// Central list of canonical names for keys. These are used as the canonical
// string returned by `keyToString` and are used to seed the reverse map in
// `stringToKey`.
const std::vector<std::pair<Key, std::string>> &keyStringPairs() {
  static const std::vector<std::pair<Key, std::string>> pairs = {
      {Key::Unknown, "Unknown"},
      // Letters
      {Key::A, "A"},
      {Key::B, "B"},
      {Key::C, "C"},
      {Key::D, "D"},
      {Key::E, "E"},
      {Key::F, "F"},
      {Key::G, "G"},
      {Key::H, "H"},
      {Key::I, "I"},
      {Key::J, "J"},
      {Key::K, "K"},
      {Key::L, "L"},
      {Key::M, "M"},
      {Key::N, "N"},
      {Key::O, "O"},
      {Key::P, "P"},
      {Key::Q, "Q"},
      {Key::R, "R"},
      {Key::S, "S"},
      {Key::T, "T"},
      {Key::U, "U"},
      {Key::V, "V"},
      {Key::W, "W"},
      {Key::X, "X"},
      {Key::Y, "Y"},
      {Key::Z, "Z"},
      // Numbers (top row)
      {Key::Num0, "0"},
      {Key::Num1, "1"},
      {Key::Num2, "2"},
      {Key::Num3, "3"},
      {Key::Num4, "4"},
      {Key::Num5, "5"},
      {Key::Num6, "6"},
      {Key::Num7, "7"},
      {Key::Num8, "8"},
      {Key::Num9, "9"},
      // Function keys
      {Key::F1, "F1"},
      {Key::F2, "F2"},
      {Key::F3, "F3"},
      {Key::F4, "F4"},
      {Key::F5, "F5"},
      {Key::F6, "F6"},
      {Key::F7, "F7"},
      {Key::F8, "F8"},
      {Key::F9, "F9"},
      {Key::F10, "F10"},
      {Key::F11, "F11"},
      {Key::F12, "F12"},
      {Key::F13, "F13"},
      {Key::F14, "F14"},
      {Key::F15, "F15"},
      {Key::F16, "F16"},
      {Key::F17, "F17"},
      {Key::F18, "F18"},
      {Key::F19, "F19"},
      {Key::F20, "F20"},
      // Control keys
      {Key::Enter, "Enter"},
      {Key::Escape, "Escape"},
      {Key::Backspace, "Backspace"},
      {Key::Tab, "Tab"},
      {Key::Space, "Space"},
      // Navigation
      {Key::Left, "Left"},
      {Key::Right, "Right"},
      {Key::Up, "Up"},
      {Key::Down, "Down"},
      {Key::Home, "Home"},
      {Key::End, "End"},
      {Key::PageUp, "PageUp"},
      {Key::PageDown, "PageDown"},
      {Key::Delete, "Delete"},
      {Key::Insert, "Insert"},
      {Key::PrintScreen, "PrintScreen"},
      {Key::ScrollLock, "ScrollLock"},
      {Key::Pause, "Pause"},
      // Numpad
      {Key::NumpadDivide, "NumpadDivide"},
      {Key::NumpadMultiply, "NumpadMultiply"},
      {Key::NumpadMinus, "NumpadMinus"},
      {Key::NumpadPlus, "NumpadPlus"},
      {Key::NumpadEnter, "NumpadEnter"},
      {Key::NumpadDecimal, "NumpadDecimal"},
      {Key::Numpad0, "Numpad0"},
      {Key::Numpad1, "Numpad1"},
      {Key::Numpad2, "Numpad2"},
      {Key::Numpad3, "Numpad3"},
      {Key::Numpad4, "Numpad4"},
      {Key::Numpad5, "Numpad5"},
      {Key::Numpad6, "Numpad6"},
      {Key::Numpad7, "Numpad7"},
      {Key::Numpad8, "Numpad8"},
      {Key::Numpad9, "Numpad9"},
      // Modifiers
      {Key::ShiftLeft, "ShiftLeft"},
      {Key::ShiftRight, "ShiftRight"},
      {Key::CtrlLeft, "CtrlLeft"},
      {Key::CtrlRight, "CtrlRight"},
      {Key::AltLeft, "AltLeft"},
      {Key::AltRight, "AltRight"},
      {Key::SuperLeft, "SuperLeft"},
      {Key::SuperRight, "SuperRight"},
      {Key::CapsLock, "CapsLock"},
      {Key::NumLock, "NumLock"},
      // Misc
      {Key::Help, "Help"},
      {Key::Menu, "Menu"},
      {Key::Power, "Power"},
      {Key::Sleep, "Sleep"},
      {Key::Wake, "Wake"},
      {Key::Mute, "Mute"},
      {Key::VolumeDown, "VolumeDown"},
      {Key::VolumeUp, "VolumeUp"},
      {Key::MediaPlayPause, "MediaPlayPause"},
      {Key::MediaStop, "MediaStop"},
      {Key::MediaNext, "MediaNext"},
      {Key::MediaPrevious, "MediaPrevious"},
      {Key::BrightnessDown, "BrightnessDown"},
      {Key::BrightnessUp, "BrightnessUp"},
      {Key::Eject, "Eject"},
      // Punctuation / layout-dependent
      {Key::Grave, "`"},
      {Key::Minus, "-"},
      {Key::Equal, "="},
      {Key::LeftBracket, "["},
      {Key::RightBracket, "]"},
      {Key::Backslash, "\\"},
      {Key::Semicolon, ";"},
      {Key::Apostrophe, "'"},
      {Key::Comma, ","},
      {Key::Period, "."},
      {Key::Slash, "/"},
      // Shifted / symbol characters (canonical textual names)
      {Key::At, "At"},
      {Key::Hashtag, "Hashtag"},
      {Key::Exclamation, "Exclamation"},
      {Key::Dollar, "Dollar"},
      {Key::Percent, "Percent"},
      {Key::Caret, "Caret"},
      {Key::Ampersand, "Ampersand"},
      {Key::Asterisk, "Asterisk"},
      {Key::LeftParen, "LeftParen"},
      {Key::RightParen, "RightParen"},
      {Key::Underscore, "Underscore"},
      {Key::Plus, "Plus"},
      {Key::Colon, "Colon"},
      {Key::Quote, "Quote"},
      {Key::QuestionMark, "QuestionMark"},
      {Key::Bar, "Bar"},
      {Key::LessThan, "LessThan"},
      {Key::GreaterThan, "GreaterThan"},
      // ASCII control canonical names (C0 / DEL)
      {Key::AsciiNUL, "NUL"},
      {Key::AsciiSOH, "SOH"},
      {Key::AsciiSTX, "STX"},
      {Key::AsciiETX, "ETX"},
      {Key::AsciiEOT, "EOT"},
      {Key::AsciiENQ, "ENQ"},
      {Key::AsciiACK, "ACK"},
      {Key::AsciiBell, "Bell"},
      {Key::AsciiVT, "VT"},
      {Key::AsciiFF, "FF"},
      {Key::AsciiSO, "SO"},
      {Key::AsciiSI, "SI"},
      {Key::AsciiDLE, "DLE"},
      {Key::AsciiDC1, "DC1"},
      {Key::AsciiDC2, "DC2"},
      {Key::AsciiDC3, "DC3"},
      {Key::AsciiDC4, "DC4"},
      {Key::AsciiNAK, "NAK"},
      {Key::AsciiSYN, "SYN"},
      {Key::AsciiETB, "ETB"},
      {Key::AsciiCAN, "CAN"},
      {Key::AsciiEM, "EM"},
      {Key::AsciiSUB, "SUB"},
      {Key::AsciiFS, "FS"},
      {Key::AsciiGS, "GS"},
      {Key::AsciiRS, "RS"},
      {Key::AsciiUS, "US"},
      {Key::AsciiDEL, "DEL"},
  };
  return pairs;
}

} // namespace

TYPR_IO_API std::string keyToString(Key key) {
  for (const auto &pair : keyStringPairs()) {
    if (pair.first == key) {
      return pair.second;
    }
  }
  return {"Unknown"};
}

TYPR_IO_API Key stringToKey(const std::string &input) {
  static std::unordered_map<std::string, Key> rev;
  if (input.empty()) {
    return Key::Unknown;
  }
  if (rev.empty()) {
    for (const auto &pair : keyStringPairs()) {
      // Seed canonical mapping (lowercased)
      rev.emplace(toLower(pair.second), pair.first);
    }
    TYPR_IO_LOG_DEBUG("Seeding reverse map with %zu canonical entries",
                      keyStringPairs().size());

    // Helpful aliases / synonyms
    rev.emplace("esc", Key::Escape);
    rev.emplace("return", Key::Enter);
    rev.emplace("spacebar", Key::Space);
    rev.emplace("space", Key::Space);
    rev.emplace("ctrl", Key::CtrlLeft);
    rev.emplace("control", Key::CtrlLeft);
    rev.emplace("shift", Key::ShiftLeft);
    rev.emplace("alt", Key::AltLeft);
    rev.emplace("super", Key::SuperLeft);
    rev.emplace("meta", Key::SuperLeft);
    rev.emplace("win", Key::SuperLeft);

    // Top-row numeric aliases like \"num0\" -> Key::Num0
    rev.emplace("num0", Key::Num0);
    rev.emplace("num1", Key::Num1);
    rev.emplace("num2", Key::Num2);
    rev.emplace("num3", Key::Num3);
    rev.emplace("num4", Key::Num4);
    rev.emplace("num5", Key::Num5);
    rev.emplace("num6", Key::Num6);
    rev.emplace("num7", Key::Num7);
    rev.emplace("num8", Key::Num8);
    rev.emplace("num9", Key::Num9);

    // Some punctuation aliases
    rev.emplace("dash", Key::Minus);
    rev.emplace("hyphen", Key::Minus);
    rev.emplace("minus", Key::Minus);
    rev.emplace("grave", Key::Grave);
    rev.emplace("backslash", Key::Backslash);
    rev.emplace("semicolon", Key::Semicolon);
    rev.emplace("apostrophe", Key::Apostrophe);
    rev.emplace("comma", Key::Comma);
    rev.emplace("period", Key::Period);
    rev.emplace("dot", Key::Period);
    rev.emplace("slash", Key::Slash);
    rev.emplace("bracketleft", Key::LeftBracket);
    rev.emplace("bracketright", Key::RightBracket);

    // Single-character aliases for punctuation / shifted characters.
    rev.emplace(" ", Key::Space);
    rev.emplace("\t", Key::Tab);

    // ASCII control single-character mappings: map raw control bytes to
    // a logical `Key` when observed as an input character.
    rev.emplace("\x00", Key::AsciiNUL);
    rev.emplace("\x01", Key::AsciiSOH);
    rev.emplace("\x02", Key::AsciiSTX);
    rev.emplace("\x03", Key::AsciiETX);
    rev.emplace("\x04", Key::AsciiEOT);
    rev.emplace("\x05", Key::AsciiENQ);
    rev.emplace("\x06", Key::AsciiACK);
    rev.emplace("\x07", Key::AsciiBell);
    rev.emplace("\x08", Key::Backspace);
    rev.emplace("\x09", Key::Tab);
    rev.emplace("\x0A", Key::Enter);
    rev.emplace("\x0B", Key::AsciiVT);
    rev.emplace("\x0C", Key::AsciiFF);
    rev.emplace("\x0D", Key::Enter);
    rev.emplace("\x0E", Key::AsciiSO);
    rev.emplace("\x0F", Key::AsciiSI);
    rev.emplace("\x10", Key::AsciiDLE);
    rev.emplace("\x11", Key::AsciiDC1);
    rev.emplace("\x12", Key::AsciiDC2);
    rev.emplace("\x13", Key::AsciiDC3);
    rev.emplace("\x14", Key::AsciiDC4);
    rev.emplace("\x15", Key::AsciiNAK);
    rev.emplace("\x16", Key::AsciiSYN);
    rev.emplace("\x17", Key::AsciiETB);
    rev.emplace("\x18", Key::AsciiCAN);
    rev.emplace("\x19", Key::AsciiEM);
    rev.emplace("\x1A", Key::AsciiSUB);
    rev.emplace("\x1B", Key::Escape);
    rev.emplace("\x1C", Key::AsciiFS);
    rev.emplace("\x1D", Key::AsciiGS);
    rev.emplace("\x1E", Key::AsciiRS);
    rev.emplace("\x1F", Key::AsciiUS);
    rev.emplace("\x7F", Key::Delete);

    // Map single-character symbols to the physical/logical keys users
    // commonly expect on US-style layouts (e.g., '@' is Shift+Num2).
    rev.emplace("@", Key::Num2);
    rev.emplace("#", Key::Num3);
    rev.emplace("!", Key::Num1);
    rev.emplace("$", Key::Num4);
    rev.emplace("%", Key::Num5);
    rev.emplace("^", Key::Num6);
    rev.emplace("&", Key::Num7);
    rev.emplace("*", Key::Num8);
    rev.emplace("(", Key::Num9);
    rev.emplace(")", Key::Num0);

    // Other single-character punctuation aliases that map to existing
    // layout-dependent keys.
    rev.emplace("_", Key::Minus);
    rev.emplace("+", Key::Equal);
    rev.emplace(":", Key::Semicolon);
    rev.emplace("\"", Key::Apostrophe);
    rev.emplace("?", Key::Slash);
    rev.emplace("|", Key::Backslash);
    rev.emplace("<", Key::Comma);
    rev.emplace(">", Key::Period);
    rev.emplace("{", Key::LeftBracket);
    rev.emplace("}", Key::RightBracket);
    rev.emplace("~", Key::Grave);

    // Helpful textual aliases for common symbols (lowercased by seeding logic)
    rev.emplace("at", Key::At);
    rev.emplace("hash", Key::Hashtag);
    rev.emplace("hashtag", Key::Hashtag);
    rev.emplace("pound", Key::Hashtag);
    rev.emplace("bang", Key::Exclamation);
    rev.emplace("exclamation", Key::Exclamation);
    rev.emplace("dollar", Key::Dollar);
    rev.emplace("percent", Key::Percent);
    rev.emplace("caret", Key::Caret);
    rev.emplace("ampersand", Key::Ampersand);
    rev.emplace("star", Key::Asterisk);
    rev.emplace("asterisk", Key::Asterisk);
    rev.emplace("lparen", Key::LeftParen);
    rev.emplace("rparen", Key::RightParen);
    rev.emplace("underscore", Key::Underscore);
    rev.emplace("plus", Key::Plus);
    rev.emplace("colon", Key::Colon);
    rev.emplace("quote", Key::Quote);
    rev.emplace("pipe", Key::Bar);
    rev.emplace("bar", Key::Bar);
    rev.emplace("lt", Key::LessThan);
    rev.emplace("gt", Key::GreaterThan);
    rev.emplace("less", Key::LessThan);
    rev.emplace("greater", Key::GreaterThan);
    // ASCII textual aliases
    rev.emplace("nul", Key::AsciiNUL);
    rev.emplace("bell", Key::AsciiBell);
    rev.emplace("vt", Key::AsciiVT);
    rev.emplace("ff", Key::AsciiFF);
    rev.emplace("dle", Key::AsciiDLE);
    rev.emplace("sub", Key::AsciiSUB);
    rev.emplace("can", Key::AsciiCAN);
    rev.emplace("fs", Key::AsciiFS);
    rev.emplace("gs", Key::AsciiGS);
    rev.emplace("rs", Key::AsciiRS);
    rev.emplace("us", Key::AsciiUS);
    rev.emplace("del", Key::AsciiDEL);

    // Numeric keypad aliases (numpadX is already present via canonical mapping,
    // but also allow \"kpX\" prefixes that some users might use).
    rev.emplace("kp0", Key::Numpad0);
    rev.emplace("kp1", Key::Numpad1);
    rev.emplace("kp2", Key::Numpad2);
    rev.emplace("kp3", Key::Numpad3);
    rev.emplace("kp4", Key::Numpad4);
    rev.emplace("kp5", Key::Numpad5);
    rev.emplace("kp6", Key::Numpad6);
    rev.emplace("kp7", Key::Numpad7);
    rev.emplace("kp8", Key::Numpad8);
    rev.emplace("kp9", Key::Numpad9);
  }

  std::string key = toLower(input);
  auto it = rev.find(key);
  if (it != rev.end()) {
    return it->second;
  }
  TYPR_IO_LOG_DEBUG("stringToKey: unknown input='%s'",
                    escapeForLog(input).c_str());
  return Key::Unknown;
}

} // namespace io
} // namespace typr
