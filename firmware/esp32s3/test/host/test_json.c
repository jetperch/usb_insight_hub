/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih_test.h"
#include "uih/json.h"

struct capture_s {
    int count;
    char keys[8][32];
    uih_json_type_e types[8];
    char raw[8][64];
};

static int on_member(void * user, const uih_json_token_s * key, const uih_json_token_s * value) {
    struct capture_s * c = (struct capture_s *) user;
    if (c->count >= 8) {
        return 1;
    }
    if (uih_json_string_decode(key, c->keys[c->count], sizeof(c->keys[0])) < 0) {
        return 1;
    }
    c->types[c->count] = value->type;
    snprintf(c->raw[c->count], sizeof(c->raw[0]), "%.*s", (int) value->len, value->ptr);
    c->count++;
    return 0;
}

static int parse(const char * s, struct capture_s * c) {
    memset(c, 0, sizeof(*c));
    return uih_json_object_parse(s, strlen(s), on_member, c);
}

static void test_flat_object(void) {
    struct capture_s c;
    ASSERT_EQ(0, parse(" { \"a\" : 1 , \"b\":\"x\",\"c\":true,\"d\":false,\"e\":null } ", &c));
    ASSERT_EQ(5, c.count);
    ASSERT_STR_EQ("a", c.keys[0]);
    ASSERT_EQ(UIH_JSON_NUMBER, c.types[0]);
    ASSERT_STR_EQ("1", c.raw[0]);
    ASSERT_EQ(UIH_JSON_STRING, c.types[1]);
    ASSERT_EQ(UIH_JSON_TRUE, c.types[2]);
    ASSERT_EQ(UIH_JSON_FALSE, c.types[3]);
    ASSERT_EQ(UIH_JSON_NULL, c.types[4]);
}

static void test_nested_values_are_skipped_as_tokens(void) {
    struct capture_s c;
    ASSERT_EQ(0, parse("{\"x\":[1,{\"a\":\"]\"},[]],\"o\":{\"k\":{}},\"z\":2}", &c));
    ASSERT_EQ(3, c.count);
    ASSERT_EQ(UIH_JSON_ARRAY, c.types[0]);
    ASSERT_STR_EQ("[1,{\"a\":\"]\"},[]]", c.raw[0]);
    ASSERT_EQ(UIH_JSON_OBJECT, c.types[1]);
    ASSERT_STR_EQ("z", c.keys[2]);
}

static void test_empty_object(void) {
    struct capture_s c;
    ASSERT_EQ(0, parse("{}", &c));
    ASSERT_EQ(0, c.count);
}

static void test_invalid_documents(void) {
    struct capture_s c;
    const char * bad[] = {
        "", "[]", "1", "{", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "{\"a\":1} x",
        "{a:1}", "{\"a\":tru}", "{\"a\":01}", "{\"a\":-}", "{\"a\":1.}", "{\"a\":1e}",
        "{\"a\":\"unterminated}", "{\"a\":\"bad\\q\"}", "{\"a\":\"\\u12\"}",
        "{\"a\":[1,2}", "{\"a\":{\"b\":1]}", "{\"a\":\"ctl\x01\"}", "{\"a\":1 \"b\":2}",
        "{\"a\"", "{\"a\": ", "{\"a\":1, ", "{\"a\":\"x\\", "{ ",
        "{\"a\":1", "{\"a\":{\"b\"", "{\"a\":[1",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        if (0 == parse(bad[i], &c)) {
            FAIL_("accepted invalid document: %s", bad[i]);
        }
    }
}

static void test_deep_nesting_rejected(void) {
    char s[256] = "{\"a\":";
    for (int i = 0; i < 40; ++i) {
        strcat(s, "[");
    }
    struct capture_s c;
    ASSERT_TRUE(0 != parse(s, &c));
}

static void test_callback_abort(void) {
    struct capture_s c;
    memset(&c, 0, sizeof(c));
    c.count = 8;  /* full: the callback returns nonzero */
    ASSERT_TRUE(0 != uih_json_object_parse("{\"a\":1}", 7, on_member, &c));
}

