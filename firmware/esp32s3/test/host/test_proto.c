/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih_test.h"
#include "uih/proto.h"
#include <stdlib.h>

#define LINE_MAX_ (1024)

typedef void (*row_fn)(const char * a, const char * b, int row);

static int for_each_row(const char * name, row_fn fn) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", UIH_VECTORS_DIR, name);
    FILE * f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s\n", path);
        return -1;
    }
    char line[LINE_MAX_];
    int rows = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') {
            line[--n] = 0;
        }
        char * tab = strchr(line, '\t');
        if (!tab) {
            continue;
        }
        *tab = 0;
        fn(line, tab + 1, rows++);
    }
    fclose(f);
    return rows;
}

static const char * code_name(uih_proto_code_e code) {
    return code == UIH_PROTO_OK ? "ok" : uih_proto_code_str(code);
}

static int vector_failures = 0;

static void frame_row(const char * expect, const char * line, int row) {
    const char * json = NULL;
    size_t json_len = 0;
    uih_proto_code_e code = uih_proto_frame_decode(line, strlen(line), &json, &json_len);
    if (code == UIH_PROTO_OK) {
        uih_request_s req;
        uih_proto_code_e rc = uih_proto_request_parse(json, json_len, &req);
        if (rc == UIH_PROTO_PARSE) {
            code = rc;
        }
    }
    if (strcmp(expect, code_name(code))) {
        printf("  frames.txt row %d: expected %s, got %s: %s\n", row, expect, code_name(code), line);
        ++vector_failures;
    }
}

static void test_frame_vectors(void) {
    vector_failures = 0;
    ASSERT_TRUE(for_each_row("frames.txt", frame_row) >= 10);
    ASSERT_EQ(0, vector_failures);
}

static void summarize(const char * line, char * out, size_t size) {
    const char * json = NULL;
    size_t json_len = 0;
    uih_request_s req;
    uih_proto_code_e code = uih_proto_frame_decode(line, strlen(line), &json, &json_len);
    if (code == UIH_PROTO_OK) {
        code = uih_proto_request_parse(json, json_len, &req);
    }
    if (code != UIH_PROTO_OK) {
        int n = snprintf(out, size, "error=%s", uih_proto_code_str(code));
        if ((code == UIH_PROTO_ARG || code == UIH_PROTO_OP) && req.seq_valid) {
            snprintf(out + n, size - n, " seq=%u", (unsigned) req.seq);
        }
        return;
    }
    int n = snprintf(out, size, "op=%s seq=%u", uih_op_str(req.op), (unsigned) req.seq);
    if (req.op == UIH_OP_SET) {
        if (req.port_mask == UIH_PORT_ALL) {
            n += snprintf(out + n, size - n, " port=all");
        } else {
            int port = (req.port_mask == 1) ? 1 : ((req.port_mask == 2) ? 2 : 3);
            n += snprintf(out + n, size - n, " port=%d", port);
        }
        if (req.power >= 0) {
            n += snprintf(out + n, size - n, " power=%d", req.power);
        }
        if (req.data >= 0) {
            n += snprintf(out + n, size - n, " data=%d", req.data);
        }
    } else if (req.op == UIH_OP_STREAM) {
        snprintf(out + n, size - n, " rate_hz=%u", (unsigned) req.rate_hz);
    }
}

static void request_row(const char * line, const char * expect, int row) {
    char actual[128];
    summarize(line, actual, sizeof(actual));
    if (strcmp(expect, actual)) {
        printf("  requests.txt row %d:\n    expected: %s\n    actual:   %s\n", row, expect, actual);
        ++vector_failures;
    }
}

static void test_request_vectors(void) {
    vector_failures = 0;
    ASSERT_TRUE(for_each_row("requests.txt", request_row) >= 30);
    ASSERT_EQ(0, vector_failures);
}

// Port states matching the _PORTS list in test/vectors/generate.py.
static const uih_port_state_s PORTS[3] = {
    {1, true, true, false, 4797, 1767},
    {2, false, true, false, 12, 0},
    {3, true, false, true, 4788, -3},
};

