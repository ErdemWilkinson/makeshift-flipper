// Host test for main/rfid/rfid_library.c. Compiles the real source against
// tests/stubs/esp_log.h and tests/stubs/nvs.h (the same in-memory NVS
// stand-in test_diag.c and test_ir_library.c use).

#include "minitest.h"
#include "../main/rfid/rfid_library.c"

static void reset_library(void)
{
    nvs_stub_reset();
    s_count = 0;
    memset(s_entries, 0, sizeof(s_entries));
}

MT_TEST(empty_library_reports_zero)
{
    reset_library();
    MT_CHECK_EQ_INT(rfid_library_count(), 0);
    MT_CHECK(rfid_library_get(0) == NULL);
}

MT_TEST(add_125khz_then_get_roundtrips_entry)
{
    reset_library();
    rdm6300_id_t id = { .bytes = { 0x01, 0x02, 0x03, 0x04, 0x05 } };
    MT_CHECK(rfid_library_add_125khz("Garage Fob", &id));

    MT_CHECK_EQ_INT(rfid_library_count(), 1);
    const rfid_library_entry_t *e = rfid_library_get(0);
    MT_CHECK(e != NULL);
    MT_CHECK_EQ_STR(e->name, "Garage Fob");
    MT_CHECK(e->kind == RFID_LIBRARY_KIND_125KHZ);
    MT_CHECK_EQ_INT(memcmp(&e->u.id_125khz, &id, sizeof(id)), 0);
}

MT_TEST(add_1356mhz_then_get_roundtrips_entry)
{
    reset_library();
    rc522_uid_t uid = { .bytes = { 0xDE, 0xAD, 0xBE, 0xEF }, .length = 4 };
    MT_CHECK(rfid_library_add_1356mhz("Office Badge", &uid));

    MT_CHECK_EQ_INT(rfid_library_count(), 1);
    const rfid_library_entry_t *e = rfid_library_get(0);
    MT_CHECK(e != NULL);
    MT_CHECK_EQ_STR(e->name, "Office Badge");
    MT_CHECK(e->kind == RFID_LIBRARY_KIND_1356MHZ);
    MT_CHECK_EQ_INT(e->u.uid_1356mhz.length, 4);
    MT_CHECK_EQ_INT(memcmp(e->u.uid_1356mhz.bytes, uid.bytes, 4), 0);
}

MT_TEST(entries_of_both_kinds_are_kept_in_insertion_order)
{
    reset_library();
    rdm6300_id_t id = { .bytes = { 1, 2, 3, 4, 5 } };
    rc522_uid_t uid = { .bytes = { 9, 8, 7, 6 }, .length = 4 };
    rfid_library_add_125khz("first", &id);
    rfid_library_add_1356mhz("second", &uid);

    MT_CHECK_EQ_STR(rfid_library_get(0)->name, "first");
    MT_CHECK_EQ_STR(rfid_library_get(1)->name, "second");
}

MT_TEST(add_fails_once_at_capacity)
{
    reset_library();
    rdm6300_id_t id = { .bytes = { 0, 0, 0, 0, 0 } };
    char name[8];
    for (int i = 0; i < RFID_LIBRARY_MAX_ENTRIES; i++) {
        snprintf(name, sizeof(name), "e%d", i);
        MT_CHECK(rfid_library_add_125khz(name, &id));
    }
    MT_CHECK_EQ_INT(rfid_library_count(), RFID_LIBRARY_MAX_ENTRIES);

    // One more (of either kind) must fail cleanly, not overflow s_entries
    // or silently drop the oldest entry.
    MT_CHECK(!rfid_library_add_125khz("overflow", &id));
    rc522_uid_t uid = { .bytes = { 0, 0, 0, 0 }, .length = 4 };
    MT_CHECK(!rfid_library_add_1356mhz("overflow2", &uid));
    MT_CHECK_EQ_INT(rfid_library_count(), RFID_LIBRARY_MAX_ENTRIES);
    MT_CHECK_EQ_STR(rfid_library_get(0)->name, "e0"); // oldest entry untouched
}

