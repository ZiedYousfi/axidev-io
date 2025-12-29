#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

#include <typr-io/c_api.h>

static void noop_listener_cb(uint32_t codepoint, typr_io_keyboard_key_t key,
                             typr_io_keyboard_modifier_t mods, bool pressed,
                             void *user_data) {
  (void)codepoint;
  (void)key;
  (void)mods;
  (void)pressed;
  (void)user_data;
}

TEST_CASE("typr-io C API - key/string conversion", "[c_api]") {
  typr_io_clear_last_error();

  const char *ver = typr_io_library_version();
  REQUIRE(ver != nullptr);
  REQUIRE(std::strlen(ver) > 0);

  typr_io_keyboard_key_t k = typr_io_keyboard_string_to_key("A");
  REQUIRE(k != 0);

  char *s = typr_io_keyboard_key_to_string(k);
  REQUIRE(s != nullptr);
  REQUIRE(std::string(s) == "A");
  typr_io_free_string(s);

  /* Unknown key */
  typr_io_keyboard_key_t unk = typr_io_keyboard_string_to_key("no-such-key");
  REQUIRE(unk == 0);
  char *unk_s = typr_io_keyboard_key_to_string(unk);
  REQUIRE(unk_s != nullptr);
  REQUIRE(std::string(unk_s) == "Unknown");
  typr_io_free_string(unk_s);
}

TEST_CASE("typr-io C API - sender creation and error handling", "[c_api]") {
  typr_io_clear_last_error();

  typr_io_keyboard_sender_t sender = typr_io_keyboard_sender_create();
  REQUIRE(sender != nullptr);

  typr_io_keyboard_capabilities_t caps;
  typr_io_keyboard_sender_get_capabilities(sender, &caps);
  /* Capabilities are platform dependent; this call should succeed without
     asserting particular values. */

  /* Passing NULL sender should fail and set last error. */
  typr_io_clear_last_error();
  bool ok = typr_io_keyboard_sender_key_down(NULL, (typr_io_keyboard_key_t)1);
  REQUIRE(ok == false);
  char *err = typr_io_get_last_error();
  REQUIRE(err != nullptr);
  REQUIRE(std::string(err).find("sender") != std::string::npos);
  typr_io_free_string(err);
  typr_io_clear_last_error();

  /* Passing NULL text should fail and set last error. */
  ok = typr_io_keyboard_sender_type_text_utf8(sender, NULL);
  REQUIRE(ok == false);
  err = typr_io_get_last_error();
  REQUIRE(err != nullptr);
  REQUIRE(std::string(err).find("utf8_text") != std::string::npos);
  typr_io_free_string(err);
  typr_io_clear_last_error();

  /* Misc calls should be safe / no-ops in tests */
  typr_io_keyboard_sender_set_key_delay(sender, 1000);
  typr_io_keyboard_sender_flush(sender);

  /* Freeing NULL should be safe */
  typr_io_free_string(NULL);

  typr_io_keyboard_sender_destroy(sender);
}

TEST_CASE("typr-io C API - listener create/start/stop", "[c_api]") {
  typr_io_clear_last_error();

  typr_io_keyboard_listener_t listener = typr_io_keyboard_listener_create();
  REQUIRE(listener != nullptr);

  /* Starting with a NULL callback should fail and set an error about callback.
   */
  bool ok = typr_io_keyboard_listener_start(listener, NULL, NULL);
  REQUIRE(ok == false);
  char *err = typr_io_get_last_error();
  REQUIRE(err != nullptr);
  REQUIRE(std::string(err).find("callback") != std::string::npos);
  typr_io_free_string(err);
  typr_io_clear_last_error();

  /* Starting with a valid callback may succeed or fail depending on platform
     permissions. The call must be safe. If it succeeds, stop the listener. */
  ok = typr_io_keyboard_listener_start(listener, noop_listener_cb, NULL);
  if (ok) {
    REQUIRE(typr_io_keyboard_listener_is_listening(listener) == true);
    typr_io_keyboard_listener_stop(listener);
    REQUIRE(typr_io_keyboard_listener_is_listening(listener) == false);
  } else {
    /* If it failed, retrieve and clear the error (platform dependent). */
    char *e = typr_io_get_last_error();
    if (e) {
      typr_io_free_string(e);
      typr_io_clear_last_error();
    }
  }

  typr_io_keyboard_listener_destroy(listener);
}
