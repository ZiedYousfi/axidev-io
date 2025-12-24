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
      // Additional canonical names for X11 / XF86 / international keys added
      // to the Key enum so they can roundtrip via `keyToString` and seed the
      // reverse lookup in `stringToKey`.
      {Key::NumpadEqual, "NumpadEqual"},
      {Key::Degree, "Degree"},
      {Key::Sterling, "Sterling"},
      {Key::Mu, "Mu"},
      {Key::PlusMinus, "PlusMinus"},
      {Key::DeadCircumflex, "DeadCircumflex"},
      {Key::DeadDiaeresis, "DeadDiaeresis"},
      {Key::Section, "Section"},
      {Key::Cancel, "Cancel"},
      {Key::Redo, "Redo"},
      {Key::Undo, "Undo"},
      {Key::Find, "Find"},
      {Key::Hangul, "Hangul"},
      {Key::HangulHanja, "HangulHanja"},
      {Key::Katakana, "Katakana"},
      {Key::Hiragana, "Hiragana"},
      {Key::Henkan, "Henkan"},
      {Key::Muhenkan, "Muhenkan"},
      {Key::OE, "OE"},
      {Key::oe, "oe"},
      {Key::SunProps, "SunProps"},
      {Key::SunFront, "SunFront"},
      {Key::Copy, "Copy"},
      {Key::Open, "Open"},
      {Key::Paste, "Paste"},
      {Key::Cut, "Cut"},
      {Key::Calculator, "Calculator"},
      {Key::Explorer, "Explorer"},
      {Key::Phone, "Phone"},
      {Key::WebCam, "WebCam"},
      {Key::AudioRecord, "AudioRecord"},
      {Key::AudioRewind, "AudioRewind"},
      {Key::AudioPreset, "AudioPreset"},
      {Key::Messenger, "Messenger"},
      {Key::Search, "Search"},
      {Key::Go, "Go"},
      {Key::Finance, "Finance"},
      {Key::Game, "Game"},
      {Key::Shop, "Shop"},
      {Key::HomePage, "HomePage"},
      {Key::Reload, "Reload"},
      {Key::Close, "Close"},
      {Key::Send, "Send"},
      {Key::Xfer, "Xfer"},
      {Key::LaunchA, "LaunchA"},
      {Key::LaunchB, "LaunchB"},
      {Key::Launch1, "Launch1"},
      {Key::Launch2, "Launch2"},
      {Key::Launch3, "Launch3"},
      {Key::Launch4, "Launch4"},
      {Key::Launch5, "Launch5"},
      {Key::Launch6, "Launch6"},
      {Key::Launch7, "Launch7"},
      {Key::Launch8, "Launch8"},
      {Key::Launch9, "Launch9"},
      {Key::TouchpadToggle, "TouchpadToggle"},
      {Key::TouchpadOn, "TouchpadOn"},
      {Key::TouchpadOff, "TouchpadOff"},
      {Key::KbdLightOnOff, "KbdLightOnOff"},
      {Key::KbdBrightnessDown, "KbdBrightnessDown"},
      {Key::KbdBrightnessUp, "KbdBrightnessUp"},
      {Key::Mail, "Mail"},
      {Key::MailForward, "MailForward"},
      {Key::Save, "Save"},
      {Key::Documents, "Documents"},
      {Key::Battery, "Battery"},
      {Key::Bluetooth, "Bluetooth"},
      {Key::WLAN, "WLAN"},
      {Key::UWB, "UWB"},
      {Key::Next_VMode, "Next_VMode"},
      {Key::Prev_VMode, "Prev_VMode"},
      {Key::MonBrightnessCycle, "MonBrightnessCycle"},
      {Key::BrightnessAuto, "BrightnessAuto"},
      {Key::DisplayOff, "DisplayOff"},
      {Key::WWAN, "WWAN"},
      {Key::RFKill, "RFKill"},
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
    // Defer canonical seeding until after alias synonyms are added so that
    // case-only aliases (e.g., "OE" vs "oe") aren't overwritten by a
    // lowercase canonical entry. Canonical entries are inserted later using
    // `emplace` to avoid clobbering helpful aliases.

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

    // Single-character aliases for common symbol characters observed in inputs.
    rev.emplace("@", Key::At);
    rev.emplace("&", Key::Ampersand);
    rev.emplace("(", Key::LeftParen);
    rev.emplace(")", Key::RightParen);
    rev.emplace("!", Key::Exclamation);
    rev.emplace("$", Key::Dollar);
    rev.emplace("^", Key::Caret);
    rev.emplace("*", Key::Asterisk);

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
    // but also allow \"kpX\" and other X11 KP_* names that some users / systems
    // emit).
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

    // Common X11 / keysym aliases observed on Linux systems (lowercased).
    // Map keyboard modifier / special names to existing logical keys.
    rev.emplace("control_l", Key::CtrlLeft);
    rev.emplace("control_r", Key::CtrlRight);
    rev.emplace("shift_l", Key::ShiftLeft);
    rev.emplace("shift_r", Key::ShiftRight);
    rev.emplace("alt_l", Key::AltLeft);
    rev.emplace("alt_r", Key::AltRight);
    rev.emplace("meta_l", Key::SuperLeft);
    rev.emplace("super_l", Key::SuperLeft);
    rev.emplace("super_r", Key::SuperRight);
    rev.emplace("hyper_l", Key::SuperLeft);
    rev.emplace("caps_lock", Key::CapsLock);
    rev.emplace("num_lock", Key::NumLock);
    rev.emplace("scroll_lock", Key::ScrollLock);

    // ISO / dead-key and punctuation aliases
    rev.emplace("iso_left_tab", Key::Tab);
    rev.emplace("iso_level3_shift", Key::AltRight);
    rev.emplace("iso_level5_shift", Key::AltRight);
    rev.emplace("quotedbl", Key::Quote);
    rev.emplace("parenleft", Key::LeftParen);
    rev.emplace("parenright", Key::RightParen);
    rev.emplace("equal", Key::Equal);
    rev.emplace("question", Key::QuestionMark);
    rev.emplace("exclam", Key::Exclamation);
    rev.emplace("section", Key::Section);
    rev.emplace("degree", Key::Degree);
    rev.emplace("sterling", Key::Sterling);
    rev.emplace("plusminus", Key::PlusMinus);
    rev.emplace("dead_circumflex", Key::DeadCircumflex);
    rev.emplace("dead_diaeresis", Key::DeadDiaeresis);

    // Accented / ligature aliases -> map to reasonable logical letter keys
    rev.emplace("eacute", Key::E);
    rev.emplace("egrave", Key::E);
    rev.emplace("agrave", Key::A);
    rev.emplace("ugrave", Key::U);
    rev.emplace("ccedilla", Key::C);
    rev.emplace("oe", Key::oe);
    rev.emplace("OE", Key::OE);
    rev.emplace("mu", Key::Mu);

    // Misc control / text aliases
    rev.emplace("linefeed", Key::Enter);
    rev.emplace("prior", Key::PageUp);
    rev.emplace("next", Key::PageDown);
    rev.emplace("print", Key::PrintScreen);
    rev.emplace("sys_req", Key::PrintScreen);
    rev.emplace("break", Key::Pause);
    rev.emplace("cancel", Key::Cancel);
    rev.emplace("redo", Key::Redo);
    rev.emplace("undo", Key::Undo);
    rev.emplace("find", Key::Find);
    rev.emplace("sunprops", Key::SunProps);
    rev.emplace("sunfront", Key::SunFront);

    // Common UX / XF86 app / hardware alias textual fallbacks
    rev.emplace("menu", Key::Menu);
    rev.emplace("copy", Key::Copy);
    rev.emplace("open", Key::Open);
    rev.emplace("paste", Key::Paste);
    rev.emplace("cut", Key::Cut);
    rev.emplace("calculator", Key::Calculator);
    rev.emplace("explorer", Key::Explorer);
    rev.emplace("phone", Key::Phone);
    rev.emplace("webcam", Key::WebCam);
    rev.emplace("mail", Key::Mail);
    rev.emplace("mailforward", Key::MailForward);
    rev.emplace("save", Key::Save);
    rev.emplace("documents", Key::Documents);

    // Now seed canonical mappings (lowercased). Use `emplace` so we don't
    // overwrite existing helpful alias entries (case-only collisions).
    size_t seeded = 0;
    for (const auto &pair : keyStringPairs()) {
      std::string lower = toLower(pair.second);
      auto [it, inserted] = rev.emplace(lower, pair.first);
      if (inserted) {
        ++seeded;
      } else {
        TYPR_IO_LOG_DEBUG("stringToKey: canonical lowercase '%s' collides with "
                          "existing mapping - keeping existing",
                          lower.c_str());
      }
    }
    TYPR_IO_LOG_DEBUG(
        "Seeding reverse map with %zu canonical entries (%zu inserted)",
        keyStringPairs().size(), seeded);

    // Numeric keypad textual aliases are already seeded - leave room for
    // dynamic parsing for `KP_*` forms in the lookup logic below.
  }

  // Exact-case lookup first: prefer an exact alias match (e.g., "OE")
  // over falling back to a case-insensitive canonical match.
  auto itExact = rev.find(input);
  if (itExact != rev.end()) {
    return itExact->second;
  }

  std::string key = toLower(input);
  auto it = rev.find(key);
  if (it != rev.end()) {
    return it->second;
  }

  // Handle X11 numeric keypad (KP_*) names that may appear in input. These are
  // often emitted as 'KP_7', 'KP_Home', 'KP_Decimal', etc. Provide a
  // best-effort mapping to our `Numpad*` values.
  if (key.rfind("kp", 0) == 0) {
    std::string suffix = key.substr(2);
    if (!suffix.empty() && suffix[0] == '_') {
      suffix.erase(0, 1);
    }
    if (suffix == "multiply" || suffix == "mul")
      return Key::NumpadMultiply;
    if (suffix == "divide" || suffix == "div")
      return Key::NumpadDivide;
    if (suffix == "add" || suffix == "plus")
      return Key::NumpadPlus;
    if (suffix == "subtract" || suffix == "minus")
      return Key::NumpadMinus;
    if (suffix == "enter")
      return Key::NumpadEnter;
    if (suffix == "decimal" || suffix == "delete" || suffix == "del")
      return Key::NumpadDecimal;
    if (suffix == "equal")
      return Key::NumpadEqual;
    if (suffix == "home" || suffix == "7")
      return Key::Numpad7;
    if (suffix == "up" || suffix == "8")
      return Key::Numpad8;
    if (suffix == "prior" || suffix == "9")
      return Key::Numpad9;
    if (suffix == "left" || suffix == "4")
      return Key::Numpad4;
    if (suffix == "begin" || suffix == "5")
      return Key::Numpad5;
    if (suffix == "right" || suffix == "6")
      return Key::Numpad6;
    if (suffix == "end" || suffix == "1")
      return Key::Numpad1;
    if (suffix == "down" || suffix == "2")
      return Key::Numpad2;
    if (suffix == "next" || suffix == "3")
      return Key::Numpad3;
    if (suffix == "insert" || suffix == "0")
      return Key::Numpad0;
  }

  // ISO-level special modifiers/keys
  if (key == "iso_level3_shift" || key == "iso_level5_shift") {
    return Key::AltRight;
  }
  if (key == "iso_left_tab") {
    return Key::Tab;
  }

  // Map common XF86 hardware/media/app keys to logical keys where appropriate.
  if (key.rfind("xf86", 0) == 0) {
    if (key.find("audiomute") != std::string::npos)
      return Key::Mute;
    if (key.find("audiolowervolume") != std::string::npos)
      return Key::VolumeDown;
    if (key.find("audioraisevolume") != std::string::npos)
      return Key::VolumeUp;
    if (key.find("audionext") != std::string::npos)
      return Key::MediaNext;
    if (key.find("audioplay") != std::string::npos ||
        key.find("audiopause") != std::string::npos)
      return Key::MediaPlayPause;
    if (key.find("audioprev") != std::string::npos)
      return Key::MediaPrevious;
    if (key.find("audiostop") != std::string::npos)
      return Key::MediaStop;
    if (key.find("audiorecord") != std::string::npos)
      return Key::AudioRecord;
    if (key.find("audiorewind") != std::string::npos)
      return Key::AudioRewind;
    if (key.find("audioforward") != std::string::npos)
      return Key::MediaNext;
    if (key.find("power") != std::string::npos)
      return Key::Power;
    if (key.find("sleep") != std::string::npos)
      return Key::Sleep;
    if (key.find("wakeup") != std::string::npos)
      return Key::Wake;
    if (key.find("eject") != std::string::npos)
      return Key::Eject;
    if (key.find("monbrightnessdown") != std::string::npos)
      return Key::BrightnessDown;
    if (key.find("monbrightnessup") != std::string::npos)
      return Key::BrightnessUp;
    if (key.find("audiomedia") != std::string::npos)
      return Key::MediaPlayPause;
    if (key.find("menukb") != std::string::npos ||
        key.find("menu") != std::string::npos)
      return Key::Menu;
    if (key.find("calculator") != std::string::npos)
      return Key::Calculator;
    if (key.find("mail") != std::string::npos)
      return Key::Mail;
    if (key.find("webcam") != std::string::npos)
      return Key::WebCam;
    if (key.find("search") != std::string::npos)
      return Key::Search;
    if (key.find("launcha") != std::string::npos)
      return Key::LaunchA;
    if (key.find("launchb") != std::string::npos)
      return Key::LaunchB;
    if (key.find("launch1") != std::string::npos)
      return Key::Launch1;
    if (key.find("launch2") != std::string::npos)
      return Key::Launch2;
    if (key.find("launch3") != std::string::npos)
      return Key::Launch3;
    if (key.find("launch4") != std::string::npos)
      return Key::Launch4;
    if (key.find("launch5") != std::string::npos)
      return Key::Launch5;
    if (key.find("launch6") != std::string::npos)
      return Key::Launch6;
    if (key.find("launch7") != std::string::npos)
      return Key::Launch7;
    if (key.find("launch8") != std::string::npos)
      return Key::Launch8;
    if (key.find("launch9") != std::string::npos)
      return Key::Launch9;
    if (key.find("touchpad") != std::string::npos)
      return Key::TouchpadToggle;
    if (key.find("kbd") != std::string::npos) {
      if (key.find("brightness") != std::string::npos) {
        if (key.find("down") != std::string::npos)
          return Key::KbdBrightnessDown;
        if (key.find("up") != std::string::npos)
          return Key::KbdBrightnessUp;
      }
      return Key::KbdLightOnOff;
    }
    if (key.find("battery") != std::string::npos)
      return Key::Battery;
    if (key.find("bluetooth") != std::string::npos)
      return Key::Bluetooth;
    if (key.find("wlan") != std::string::npos)
      return Key::WLAN;
    if (key.find("wwan") != std::string::npos)
      return Key::WWAN;
    if (key.find("rfkill") != std::string::npos)
      return Key::RFKill;

    // If we don't find a match here, allow the code below to either map a more
    // generic alias or fall back to Unknown (and log) so missing names are
    // still discoverable.
  }

  // Generic / common aliases that don't need XF86/KP prefixes.
  if (key == "eacute" || key == "egrave")
    return Key::E;
  if (key == "agrave")
    return Key::A;
  if (key == "ugrave")
    return Key::U;
  if (key == "ccedilla")
    return Key::C;
  if (key == "OE")
    return Key::OE;
  if (key == "oe")
    return Key::oe;
  if (key == "quotedbl")
    return Key::Quote;
  if (key == "question")
    return Key::QuestionMark;
  if (key == "exclam")
    return Key::Exclamation;
  if (key == "degree")
    return Key::Degree;
  if (key == "sterling")
    return Key::Sterling;
  if (key == "mu")
    return Key::Mu;
  if (key == "section")
    return Key::Section;
  if (key == "plusminus")
    return Key::PlusMinus;
  if (key == "linefeed")
    return Key::Enter;
  if (key == "prior")
    return Key::PageUp;
  if (key == "next")
    return Key::PageDown;
  if (key == "print" || key == "sys_req")
    return Key::PrintScreen;
  if (key == "cancel")
    return Key::Cancel;
  if (key == "redo")
    return Key::Redo;
  if (key == "undo")
    return Key::Undo;
  if (key == "find")
    return Key::Find;
  if (key == "sunprops")
    return Key::SunProps;
  if (key == "sunfront")
    return Key::SunFront;

  TYPR_IO_LOG_DEBUG("stringToKey: unknown input='%s'",
                    escapeForLog(input).c_str());
  return Key::Unknown;
}

} // namespace io
} // namespace typr