MT_TEST(name_is_truncated_and_null_terminated)
{
    reset_library();
    char long_name[RFID_LIBRARY_NAME_MAX_LEN + 20];
    memset(long_name, 'N', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    rdm6300_id_t id = { .bytes = { 0, 0, 0, 0, 0 } };
    rfid_library_add_125khz(long_name, &id);

    MT_CHECK_EQ_INT((int)strlen(rfid_library_get(0)->name), RFID_LIBRARY_NAME_MAX_LEN);
}

MT_TEST(remove_shifts_later_entries_down)
{
    reset_library();
    rdm6300_id_t id = { .bytes = { 0, 0, 0, 0, 0 } };
    rfid_library_add_125khz("a", &id);
    rfid_library_add_125khz("b", &id);
    rfid_library_add_125khz("c", &id);

    rfid_library_remove(0); // remove "a"

    MT_CHECK_EQ_INT(rfid_library_count(), 2);
    MT_CHECK_EQ_STR(rfid_library_get(0)->name, "b");
    MT_CHECK_EQ_STR(rfid_library_get(1)->name, "c");
}

MT_TEST(remove_out_of_range_is_a_no_op)
{
    reset_library();
    rdm6300_id_t id = { .bytes = { 0, 0, 0, 0, 0 } };
    rfid_library_add_125khz("only", &id);

    rfid_library_remove(-1);
    rfid_library_remove(5);
    MT_CHECK_EQ_INT(rfid_library_count(), 1);
    MT_CHECK_EQ_STR(rfid_library_get(0)->name, "only");
}

MT_TEST(get_out_of_range_returns_null)
{
    reset_library();
    rdm6300_id_t id = { .bytes = { 0, 0, 0, 0, 0 } };
    rfid_library_add_125khz("only", &id);

    MT_CHECK(rfid_library_get(-1) == NULL);
    MT_CHECK(rfid_library_get(1) == NULL);
}

MT_TEST(matches_125khz_finds_saved_uid_and_ignores_other_kind)
{
    reset_library();
    rdm6300_id_t saved = { .bytes = { 1, 2, 3, 4, 5 } };
    rdm6300_id_t other = { .bytes = { 9, 9, 9, 9, 9 } };
    rfid_library_add_125khz("known", &saved);

    MT_CHECK(rfid_library_matches_125khz(&saved));
    MT_CHECK(!rfid_library_matches_125khz(&other));

    // A 13.56MHz entry with coincidentally-overlapping bytes must never
    // match a 125kHz lookup -- matches_*() must check kind, not just bytes.
    reset_library();
    rc522_uid_t uid = { .bytes = { 1, 2, 3, 4 }, .length = 4 };
    rfid_library_add_1356mhz("badge", &uid);
    MT_CHECK(!rfid_library_matches_125khz(&saved));
}

MT_TEST(matches_1356mhz_checks_length_and_bytes)
{
    reset_library();
    rc522_uid_t saved = { .bytes = { 0xAA, 0xBB, 0xCC, 0xDD }, .length = 4 };
    rfid_library_add_1356mhz("badge", &saved);

    rc522_uid_t same = { .bytes = { 0xAA, 0xBB, 0xCC, 0xDD }, .length = 4 };
    MT_CHECK(rfid_library_matches_1356mhz(&same));

    // Same leading bytes but different length must not match.
    rc522_uid_t longer = { .bytes = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 }, .length = 7 };
    MT_CHECK(!rfid_library_matches_1356mhz(&longer));

    rc522_uid_t different = { .bytes = { 0x11, 0x22, 0x33, 0x44 }, .length = 4 };
    MT_CHECK(!rfid_library_matches_1356mhz(&different));
}

MT_TEST(save_then_load_roundtrips_across_simulated_reboot)
{
    reset_library();
    rdm6300_id_t id = { .bytes = { 0x10, 0x20, 0x30, 0x40, 0x50 } };
    rc522_uid_t uid = { .bytes = { 0x60, 0x70, 0x80, 0x90 }, .length = 4 };
    rfid_library_add_125khz("Garage Fob", &id);
    rfid_library_add_1356mhz("Office Badge", &uid);
    MT_CHECK(rfid_library_save());

    // Simulate a reboot: clear RAM (but not the NVS stub store), then load.
    s_count = 0;
    memset(s_entries, 0, sizeof(s_entries));
    MT_CHECK_EQ_INT(rfid_library_count(), 0);

    rfid_library_load();
    MT_CHECK_EQ_INT(rfid_library_count(), 2);
    MT_CHECK_EQ_STR(rfid_library_get(0)->name, "Garage Fob");
    MT_CHECK(rfid_library_get(0)->kind == RFID_LIBRARY_KIND_125KHZ);
    MT_CHECK_EQ_STR(rfid_library_get(1)->name, "Office Badge");
    MT_CHECK(rfid_library_get(1)->kind == RFID_LIBRARY_KIND_1356MHZ);
    MT_CHECK_EQ_INT(rfid_library_get(1)->u.uid_1356mhz.length, 4);
}

MT_TEST(load_with_nothing_saved_is_a_true_no_op)
{
    nvs_stub_reset(); // fresh device, nothing saved -- don't also clear RAM
    s_count = 0;
    memset(s_entries, 0, sizeof(s_entries));
    rdm6300_id_t id = { .bytes = { 0, 0, 0, 0, 0 } };
    rfid_library_add_125khz("still here", &id);

    rfid_library_load();
    MT_CHECK_EQ_INT(rfid_library_count(), 1);
    MT_CHECK_EQ_STR(rfid_library_get(0)->name, "still here");
}

int main(void)
{
    printf("test_rfid_library:\n");
    MT_RUN(empty_library_reports_zero);
    MT_RUN(add_125khz_then_get_roundtrips_entry);
    MT_RUN(add_1356mhz_then_get_roundtrips_entry);
    MT_RUN(entries_of_both_kinds_are_kept_in_insertion_order);
    MT_RUN(add_fails_once_at_capacity);
    MT_RUN(name_is_truncated_and_null_terminated);
    MT_RUN(remove_shifts_later_entries_down);
    MT_RUN(remove_out_of_range_is_a_no_op);
    MT_RUN(get_out_of_range_returns_null);
    MT_RUN(matches_125khz_finds_saved_uid_and_ignores_other_kind);
    MT_RUN(matches_1356mhz_checks_length_and_bytes);
    MT_RUN(save_then_load_roundtrips_across_simulated_reboot);
    MT_RUN(load_with_nothing_saved_is_a_true_no_op);
    return MT_SUMMARY();
}