static void test_number_validation(void) {
    struct capture_s c;
    const char * ok[] = {"{\"a\":0}", "{\"a\":-0}", "{\"a\":12.5}", "{\"a\":1e3}",
                         "{\"a\":-1.25E-2}", "{\"a\":1E+2}"};
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); ++i) {
        if (0 != parse(ok[i], &c)) {
            FAIL_("rejected valid number: %s", ok[i]);
        }
    }
}

static void test_int_conversion(void) {
    uih_json_token_s t = {UIH_JSON_NUMBER, "4294967295", 10};
    int64_t v = 0;
    ASSERT_EQ(0, uih_json_int(&t, &v));
    ASSERT_EQ(4294967295LL, v);
    t.ptr = "-12"; t.len = 3;
    ASSERT_EQ(0, uih_json_int(&t, &v));
    ASSERT_EQ(-12, v);
    t.ptr = "1.5"; t.len = 3;
    ASSERT_TRUE(0 != uih_json_int(&t, &v));
    t.ptr = "2.0"; t.len = 3;
    ASSERT_TRUE(0 != uih_json_int(&t, &v));  /* integers only, as in Python */
    t.ptr = "1e3"; t.len = 3;
    ASSERT_TRUE(0 != uih_json_int(&t, &v));
    t.ptr = "99999999999999999999"; t.len = 20;
    ASSERT_TRUE(0 != uih_json_int(&t, &v));  /* overflow */
    t.ptr = "-"; t.len = 1;
    ASSERT_TRUE(0 != uih_json_int(&t, &v));  /* sign only */
    t.type = UIH_JSON_STRING; t.ptr = "\"1\""; t.len = 3;
    ASSERT_TRUE(0 != uih_json_int(&t, &v));
}

static void test_string_decode(void) {
    char buf[16];
    const char * text = "\"a\\\"b\\\\c\\/\\n\\u0041\"";
    uih_json_token_s t = {UIH_JSON_STRING, text, strlen(text)};
    ASSERT_EQ(8, uih_json_string_decode(&t, buf, sizeof(buf)));
    ASSERT_STR_EQ("a\"b\\c/\nA", buf);
    char small[4];
    t.ptr = "\"abcdef\""; t.len = strlen(t.ptr);
    ASSERT_TRUE(uih_json_string_decode(&t, small, sizeof(small)) < 0);  /* too long */
    t.ptr = "\"\\u00e9\""; t.len = strlen(t.ptr);
    ASSERT_TRUE(uih_json_string_decode(&t, buf, sizeof(buf)) < 0);  /* non-ASCII escape */
    t.ptr = "\"\\b\\f\\r\\t\\u004A\\u004a\""; t.len = strlen(t.ptr);
    ASSERT_EQ(6, uih_json_string_decode(&t, buf, sizeof(buf)));
    ASSERT_STR_EQ("\b\f\r\tJJ", buf);  /* hex digits in either case */
    t.type = UIH_JSON_NUMBER;
    ASSERT_TRUE(uih_json_string_decode(&t, buf, sizeof(buf)) < 0);
}

static void test_string_equals(void) {
    const char * text = "\"pi\\u006eg\"";
    uih_json_token_s t = {UIH_JSON_STRING, text, strlen(text)};
    ASSERT_TRUE(uih_json_string_equals(&t, "ping"));
    ASSERT_FALSE(uih_json_string_equals(&t, "pin"));
    ASSERT_FALSE(uih_json_string_equals(&t, "pings"));
}

int main(void) {
    RUN(test_flat_object);
    RUN(test_nested_values_are_skipped_as_tokens);
    RUN(test_empty_object);
    RUN(test_invalid_documents);
    RUN(test_deep_nesting_rejected);
    RUN(test_callback_abort);
    RUN(test_number_validation);
    RUN(test_int_conversion);
    RUN(test_string_decode);
    RUN(test_string_equals);
    return test_main_result("json");
}
