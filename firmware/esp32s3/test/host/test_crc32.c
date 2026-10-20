/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih_test.h"
#include "uih/crc32.h"

static void test_known_values(void) {
    ASSERT_EQ(0x00000000u, uih_crc32((const uint8_t *) "", 0));
    ASSERT_EQ(0xCBF43926u, uih_crc32((const uint8_t *) "123456789", 9));  /* check value */
    ASSERT_EQ(0x414FA339u, uih_crc32((const uint8_t *) "The quick brown fox jumps over the lazy dog", 43));
}

static void test_ping_frame(void) {
    /* From test/vectors/frames.txt: {"op":"ping","seq":1}*1c8550d4 */
    const char * s = "{\"op\":\"ping\",\"seq\":1}";
    ASSERT_EQ(0x1c8550d4u, uih_crc32((const uint8_t *) s, strlen(s)));
}

int main(void) {
    RUN(test_known_values);
    RUN(test_ping_frame);
    return test_main_result("crc32");
}