struct message_case_s {
    const char * name;
    char line[LINE_MAX_];
};

static struct message_case_s expected[32];
static int expected_count = 0;

static void message_row(const char * name, const char * line, int row) {
    (void) row;
    snprintf(expected[expected_count].line, LINE_MAX_, "%s\n", line);
    expected[expected_count++].name = strdup(name);
}

static const char * expected_line(const char * name) {
    for (int i = 0; i < expected_count; ++i) {
        if (!strcmp(expected[i].name, name)) {
            return expected[i].line;
        }
    }
    return "(missing vector)";
}

#define CHECK_MESSAGE(name_) ASSERT_STR_EQ(expected_line(name_), w.buf)

static void test_message_vectors(void) {
    char buf[LINE_MAX_];
    uih_writer_s w;
    expected_count = 0;
    ASSERT_TRUE(for_each_row("messages.txt", message_row) >= 10);

    uih_msg_rsp_begin(&w, buf, sizeof(buf), 1, "ping", NULL);
    uih_msg_end(&w);
    CHECK_MESSAGE("rsp_ping");

    uih_msg_rsp_begin(&w, buf, sizeof(buf), 7, "set", NULL);
    uih_msg_ports(&w, "ports", &PORTS[1], 1);
    uih_msg_end(&w);
    CHECK_MESSAGE("rsp_set");

    uih_aux_s aux = {.vhost = true, .vext = false, .usb3 = true};
    uih_msg_rsp_begin(&w, buf, sizeof(buf), 5, "status", NULL);
    uih_msg_ports(&w, "ports", PORTS, 3);
    uih_msg_aux(&w, &aux);
    uih_msg_end(&w);
    CHECK_MESSAGE("rsp_status");

    uih_msg_rsp_begin(&w, buf, sizeof(buf), 11, "stream", NULL);
    uih_msg_u32(&w, "rate_hz", 10);
    uih_msg_end(&w);
    CHECK_MESSAGE("rsp_stream");

    uih_info_s info = {.fw = "0.1.0", .git = "c813f74", .idf = "v6.1", .serial = "B43A45B7BFBC",
                       .stm8_version = 4, .meter = true, .reset = "poweron", .uptime_ms = 1234};
    uih_msg_rsp_begin(&w, buf, sizeof(buf), 2, "info", NULL);
    uih_msg_info(&w, &info);
    uih_msg_end(&w);
    CHECK_MESSAGE("rsp_info");

    info.stm8_version = -1;
    info.meter = false;
    info.reset = "wdt";
    info.uptime_ms = 5;
    uih_msg_rsp_begin(&w, buf, sizeof(buf), 3, "info", NULL);
    uih_msg_info(&w, &info);
    uih_msg_end(&w);
    CHECK_MESSAGE("rsp_info_no_stm8");

    uih_msg_rsp_begin(&w, buf, sizeof(buf), 7, "set", "not_applied");
    uih_msg_end(&w);
    CHECK_MESSAGE("rsp_error");

    uih_msg_rsp_begin(&w, buf, sizeof(buf), 20, "fly", "op");
    uih_msg_end(&w);
    CHECK_MESSAGE("rsp_error_op");

    uih_msg_status(&w, buf, sizeof(buf), 42, 12345, PORTS, 3);
    CHECK_MESSAGE("status");

    uih_msg_event_begin(&w, buf, sizeof(buf), 0, 12, "boot");
    uih_msg_str(&w, "reset", "poweron");
    uih_msg_bool(&w, "defaults", true);
    uih_msg_end(&w);
    CHECK_MESSAGE("event_boot");

    uih_msg_event_begin(&w, buf, sizeof(buf), 3, 5021, "button");
    uih_msg_u32(&w, "port", 2);
    uih_msg_bool(&w, "power", false);
    uih_msg_end(&w);
    CHECK_MESSAGE("event_button");

    uih_msg_event_begin(&w, buf, sizeof(buf), 4, 6000, "fault");
    uih_msg_u32(&w, "port", 3);
    uih_msg_bool(&w, "fault", true);
    uih_msg_end(&w);
    CHECK_MESSAGE("event_fault");

    uih_msg_error(&w, buf, sizeof(buf), UIH_PROTO_CRC, false, 0);
    CHECK_MESSAGE("error_crc");

    uih_msg_error(&w, buf, sizeof(buf), UIH_PROTO_ARG, true, 21);
    CHECK_MESSAGE("error_arg_seq");
}

