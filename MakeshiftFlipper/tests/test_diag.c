// Host test for main/diag/diag.c's ring buffer. Compiles the real diag.c
// against tests/stubs/esp_timer.h instead of ESP-IDF's, and against
// diag.h's real DIAG_HISTORY_CAPACITY -- no logic is duplicated here.

#include <stdint.h>

#include "minitest.h"

int64_t g_fake_time_us = 0;

#include "../main/diag/diag.h"

// Pulls in the real implementation; -I flags at compile time point its own
// "esp_timer.h" include at tests/stubs/ instead of a real ESP-IDF tree.
#include "../main/diag/diag.c"

MT_TEST(empty_history_reports_zero)
{
    diag_clear();
    MT_CHECK_EQ_INT(diag_get_history_count(), 0);

    diag_entry_t out[DIAG_HISTORY_CAPACITY];
    MT_CHECK_EQ_INT(diag_get_history(out, DIAG_HISTORY_CAPACITY), 0);
}

MT_TEST(single_entry_roundtrips)
{
    diag_clear();
    g_fake_time_us = 5000000; // 5s since boot
    diag_record_error("WiFi Setup", "CONNECT_FAILED");

    MT_CHECK_EQ_INT(diag_get_history_count(), 1);

    diag_entry_t out[1];
    MT_CHECK_EQ_INT(diag_get_history(out, 1), 1);
    MT_CHECK_EQ_STR(out[0].module, "WiFi Setup");
    MT_CHECK_EQ_STR(out[0].code, "CONNECT_FAILED");
    MT_CHECK_EQ_INT(out[0].timestamp_us, 5000000);
}

MT_TEST(history_is_newest_first)
{
    diag_clear();
    g_fake_time_us = 1;
    diag_record_error("A", "first");
    g_fake_time_us = 2;
    diag_record_error("B", "second");
    g_fake_time_us = 3;
    diag_record_error("C", "third");

    diag_entry_t out[3];
    MT_CHECK_EQ_INT(diag_get_history(out, 3), 3);
    MT_CHECK_EQ_STR(out[0].code, "third");
    MT_CHECK_EQ_STR(out[1].code, "second");
    MT_CHECK_EQ_STR(out[2].code, "first");
}

MT_TEST(get_history_respects_max_entries_cap)
{
    diag_clear();
    for (int i = 0; i < 5; i++) {
        diag_record_error("mod", "code");
    }
    diag_entry_t out[2];
    // Asking for fewer than what's recorded should only copy that many,
    // not overflow the caller's buffer.
    MT_CHECK_EQ_INT(diag_get_history(out, 2), 2);
}

MT_TEST(ring_buffer_overwrites_oldest_when_full)
{
    diag_clear();
    // Fill past capacity: record DIAG_HISTORY_CAPACITY + 3 entries, with
    // codes "0", "1", "2", ... so we can check which ones survived.
    char code[8];
    for (int i = 0; i < DIAG_HISTORY_CAPACITY + 3; i++) {
        snprintf(code, sizeof(code), "%d", i);
        diag_record_error("mod", code);
    }

    // Count caps at capacity, doesn't grow past it.
    MT_CHECK_EQ_INT(diag_get_history_count(), DIAG_HISTORY_CAPACITY);

    diag_entry_t out[DIAG_HISTORY_CAPACITY];
    MT_CHECK_EQ_INT(diag_get_history(out, DIAG_HISTORY_CAPACITY), DIAG_HISTORY_CAPACITY);

    // Newest entry should be the last one recorded.
    snprintf(code, sizeof(code), "%d", DIAG_HISTORY_CAPACITY + 2);
    MT_CHECK_EQ_STR(out[0].code, code);

    // Oldest surviving entry (index CAPACITY-1) should be entry #3 -- the
    // first 3 (codes "0","1","2") were overwritten.
    snprintf(code, sizeof(code), "%d", 3);
    MT_CHECK_EQ_STR(out[DIAG_HISTORY_CAPACITY - 1].code, code);
}

