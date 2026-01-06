#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include <axidev-io/c_api.h>

static void noop_listener_cb(uint32_t codepoint,
                             axidev_io_keyboard_key_with_modifier_t key_mod,
                             bool pressed, void *user_data) {
  (void)codepoint;
  (void)key_mod;
  (void)pressed;
  (void)user_data;
}

TEST(CApiTest, KeyStringConversion) {
  axidev_io_clear_last_error();

  const char *ver = axidev_io_library_version();
  ASSERT_NE(ver, nullptr);
  EXPECT_GT(std::strlen(ver), 0u);

  axidev_io_keyboard_key_t k = axidev_io_keyboard_string_to_key("A");
  ASSERT_NE(k, 0u);

  char *s = axidev_io_keyboard_key_to_string(k);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(std::string(s), "A");
  axidev_io_free_string(s);

  /* Unknown key */
  axidev_io_keyboard_key_t unk = axidev_io_keyboard_string_to_key("no-such-key");
  EXPECT_EQ(unk, 0u);
  char *unk_s = axidev_io_keyboard_key_to_string(unk);
  ASSERT_NE(unk_s, nullptr);
  EXPECT_EQ(std::string(unk_s), "Unknown");
  axidev_io_free_string(unk_s);
}

TEST(CApiTest, KeyStringWithModifierConversion) {
  axidev_io_clear_last_error();

  /* Test keyToStringWithModifier */
  axidev_io_keyboard_key_with_modifier_t parsed;
  parsed.key = axidev_io_keyboard_string_to_key("A");
  parsed.mods = AXIDEV_IO_MOD_SHIFT;

  char *s = axidev_io_keyboard_key_to_string_with_modifier(parsed);
  ASSERT_NE(s, nullptr);
  EXPECT_NE(std::string(s).find("Shift"), std::string::npos);
  EXPECT_NE(std::string(s).find("A"), std::string::npos);
  axidev_io_free_string(s);

  /* Test with multiple modifiers */
  parsed.key = axidev_io_keyboard_string_to_key("C");
  parsed.mods = AXIDEV_IO_MOD_CTRL | AXIDEV_IO_MOD_SHIFT;
  s = axidev_io_keyboard_key_to_string_with_modifier(parsed);
  ASSERT_NE(s, nullptr);
  EXPECT_NE(std::string(s).find("Ctrl"), std::string::npos);
  EXPECT_NE(std::string(s).find("Shift"), std::string::npos);
  EXPECT_NE(std::string(s).find("C"), std::string::npos);
  axidev_io_free_string(s);

  /* Test stringToKeyWithModifier */
  axidev_io_keyboard_key_with_modifier_t kwm;
  EXPECT_TRUE(axidev_io_keyboard_string_to_key_with_modifier("Shift+A", &kwm));
  EXPECT_NE(kwm.key, 0u);
  EXPECT_TRUE((kwm.mods & AXIDEV_IO_MOD_SHIFT) != 0);

  /* Verify the parsed key is correct */
  char *key_name = axidev_io_keyboard_key_to_string(kwm.key);
  ASSERT_NE(key_name, nullptr);
  EXPECT_EQ(std::string(key_name), "A");
  axidev_io_free_string(key_name);

  /* Test with multiple modifier prefixes */
  EXPECT_TRUE(
      axidev_io_keyboard_string_to_key_with_modifier("Ctrl+Shift+S", &kwm));
  EXPECT_NE(kwm.key, 0u);
  EXPECT_TRUE((kwm.mods & AXIDEV_IO_MOD_CTRL) != 0);
  EXPECT_TRUE((kwm.mods & AXIDEV_IO_MOD_SHIFT) != 0);

  /* Test NULL handling */
  EXPECT_FALSE(axidev_io_keyboard_string_to_key_with_modifier(NULL, &kwm));
  EXPECT_FALSE(axidev_io_keyboard_string_to_key_with_modifier("Shift+A", NULL));
}

TEST(CApiTest, SenderCreationAndErrorHandling) {
  axidev_io_clear_last_error();

  axidev_io_keyboard_sender_t sender = axidev_io_keyboard_sender_create();
  ASSERT_NE(sender, nullptr);

  axidev_io_keyboard_capabilities_t caps;
  axidev_io_keyboard_sender_get_capabilities(sender, &caps);
  /* Capabilities are platform dependent; this call should succeed without
     asserting particular values. */

  /* Passing NULL sender should fail and set last error. */
  axidev_io_clear_last_error();
  axidev_io_keyboard_key_with_modifier_t test_key_mod = {1, 0};
  bool ok = axidev_io_keyboard_sender_key_down(NULL, test_key_mod);
  EXPECT_FALSE(ok);
  char *err = axidev_io_get_last_error();
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("sender"), std::string::npos);
  axidev_io_free_string(err);
  axidev_io_clear_last_error();

  /* Passing NULL text should fail and set last error. */
  ok = axidev_io_keyboard_sender_type_text_utf8(sender, NULL);
  EXPECT_FALSE(ok);
  err = axidev_io_get_last_error();
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("utf8_text"), std::string::npos);
  axidev_io_free_string(err);
  axidev_io_clear_last_error();

  /* Misc calls should be safe / no-ops in tests */
  axidev_io_keyboard_sender_set_key_delay(sender, 1000);
  axidev_io_keyboard_sender_flush(sender);

  /* Freeing NULL should be safe */
  axidev_io_free_string(NULL);

  axidev_io_keyboard_sender_destroy(sender);
}

TEST(CApiTest, ListenerCreateStartStop) {
  axidev_io_clear_last_error();

  axidev_io_keyboard_listener_t listener = axidev_io_keyboard_listener_create();
  ASSERT_NE(listener, nullptr);

  /* Starting with a NULL callback should fail and set an error about callback.
   */
  bool ok = axidev_io_keyboard_listener_start(listener, NULL, NULL);
  EXPECT_FALSE(ok);
  char *err = axidev_io_get_last_error();
  ASSERT_NE(err, nullptr);
  EXPECT_NE(std::string(err).find("callback"), std::string::npos);
  axidev_io_free_string(err);
  axidev_io_clear_last_error();

  /* Starting with a valid callback may succeed or fail depending on platform
     permissions. The call must be safe. If it succeeds, stop the listener. */
  ok = axidev_io_keyboard_listener_start(listener, noop_listener_cb, NULL);
  if (ok) {
    EXPECT_TRUE(axidev_io_keyboard_listener_is_listening(listener));
    axidev_io_keyboard_listener_stop(listener);
    EXPECT_FALSE(axidev_io_keyboard_listener_is_listening(listener));
  } else {
    /* If it failed, retrieve and clear the error (platform dependent). */
    char *e = axidev_io_get_last_error();
    if (e) {
      axidev_io_free_string(e);
      axidev_io_clear_last_error();
    }
  }

  axidev_io_keyboard_listener_destroy(listener);
}
