/**
 * @file keyboard/common/linux_layout.cpp
 * @brief Internal helpers for detecting XKB keyboard layout information on
 * Linux.
 */

#include "keyboard/common/linux_layout.hpp"

#include <algorithm>
#include <cctype>

#if defined(__linux__)
#include <cstdlib>
#include <fstream>
#endif

namespace axidev::io::keyboard::detail {

namespace {

void trimInPlace(std::string &s) {
  const char *ws = " \t\r\n";
  size_t a = s.find_first_not_of(ws);
  if (a == std::string::npos) {
    s.clear();
    return;
  }
  size_t b = s.find_last_not_of(ws);
  s = s.substr(a, b - a + 1);
}

void stripSurroundingQuotes(std::string &s) {
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                        (s.front() == '\'' && s.back() == '\''))) {
    s = s.substr(1, s.size() - 2);
  }
}

// Normalize key names from /etc/default/keyboard for comparison.
std::string normalizeKey(std::string key) {
  trimInPlace(key);
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return key;
}

void setIfEmpty(std::string &dst, const std::string &value) {
  if (dst.empty())
    dst = value;
}

} // namespace

XkbRuleNamesStrings detectXkbRuleNames() {
  XkbRuleNamesStrings out;

#if defined(__linux__)
  // 1) Environment variables (common across compositors/session managers)
  if (const char *env = std::getenv("XKB_DEFAULT_RULES"))
    out.rules = env;
  if (const char *env = std::getenv("XKB_DEFAULT_MODEL"))
    out.model = env;
  if (const char *env = std::getenv("XKB_DEFAULT_LAYOUT"))
    out.layout = env;
  if (const char *env = std::getenv("XKB_DEFAULT_VARIANT"))
    out.variant = env;
  if (const char *env = std::getenv("XKB_DEFAULT_OPTIONS"))
    out.options = env;

  trimInPlace(out.rules);
  trimInPlace(out.model);
  trimInPlace(out.layout);
  trimInPlace(out.variant);
  trimInPlace(out.options);

  // 2) /etc/default/keyboard (Debian/Ubuntu-style).
  // Only fill missing fields from the file.
  if (out.rules.empty() || out.model.empty() || out.layout.empty() ||
      out.variant.empty() || out.options.empty()) {
    std::ifstream f("/etc/default/keyboard");
    if (f) {
      std::string line;
      while (std::getline(f, line)) {
        size_t comment = line.find('#');
        if (comment != std::string::npos)
          line.resize(comment);
        trimInPlace(line);
        if (line.empty())
          continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos)
          continue;

        std::string key = normalizeKey(line.substr(0, eq));
        std::string val = line.substr(eq + 1);
        trimInPlace(val);
        stripSurroundingQuotes(val);
        trimInPlace(val);

        if (val.empty())
          continue;

        if (key == "XKBRULES" || key == "XKB_DEFAULT_RULES")
          setIfEmpty(out.rules, val);
        else if (key == "XKBMODEL" || key == "XKB_DEFAULT_MODEL")
          setIfEmpty(out.model, val);
        else if (key == "XKBLAYOUT" || key == "XKB_DEFAULT_LAYOUT")
          setIfEmpty(out.layout, val);
        else if (key == "XKBVARIANT" || key == "XKB_DEFAULT_VARIANT")
          setIfEmpty(out.variant, val);
        else if (key == "XKBOPTIONS" || key == "XKB_DEFAULT_OPTIONS")
          setIfEmpty(out.options, val);
      }
    }
  }

  // 3) Locale-based heuristic for layout when missing.
  if (out.layout.empty()) {
    const char *localeEnv = std::getenv("LC_ALL");
    if (!localeEnv)
      localeEnv = std::getenv("LC_MESSAGES");
    if (!localeEnv)
      localeEnv = std::getenv("LANG");

    if (localeEnv) {
      std::string locale(localeEnv);
      // Trim off encoding/variants (e.g. en_US.UTF-8 -> en_US)
      if (size_t dot = locale.find('.'); dot != std::string::npos)
        locale.resize(dot);
      if (size_t at = locale.find('@'); at != std::string::npos)
        locale.resize(at);

      std::string lang = locale;
      std::string region;
      if (size_t us = locale.find('_'); us != std::string::npos) {
        lang = locale.substr(0, us);
        region = locale.substr(us + 1);
      }

      std::transform(
          lang.begin(), lang.end(), lang.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      std::transform(
          region.begin(), region.end(), region.begin(),
          [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

      if (lang == "en") {
        if (region == "GB" || region == "UK")
          out.layout = "gb";
        else
          out.layout = "us";
      } else if (lang == "pt" && region == "BR") {
        out.layout = "br";
      } else if (lang == "da") {
        out.layout = "dk";
      } else if (lang == "sv") {
        out.layout = "se";
      } else if (!lang.empty()) {
        out.layout = lang;
      }
    }
  }
#endif

  return out;
}

} // namespace axidev::io::keyboard::detail
