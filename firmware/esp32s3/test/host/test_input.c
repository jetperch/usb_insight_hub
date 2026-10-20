/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih_test.h"
#include "uih/input.h"

static void test_press_after_stable_samples(void) {
    uih_buttons_s b;
    uih_buttons_init(&b);
    ASSERT_EQ(0, uih_buttons_update(&b, 0x01));
    ASSERT_EQ(0, uih_buttons_update(&b, 0x01));
    ASSERT_EQ(0x01, uih_buttons_update(&b, 0x01));  // third stable sample
    ASSERT_EQ(0, uih_buttons_update(&b, 0x01));     // held: no repeat
}

static void test_bounce_is_ignored(void) {
    uih_buttons_s b;
    uih_buttons_init(&b);
    uint8_t presses = 0;
    const uint8_t samples[] = {1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
    for (size_t i = 0; i < sizeof(samples); ++i) {
        presses = (uint8_t) (presses + (uih_buttons_update(&b, samples[i] ? 0x02 : 0) ? 1 : 0));
    }
    ASSERT_EQ(1, presses);
}

static void test_release_rearms(void) {
    uih_buttons_s b;
    uih_buttons_init(&b);
    for (int i = 0; i < 3; ++i) uih_buttons_update(&b, 0x04);
    for (int i = 0; i < 3; ++i) uih_buttons_update(&b, 0);
    uint8_t m = 0;
    for (int i = 0; i < 3; ++i) m |= uih_buttons_update(&b, 0x04);
    ASSERT_EQ(0x04, m);
}

static void test_independent_buttons(void) {
    uih_buttons_s b;
    uih_buttons_init(&b);
    uih_buttons_update(&b, 0x03);
    uih_buttons_update(&b, 0x03);
    ASSERT_EQ(0x03, uih_buttons_update(&b, 0x07));  // button 3 just started
    uih_buttons_update(&b, 0x07);
    ASSERT_EQ(0x04, uih_buttons_update(&b, 0x07));
}

static char got[8][300];
static size_t got_len[8];
static int got_count;
static int overflows;

static void on_line(void * ctx, const char * line, size_t len) {
    (void) ctx;
    memcpy(got[got_count], line, len);
    got[got_count][len] = 0;
    got_len[got_count++] = len;
}

static void on_overflow(void * ctx) {
    (void) ctx;
    overflows++;
}

static void feed(uih_line_s * l, const char * s) {
    uih_line_feed(l, (const uint8_t *) s, strlen(s), on_line, on_overflow, NULL);
}

static void reset_capture(void) {
    got_count = 0;
    overflows = 0;
}

static void test_lines_split_across_reads(void) {
    uih_line_s l;
    uih_line_init(&l);
    reset_capture();
    feed(&l, "ab");
    feed(&l, "c\nde\r\nf");
    ASSERT_EQ(2, got_count);
    ASSERT_STR_EQ("abc", got[0]);
    ASSERT_STR_EQ("de\r", got[1]);  // the frame decoder strips the CR
    feed(&l, "\n");
    ASSERT_STR_EQ("f", got[2]);
}

static void test_overflow_discards_until_newline(void) {
    uih_line_s l;
    uih_line_init(&l);
    reset_capture();
    char big[UIH_PROTO_LINE_MAX + 10];
    memset(big, 'x', sizeof(big));
    uih_line_feed(&l, (const uint8_t *) big, sizeof(big), on_line, on_overflow, NULL);
    ASSERT_EQ(0, overflows);  // reported at the end of the line
    feed(&l, "yyy\nok\n");
    ASSERT_EQ(1, overflows);
    ASSERT_EQ(1, got_count);
    ASSERT_STR_EQ("ok", got[0]);
}

static void test_max_length_line_fits(void) {
    uih_line_s l;
    uih_line_init(&l);
    reset_capture();
    char line[UIH_PROTO_LINE_MAX + 2];
    memset(line, 'z', UIH_PROTO_LINE_MAX);
    line[UIH_PROTO_LINE_MAX] = '\r';  // a CR does not count toward the limit
    line[UIH_PROTO_LINE_MAX + 1] = '\n';
    uih_line_feed(&l, (const uint8_t *) line, sizeof(line), on_line, on_overflow, NULL);
    ASSERT_EQ(0, overflows);
    ASSERT_EQ(1, got_count);
    ASSERT_EQ(UIH_PROTO_LINE_MAX + 1, got_len[0]);
}

int main(void) {
    RUN(test_press_after_stable_samples);
    RUN(test_bounce_is_ignored);
    RUN(test_release_rearms);
    RUN(test_independent_buttons);
    RUN(test_lines_split_across_reads);
    RUN(test_overflow_discards_until_newline);
    RUN(test_max_length_line_fits);
    return test_main_result("input");
}
