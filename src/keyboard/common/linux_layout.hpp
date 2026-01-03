#pragma once
/**
 * @file keyboard/common/linux_layout.hpp
 * @brief Internal helpers for detecting XKB keyboard layout information on
 * Linux.
 *
 * This header is intentionally placed under `src/` (not installed) because it
 * is an internal implementation detail shared by Linux backends.
 */

#include <string>

namespace axidev::io::keyboard::detail {

/**
 * @brief Container for XKB rule names components.
 *
 * These fields correspond to `struct xkb_rule_names` fields. Callers typically
 * populate an `xkb_rule_names` instance by assigning `.c_str()` pointers from
 * these strings.
 */
struct XkbRuleNamesStrings {
  std::string rules;
  std::string model;
  std::string layout;
  std::string variant;
  std::string options;

  bool empty() const {
    return rules.empty() && model.empty() && layout.empty() &&
           variant.empty() && options.empty();
  }
};

/**
 * @brief Detect XKB rule names on Linux.
 *
 * Detection strategy (best-effort):
 * 1) Read `XKB_DEFAULT_*` environment variables if set.
 * 2) Fallback to parsing `/etc/default/keyboard` (Debian/Ubuntu-style).
 * 3) If layout is still missing, guess it from locale (`LC_ALL`, `LC_MESSAGES`,
 *    `LANG`).
 *
 * The function does not invoke external commands.
 */
XkbRuleNamesStrings detectXkbRuleNames();

} // namespace axidev::io::keyboard::detail