MT_TEST(module_and_code_are_truncated_and_null_terminated)
{
    diag_clear();
    char long_module[DIAG_MODULE_MAX_LEN + 20];
    memset(long_module, 'M', sizeof(long_module) - 1);
    long_module[sizeof(long_module) - 1] = '\0';

    char long_code[DIAG_CODE_MAX_LEN + 20];
    memset(long_code, 'C', sizeof(long_code) - 1);
    long_code[sizeof(long_code) - 1] = '\0';

    diag_record_error(long_module, long_code);

    diag_entry_t out[1];
    diag_get_history(out, 1);
    MT_CHECK_EQ_INT((int)strlen(out[0].module), DIAG_MODULE_MAX_LEN);
    MT_CHECK_EQ_INT((int)strlen(out[0].code), DIAG_CODE_MAX_LEN);
}

MT_TEST(clear_resets_count_and_cursor)
{
    diag_clear();
    diag_record_error("mod", "code");
    diag_record_error("mod", "code");
    MT_CHECK_EQ_INT(diag_get_history_count(), 2);

    diag_clear();
    MT_CHECK_EQ_INT(diag_get_history_count(), 0);

    // After clearing, a fresh record should behave like a brand new
    // buffer (newest-first ordering starts over cleanly).
    g_fake_time_us = 99;
    diag_record_error("fresh", "entry");
    diag_entry_t out[1];
    diag_get_history(out, 1);
    MT_CHECK_EQ_STR(out[0].code, "entry");
}

MT_TEST(save_then_load_roundtrips_history)
{
    nvs_stub_reset();
    diag_clear();
    g_fake_time_us = 42;
    diag_record_error("RFID Clone", "SAVE_ME");
    MT_CHECK(diag_save());

    // Simulate a reboot: wipe the RAM history, then load from the "flash"
    // (stub store), which survives across this.
    diag_clear();
    MT_CHECK_EQ_INT(diag_get_history_count(), 0);

    diag_load();
    MT_CHECK_EQ_INT(diag_get_history_count(), 1);
    diag_entry_t out[1];
    diag_get_history(out, 1);
    MT_CHECK_EQ_STR(out[0].module, "RFID Clone");
    MT_CHECK_EQ_STR(out[0].code, "SAVE_ME");
    MT_CHECK_EQ_INT(out[0].timestamp_us, 42);
}

MT_TEST(load_with_nothing_saved_is_a_true_no_op)
{
    nvs_stub_reset(); // simulates a freshly-erased device, nothing saved yet
    diag_clear();
    diag_record_error("still", "here_after_load");

    // diag_load() must NOT clear the RAM history just because NVS is
    // empty -- it should leave whatever's already there untouched (see
    // diag.h's comment on why: "nothing to load" isn't the same as
    // "clear what's there").
    diag_load();
    MT_CHECK_EQ_INT(diag_get_history_count(), 1);
    diag_entry_t out[1];
    diag_get_history(out, 1);
    MT_CHECK_EQ_STR(out[0].code, "here_after_load");
}

MT_TEST(save_then_load_preserves_ring_buffer_order_after_wraparound)
{
    nvs_stub_reset();
    diag_clear();
    char code[8];
    // Wrap the ring buffer past capacity before saving, so the save/load
    // path is exercised on a non-trivial s_next_slot, not just slot 0.
    for (int i = 0; i < DIAG_HISTORY_CAPACITY + 5; i++) {
        snprintf(code, sizeof(code), "%d", i);
        diag_record_error("mod", code);
    }
    MT_CHECK(diag_save());

    diag_clear();
    diag_load();

    MT_CHECK_EQ_INT(diag_get_history_count(), DIAG_HISTORY_CAPACITY);
    diag_entry_t out[DIAG_HISTORY_CAPACITY];
    diag_get_history(out, DIAG_HISTORY_CAPACITY);
    snprintf(code, sizeof(code), "%d", DIAG_HISTORY_CAPACITY + 4);
    MT_CHECK_EQ_STR(out[0].code, code); // newest survives at index 0
}

int main(void)
{
    printf("test_diag:\n");
    MT_RUN(empty_history_reports_zero);
    MT_RUN(single_entry_roundtrips);
    MT_RUN(history_is_newest_first);
    MT_RUN(get_history_respects_max_entries_cap);
    MT_RUN(ring_buffer_overwrites_oldest_when_full);
    MT_RUN(module_and_code_are_truncated_and_null_terminated);
    MT_RUN(clear_resets_count_and_cursor);
    MT_RUN(save_then_load_roundtrips_history);
    MT_RUN(load_with_nothing_saved_is_a_true_no_op);
    MT_RUN(save_then_load_preserves_ring_buffer_order_after_wraparound);
    return MT_SUMMARY();
}
