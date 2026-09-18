// Host test for main/net/json_escape.c -- no ESP-IDF dependency at all,
// compiles the real source directly.

#include "minitest.h"
#include "../main/net/json_escape.c"

MT_TEST(passes_through_plain_text)
{
    char out[32];
    size_t len = 0;
    json_escape_append(out, sizeof(out), &len, "hello");
    MT_CHECK_EQ_STR(out, "hello");
    MT_CHECK_EQ_INT(len, 5);
}

MT_TEST(escapes_quote_and_backslash)
{
    char out[32];
    size_t len = 0;
    json_escape_append(out, sizeof(out), &len, "a\"b\\c");
    MT_CHECK_EQ_STR(out, "a\\\"b\\\\c");
}

MT_TEST(empty_input_produces_empty_output)
{
    char out[32];
    size_t len = 0;
    json_escape_append(out, sizeof(out), &len, "");
    MT_CHECK_EQ_STR(out, "");
    MT_CHECK_EQ_INT(len, 0);
}

MT_TEST(appends_onto_existing_content)
{
    char out[32] = "prefix:";
    size_t len = 7;
    json_escape_append(out, sizeof(out), &len, "more");
    MT_CHECK_EQ_STR(out, "prefix:more");
}

MT_TEST(truncates_at_buffer_capacity_without_overflow)
{
    char out[6]; // room for "abcd" + escape margin
    size_t len = 0;
    json_escape_append(out, sizeof(out), &len, "abcdefgh");
    // Must stay null-terminated and never write past out_cap.
    MT_CHECK(strlen(out) < sizeof(out));
    MT_CHECK(len < sizeof(out));
}

MT_TEST(truncation_never_splits_an_escape_pair)
{
    // If the buffer is nearly full and the next char needs escaping, the
    // function must not emit a lone trailing backslash with the escaped
    // character cut off.
    char out[4];
    size_t len = 0;
    json_escape_append(out, sizeof(out), &len, "a\"\"\"");
    // Whatever was written, it must not end with an unmatched backslash.
    size_t n = strlen(out);
    if (n > 0) {
        MT_CHECK(!(out[n - 1] == '\\'));
    }
}

int main(void)
{
    printf("test_json_escape:\n");
    MT_RUN(passes_through_plain_text);
    MT_RUN(escapes_quote_and_backslash);
    MT_RUN(empty_input_produces_empty_output);
    MT_RUN(appends_onto_existing_content);
    MT_RUN(truncates_at_buffer_capacity_without_overflow);
    MT_RUN(truncation_never_splits_an_escape_pair);
    return MT_SUMMARY();
}
