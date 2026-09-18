// Host test for main/ir/ir_library.c. Uses the NVS in-memory stub so the
// real persistence code can be tested without an ESP-IDF target.

#include <string.h>

#include "minitest.h"
#include "ir/ir_library.h"

#include "../main/ir/ir_library.c"

static ir_nec_frame_t frame(uint8_t address, uint8_t command)
{
    return (ir_nec_frame_t){ .address = address, .command = command };
}

static void reset_library(void)
{
    while (ir_library_count() > 0) {
        ir_library_remove(0);
    }
}

MT_TEST(add_get_and_remove_preserve_insertion_order)
{
    reset_library();
    ir_nec_frame_t first = frame(0x01, 0x11);
    ir_nec_frame_t second = frame(0x02, 0x22);
    ir_nec_frame_t third = frame(0x03, 0x33);

    MT_CHECK(ir_library_add("TV", &first));
    MT_CHECK(ir_library_add("Fan", &second));
    MT_CHECK(ir_library_add("Light", &third));
    MT_CHECK_EQ_INT(ir_library_count(), 3);
    MT_CHECK_EQ_STR(ir_library_get(1)->name, "Fan");
    MT_CHECK_EQ_INT(ir_library_get(1)->frame.command, 0x22);

    ir_library_remove(1);
    MT_CHECK_EQ_INT(ir_library_count(), 2);
    MT_CHECK_EQ_STR(ir_library_get(1)->name, "Light");
    MT_CHECK_EQ_INT(ir_library_get(1)->frame.address, 0x03);
}

MT_TEST(names_are_truncated_and_terminated)
{
    reset_library();
    char long_name[IR_LIBRARY_NAME_MAX_LEN + 8];
    memset(long_name, 'A', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    ir_nec_frame_t code = frame(0x01, 0x02);

    MT_CHECK(ir_library_add(long_name, &code));
    MT_CHECK_EQ_INT((int)strlen(ir_library_get(0)->name), IR_LIBRARY_NAME_MAX_LEN);
}

MT_TEST(full_library_rejects_new_code)
{
    reset_library();
    ir_nec_frame_t code = frame(0x01, 0x02);
    for (int i = 0; i < IR_LIBRARY_MAX_ENTRIES; i++) {
        MT_CHECK(ir_library_add("Code", &code));
    }
    MT_CHECK(!ir_library_add("Extra", &code));
    MT_CHECK_EQ_INT(ir_library_count(), IR_LIBRARY_MAX_ENTRIES);
}

MT_TEST(save_then_load_roundtrips_entries)
{
    nvs_stub_reset();
    reset_library();
    ir_nec_frame_t tv = frame(0x0a, 0x4f);
    ir_nec_frame_t fan = frame(0x0b, 0x50);
    MT_CHECK(ir_library_add("TV", &tv));
    MT_CHECK(ir_library_add("Fan", &fan));
    MT_CHECK(ir_library_save());

    reset_library();
    ir_library_load();
    MT_CHECK_EQ_INT(ir_library_count(), 2);
    MT_CHECK_EQ_STR(ir_library_get(0)->name, "TV");
    MT_CHECK_EQ_INT(ir_library_get(0)->frame.command, 0x4f);
    MT_CHECK_EQ_STR(ir_library_get(1)->name, "Fan");
    MT_CHECK_EQ_INT(ir_library_get(1)->frame.address, 0x0b);
}

MT_TEST(invalid_indices_are_noops)
{
    reset_library();
    ir_nec_frame_t code = frame(0x01, 0x02);
    MT_CHECK(ir_library_add("Code", &code));
    ir_library_remove(-1);
    ir_library_remove(1);
    MT_CHECK_EQ_INT(ir_library_count(), 1);
    MT_CHECK(ir_library_get(-1) == NULL);
    MT_CHECK(ir_library_get(1) == NULL);
}

int main(void)
{
    MT_RUN(add_get_and_remove_preserve_insertion_order);
    MT_RUN(names_are_truncated_and_terminated);
    MT_RUN(full_library_rejects_new_code);
    MT_RUN(save_then_load_roundtrips_entries);
    MT_RUN(invalid_indices_are_noops);
    return MT_SUMMARY();
}
