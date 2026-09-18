// Host test for c6-firmware/main/text_sanitize.c -- no ESP-IDF dependency,
// compiles the real source directly.

#include "minitest.h"
#include "../c6-firmware/main/text_sanitize.c"

MT_TEST(sanitize_replaces_comma_and_newlines)
{
    char text[] = "a,b\nc\rd";
    sanitize_wire_text(text);
    MT_CHECK_EQ_STR(text, "a_b_c_d");
}

MT_TEST(sanitize_leaves_clean_text_untouched)
{
    char text[] = "MyDevice-01";
    sanitize_wire_text(text);
    MT_CHECK_EQ_STR(text, "MyDevice-01");
}

MT_TEST(sanitize_handles_empty_string)
{
    char text[] = "";
    sanitize_wire_text(text); // must not crash / read past the terminator
    MT_CHECK_EQ_STR(text, "");
}

MT_TEST(extract_ble_name_finds_complete_local_name)
{
    // AD structure: [len=6][type=0x09]['H','i','T','h','e']
    uint8_t data[] = {6, 0x09, 'H', 'i', 'T', 'h', 'e'};
    char out[16];
    extract_ble_name(data, sizeof(data), out, sizeof(out));
    MT_CHECK_EQ_STR(out, "HiThe");
}

MT_TEST(extract_ble_name_finds_shortened_local_name)
{
    uint8_t data[] = {3, 0x08, 'H', 'i'};
    char out[16];
    extract_ble_name(data, sizeof(data), out, sizeof(out));
    MT_CHECK_EQ_STR(out, "Hi");
}

MT_TEST(extract_ble_name_skips_uninteresting_ad_structures)
{
    // First AD structure is flags (type 0x01), second is the name.
    uint8_t data[] = {
        2, 0x01, 0x06,             // flags AD structure
        4, 0x09, 'A', 'B', 'C',    // complete local name "ABC"
    };
    char out[16];
    extract_ble_name(data, sizeof(data), out, sizeof(out));
    MT_CHECK_EQ_STR(out, "ABC");
}

MT_TEST(extract_ble_name_empty_when_no_name_present)
{
    uint8_t data[] = {2, 0x01, 0x06}; // just flags, no name
    char out[16] = "leftover";
    extract_ble_name(data, sizeof(data), out, sizeof(out));
    MT_CHECK_EQ_STR(out, "");
}

MT_TEST(extract_ble_name_truncates_to_output_capacity)
{
    uint8_t data[] = {11, 0x09, 'L','o','n','g','N','a','m','e','X','X'};
    char out[6]; // room for 5 chars + NUL
    extract_ble_name(data, sizeof(data), out, sizeof(out));
    MT_CHECK(strlen(out) < sizeof(out));
}

MT_TEST(extract_ble_name_handles_malformed_length_without_reading_oob)
{
    // field_len claims more bytes than are actually present.
    uint8_t data[] = {200, 0x09, 'X'};
    char out[16] = "untouched";
    extract_ble_name(data, sizeof(data), out, sizeof(out));
    // Malformed/truncated frame: should bail out leaving out_name empty,
    // not read past `data`.
    MT_CHECK_EQ_STR(out, "");
}

MT_TEST(extract_ble_name_handles_zero_length_field)
{
    uint8_t data[] = {0, 0x09}; // field_len == 0 must not infinite-loop
    char out[16] = "untouched";
    extract_ble_name(data, sizeof(data), out, sizeof(out));
    MT_CHECK_EQ_STR(out, "");
}

int main(void)
{
    printf("test_text_sanitize:\n");
    MT_RUN(sanitize_replaces_comma_and_newlines);
    MT_RUN(sanitize_leaves_clean_text_untouched);
    MT_RUN(sanitize_handles_empty_string);
    MT_RUN(extract_ble_name_finds_complete_local_name);
    MT_RUN(extract_ble_name_finds_shortened_local_name);
    MT_RUN(extract_ble_name_skips_uninteresting_ad_structures);
    MT_RUN(extract_ble_name_empty_when_no_name_present);
    MT_RUN(extract_ble_name_truncates_to_output_capacity);
    MT_RUN(extract_ble_name_handles_malformed_length_without_reading_oob);
    MT_RUN(extract_ble_name_handles_zero_length_field);
    return MT_SUMMARY();
}