static void test_writer_overflow(void) {
    char buf[24];
    uih_writer_s w;
    uih_msg_status(&w, buf, sizeof(buf), 1, 2, PORTS, 3);
    ASSERT_TRUE(w.overflow);
    ASSERT_EQ(0, w.len);  // nothing to send
    ASSERT_EQ(0, buf[0]);
}

static void test_string_escaping(void) {
    char buf[128];
    uih_writer_s w;
    uih_msg_rsp_begin(&w, buf, sizeof(buf), 1, "a\"b\\c\x01", "op");
    uih_msg_end(&w);
    ASSERT_TRUE(strstr(buf, "\"op\":\"a\\\"b\\\\c\\u0001\"") != NULL);
}

static void test_negative_values(void) {
    char buf[256];
    uih_writer_s w;
    uih_port_state_s p = {.port = 1, .power = true, .data = true, .mv = -12, .ma_x10 = -2147483647 - 1};
    uih_msg_rsp_begin(&w, buf, sizeof(buf), 1, "status", NULL);
    uih_msg_ports(&w, "ports", &p, 1);
    uih_msg_end(&w);
    ASSERT_TRUE(strstr(buf, "\"mV\":-12,") != NULL);
    ASSERT_TRUE(strstr(buf, "\"mA\":-214748364.8") != NULL);
}

static void test_op_name_not_decodable(void) {
    uih_request_s req;
    const char * json = "{\"op\":\"\\u00e9\",\"seq\":3}";
    ASSERT_EQ(UIH_PROTO_OP, uih_proto_request_parse(json, strlen(json), &req));
    ASSERT_TRUE(req.seq_valid);
    ASSERT_STR_EQ("", req.op_name);
}

static void test_frame_edge_cases(void) {
    const char * json;
    size_t len;
    ASSERT_EQ(UIH_PROTO_OK, uih_proto_frame_decode("{}*a3a6bf43\r", 12, &json, &len));  // crc32("{}")
    ASSERT_EQ(2, len);
    ASSERT_EQ(UIH_PROTO_FORMAT, uih_proto_frame_decode("*", 1, &json, &len));
}

static void test_code_strings(void) {
    ASSERT_STR_EQ("format", uih_proto_code_str(UIH_PROTO_FORMAT));
    ASSERT_STR_EQ("overflow", uih_proto_code_str(UIH_PROTO_OVERFLOW));
    ASSERT_STR_EQ("not_applied", uih_proto_code_str(UIH_PROTO_NOT_APPLIED));
    ASSERT_STR_EQ("io", uih_proto_code_str(UIH_PROTO_IO));
    ASSERT_STR_EQ("busy", uih_proto_code_str(UIH_PROTO_BUSY));
    ASSERT_STR_EQ("unknown", uih_proto_code_str((uih_proto_code_e) 99));
    ASSERT_STR_EQ("reset", uih_op_str(UIH_OP_RESET));
    ASSERT_STR_EQ("?", uih_op_str((uih_op_e) 99));
}

int main(void) {
    RUN(test_frame_vectors);
    RUN(test_request_vectors);
    RUN(test_message_vectors);
    RUN(test_writer_overflow);
    RUN(test_string_escaping);
    RUN(test_negative_values);
    RUN(test_op_name_not_decodable);
    RUN(test_frame_edge_cases);
    RUN(test_code_strings);
    return test_main_result("proto");
}
