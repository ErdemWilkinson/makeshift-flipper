// Host test for main/ir/ir_library.c. Compiles the real source against
// tests/stubs/esp_log.h and tests/stubs/nvs.h (the same in-memory NVS
// stand-in test_diag.c uses).

#include "minitest.h"
#include "../main/ir/ir_library.c"

static void reset_library(void)
{
    nvs_stub_reset();
    s_count = 0;
    memset(s_entries, 0, sizeof(s_entries));
}

MT_TEST(empty_library_reports_zero)
{
    reset_library();
    MT_CHECK_EQ_INT(ir_library_count(), 0);
    MT_CHECK(ir_library_get(0) == NULL);
}

MT_TEST(add_then_get_roundtrips_entry)
{
    reset_library();
    ir_nec_frame_t frame = { .address = 0x01, .command = 0x45 };
    MT_CHECK(ir_library_add("TV Power", &frame));

    MT_CHECK_EQ_INT(ir_library_count(), 1);
    const ir_library_entry_t *e = ir_library_get(0);
    MT_CHECK(e != NULL);
    MT_CHECK_EQ_STR(e->name, "TV Power");
    MT_CHECK_EQ_INT(e->frame.address, 0x01);
    MT_CHECK_EQ_INT(e->frame.command, 0x45);
}

MT_TEST(entries_are_kept_in_insertion_order)
{
    reset_library();
    ir_nec_frame_t f1 = { .address = 1, .command = 1 };
    ir_nec_frame_t f2 = { .address = 2, .command = 2 };
    ir_library_add("first", &f1);
    ir_library_add("second", &f2);

    MT_CHECK_EQ_STR(ir_library_get(0)->name, "first");
    MT_CHECK_EQ_STR(ir_library_get(1)->name, "second");
}

MT_TEST(add_fails_once_at_capacity)
{
    reset_library();
    ir_nec_frame_t frame = { .address = 0, .command = 0 };
    char name[8];
    for (int i = 0; i < IR_LIBRARY_MAX_ENTRIES; i++) {
        snprintf(name, sizeof(name), "e%d", i);
        MT_CHECK(ir_library_add(name, &frame));
    }
    MT_CHECK_EQ_INT(ir_library_count(), IR_LIBRARY_MAX_ENTRIES);

    // One more must fail cleanly, not overflow s_entries or silently drop
    // the oldest entry (unlike diag.c's ring buffer -- a saved remote code
    // shouldn't vanish without the user choosing to delete it).
    MT_CHECK(!ir_library_add("overflow", &frame));
    MT_CHECK_EQ_INT(ir_library_count(), IR_LIBRARY_MAX_ENTRIES);
    MT_CHECK_EQ_STR(ir_library_get(0)->name, "e0"); // oldest entry untouched
}

MT_TEST(name_is_truncated_and_null_terminated)
{
    reset_library();
    char long_name[IR_LIBRARY_NAME_MAX_LEN + 20];
    memset(long_name, 'N', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    ir_nec_frame_t frame = { .address = 0, .command = 0 };
    ir_library_add(long_name, &frame);

    MT_CHECK_EQ_INT((int)strlen(ir_library_get(0)->name), IR_LIBRARY_NAME_MAX_LEN);
}

MT_TEST(remove_shifts_later_entries_down)
{
    reset_library();
    ir_nec_frame_t frame = { .address = 0, .command = 0 };
    ir_library_add("a", &frame);
    ir_library_add("b", &frame);
    ir_library_add("c", &frame);

    ir_library_remove(0); // remove "a"

    MT_CHECK_EQ_INT(ir_library_count(), 2);
    MT_CHECK_EQ_STR(ir_library_get(0)->name, "b");
    MT_CHECK_EQ_STR(ir_library_get(1)->name, "c");
}

MT_TEST(remove_out_of_range_is_a_no_op)
{
    reset_library();
    ir_nec_frame_t frame = { .address = 0, .command = 0 };
    ir_library_add("only", &frame);

    ir_library_remove(-1);
    ir_library_remove(5);
    MT_CHECK_EQ_INT(ir_library_count(), 1);
    MT_CHECK_EQ_STR(ir_library_get(0)->name, "only");
}

MT_TEST(get_out_of_range_returns_null)
{
    reset_library();
    ir_nec_frame_t frame = { .address = 0, .command = 0 };
    ir_library_add("only", &frame);

    MT_CHECK(ir_library_get(-1) == NULL);
    MT_CHECK(ir_library_get(1) == NULL);
}

MT_TEST(save_then_load_roundtrips_across_simulated_reboot)
{
    reset_library();
    ir_nec_frame_t f1 = { .address = 0x10, .command = 0x20 };
    ir_nec_frame_t f2 = { .address = 0x30, .command = 0x40 };
    ir_library_add("Living Room TV", &f1);
    ir_library_add("Bedroom AC", &f2);
    MT_CHECK(ir_library_save());

    // Simulate a reboot: clear RAM (but not the NVS stub store), then load.
    s_count = 0;
    memset(s_entries, 0, sizeof(s_entries));
    MT_CHECK_EQ_INT(ir_library_count(), 0);

    ir_library_load();
    MT_CHECK_EQ_INT(ir_library_count(), 2);
    MT_CHECK_EQ_STR(ir_library_get(0)->name, "Living Room TV");
    MT_CHECK_EQ_INT(ir_library_get(0)->frame.address, 0x10);
    MT_CHECK_EQ_STR(ir_library_get(1)->name, "Bedroom AC");
    MT_CHECK_EQ_INT(ir_library_get(1)->frame.command, 0x40);
}

MT_TEST(load_with_nothing_saved_is_a_true_no_op)
{
    nvs_stub_reset(); // fresh device, nothing saved -- don't also clear RAM
    s_count = 0;
    memset(s_entries, 0, sizeof(s_entries));
    ir_nec_frame_t frame = { .address = 0, .command = 0 };
    ir_library_add("still here", &frame);

    ir_library_load();
    MT_CHECK_EQ_INT(ir_library_count(), 1);
    MT_CHECK_EQ_STR(ir_library_get(0)->name, "still here");
}

int main(void)
{
    printf("test_ir_library:\n");
    MT_RUN(empty_library_reports_zero);
    MT_RUN(add_then_get_roundtrips_entry);
    MT_RUN(entries_are_kept_in_insertion_order);
    MT_RUN(add_fails_once_at_capacity);
    MT_RUN(name_is_truncated_and_null_terminated);
    MT_RUN(remove_shifts_later_entries_down);
    MT_RUN(remove_out_of_range_is_a_no_op);
    MT_RUN(get_out_of_range_returns_null);
    MT_RUN(save_then_load_roundtrips_across_simulated_reboot);
    MT_RUN(load_with_nothing_saved_is_a_true_no_op);
    return MT_SUMMARY();
}
