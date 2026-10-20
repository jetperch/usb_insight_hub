/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih_test.h"
#include "fake_i2c.h"
#include "uih/app.h"
#include "uih/crc32.h"

static fake_bus_s bus;
static uih_hub_s hub;
static uih_app_s app;

static char lines[32][1024];
static bool droppable[32];
static int line_count;
static bool tx_full;
static int bootloader_calls;
static int reset_calls;

static bool tx(void * ctx, const char * line, size_t len, bool may_drop) {
    (void) ctx;
    if (may_drop && tx_full) {
        return false;
    }
    if (line_count < 32) {
        snprintf(lines[line_count], sizeof(lines[0]), "%.*s", (int) len, line);
        droppable[line_count] = may_drop;
        line_count++;
    }
    return true;
}

static void do_bootloader(void * ctx) { (void) ctx; bootloader_calls++; }
static void do_reset(void * ctx) { (void) ctx; reset_calls++; }

static void setup(void) {
    fake_bus_init(&bus);
    uih_hub_ops_s ops = fake_ops(&bus);
    uih_hub_init(&hub, &ops);
    uih_app_platform_s pf = {
        .tx = tx, .bootloader = do_bootloader, .reset = do_reset, .ctx = NULL,
        .info = {.fw = "0.1.0", .git = "abc1234", .idf = "v6.1", .serial = "B43A45B7BFBC"},
    };
    line_count = 0;
    tx_full = false;
    bootloader_calls = 0;
    reset_calls = 0;
    uih_app_init(&app, &hub, &pf);
}

/// Send one framed request line, built from the JSON body.
static void request(const char * json) {
    char line[512];
    int n = snprintf(line, sizeof(line), "%s*%08lx", json,
                     (unsigned long) uih_crc32((const uint8_t *) json, strlen(json)));
    uih_app_line(&app, line, (size_t) n);
}

/// Return the body of the most recent line, after checking its frame.
static const char * last(void) {
    static char body[1024];
    if (!line_count) {
        return "(none)";
    }
    const char * json;
    size_t len;
    const char * line = lines[line_count - 1];
    if (uih_proto_frame_decode(line, strlen(line), &json, &len) != UIH_PROTO_OK) {
        return "(bad frame)";
    }
    snprintf(body, sizeof(body), "%.*s", (int) len, json);
    return body;
}

#define ASSERT_HAS(text) do { if (!strstr(last(), (text))) { \
    FAIL_("missing %s in %s", (text), last()); } } while (0)

static void test_boot_event(void) {
    setup();
    uih_app_boot(&app, true, "poweron");
    ASSERT_EQ(1, line_count);
    ASSERT_STR_EQ("{\"type\":\"event\",\"n\":0,\"t\":0,\"event\":\"boot\",\"reset\":\"poweron\",\"defaults\":true}", last());
    ASSERT_TRUE(lines[0][strlen(lines[0]) - 1] == '\n');
}

static void test_ping_info_status(void) {
    setup();
    uih_app_boot(&app, true, "poweron");
    request("{\"op\":\"ping\",\"seq\":1}");
    ASSERT_STR_EQ("{\"type\":\"rsp\",\"seq\":1,\"op\":\"ping\",\"status\":\"ok\"}", last());
    bus.now_ms = 1234;
    request("{\"op\":\"info\",\"seq\":2}");
    ASSERT_HAS("\"fw\":\"0.1.0\",\"git\":\"abc1234\",\"proto\":1,\"idf\":\"v6.1\",\"serial\":\"B43A45B7BFBC\"");
    ASSERT_HAS("\"stm8\":4,\"meter\":true,\"display\":\"starting\",\"reset\":\"poweron\",\"uptime_ms\":1234");
    // The display task reports its state later, from its own task.
    uih_app_display_status(&app, "ok");
    request("{\"op\":\"info\",\"seq\":4}");
    ASSERT_HAS("\"display\":\"ok\"");
    request("{\"op\":\"status\",\"seq\":3}");
    ASSERT_HAS("\"ports\":[{\"port\":1,\"power\":true,\"data\":true,\"fault\":false");
    ASSERT_HAS("\"aux\":{\"vhost\":true,\"vext\":false,\"usb3\":true}");
    ASSERT_FALSE(droppable[line_count - 1]);
}

static void test_set_reports_read_back(void) {
    setup();
    uih_app_boot(&app, true, "poweron");
    request("{\"op\":\"set\",\"seq\":7,\"port\":2,\"power\":false}");
    ASSERT_HAS("\"seq\":7,\"op\":\"set\",\"status\":\"ok\",\"ports\":[{\"port\":2,\"power\":false,\"data\":true");
    ASSERT_EQ(0x42, bus.ch[1]);
    request("{\"op\":\"set\",\"seq\":8,\"port\":\"all\",\"data\":false}");
    ASSERT_HAS("{\"port\":1,");
    ASSERT_HAS("{\"port\":3,\"power\":true,\"data\":false");
}

static void test_set_errors(void) {
    setup();
    uih_app_boot(&app, true, "poweron");
    bus.drop_channel_writes = 100;
    request("{\"op\":\"set\",\"seq\":9,\"port\":1,\"power\":false}");
    ASSERT_STR_EQ("{\"type\":\"rsp\",\"seq\":9,\"op\":\"set\",\"status\":\"error\",\"code\":\"not_applied\"}", last());
    bus.drop_channel_writes = 0;
    bus.fail_transfers = 100;
    request("{\"op\":\"set\",\"seq\":10,\"port\":1,\"power\":false}");
    ASSERT_HAS("\"code\":\"io\"");
}

static void test_bad_lines(void) {
    setup();
    uih_app_line(&app, "garbage", 7);
    ASSERT_STR_EQ("{\"type\":\"error\",\"code\":\"format\"}", last());
    uih_app_line(&app, "{\"op\":\"ping\",\"seq\":1}*00000000", 30);
    ASSERT_STR_EQ("{\"type\":\"error\",\"code\":\"crc\"}", last());
    request("{\"op\":");
    ASSERT_STR_EQ("{\"type\":\"error\",\"code\":\"parse\"}", last());
    request("{\"op\":\"set\",\"seq\":4,\"port\":9,\"power\":true}");
    ASSERT_STR_EQ("{\"type\":\"error\",\"code\":\"arg\",\"seq\":4}", last());
    request("{\"op\":\"fly\",\"seq\":5}");
    ASSERT_STR_EQ("{\"type\":\"rsp\",\"seq\":5,\"op\":\"fly\",\"status\":\"error\",\"code\":\"op\"}", last());
    uih_app_overflow(&app);
    ASSERT_STR_EQ("{\"type\":\"error\",\"code\":\"overflow\"}", last());
    uih_app_line(&app, "", 0);  // empty lines are ignored
    ASSERT_STR_EQ("{\"type\":\"error\",\"code\":\"overflow\"}", last());
}

static void test_stream(void) {
    setup();
    uih_app_boot(&app, true, "poweron");
    request("{\"op\":\"stream\",\"seq\":11,\"rate_hz\":10}");
    ASSERT_STR_EQ("{\"type\":\"rsp\",\"seq\":11,\"op\":\"stream\",\"status\":\"ok\",\"rate_hz\":10}", last());
    int before = line_count;
    for (int ms = 0; ms <= 1000; ms += 10) {
        bus.now_ms = (uint32_t) ms;
        uih_app_tick(&app);
    }
    int status = line_count - before;
    ASSERT_TRUE(status >= 10 && status <= 11);
    ASSERT_TRUE(droppable[line_count - 1]);
    ASSERT_HAS("\"type\":\"status\",\"n\":");
    // Dropped status messages still advance n.
    tx_full = true;
    for (int ms = 1010; ms <= 1500; ms += 10) {
        bus.now_ms = (uint32_t) ms;
        uih_app_tick(&app);
    }
    tx_full = false;
    uint32_t n_before = app.status_n;
    ASSERT_TRUE(n_before >= 15);
    request("{\"op\":\"stream\",\"seq\":12,\"rate_hz\":0}");
    before = line_count;
    for (int ms = 1510; ms <= 3000; ms += 10) {
        bus.now_ms = (uint32_t) ms;
        uih_app_tick(&app);
    }
    ASSERT_EQ(before, line_count);
}

static void test_stream_does_not_burst_after_a_stall(void) {
    setup();
    uih_app_boot(&app, true, "poweron");
    request("{\"op\":\"stream\",\"seq\":1,\"rate_hz\":10}");
    int before = line_count;
    bus.now_ms = 5000;   // the hub task stalled for seconds
    uih_app_tick(&app);
    uih_app_tick(&app);
    ASSERT_EQ(before + 1, line_count);  // one status, not a catch-up burst
    bus.now_ms = 5100;
    uih_app_tick(&app);
    ASSERT_EQ(before + 2, line_count);
}

static void test_hub_events_are_sent(void) {
    setup();
    uih_app_boot(&app, true, "poweron");
    uih_app_button(&app, 3);
    ASSERT_STR_EQ("{\"type\":\"event\",\"n\":1,\"t\":0,\"event\":\"button\",\"port\":3,\"power\":false}", last());
    bus.fault_pin[0] = true;
    bus.now_ms = 100;
    uih_app_tick(&app);  // poll
    ASSERT_STR_EQ("{\"type\":\"event\",\"n\":2,\"t\":100,\"event\":\"fault\",\"port\":1,\"fault\":true}", last());
    bus.first_boot = true;
    bus.now_ms = 200;
    uih_app_tick(&app);
    ASSERT_HAS("\"event\":\"stm8_reset\"");
}

static void test_bootloader_and_reset_after_response(void) {
    setup();
    uih_app_boot(&app, true, "poweron");
    bus.now_ms = 1000;
    request("{\"op\":\"bootloader\",\"seq\":13}");
    ASSERT_STR_EQ("{\"type\":\"rsp\",\"seq\":13,\"op\":\"bootloader\",\"status\":\"ok\"}", last());
    uih_app_tick(&app);
    ASSERT_EQ(0, bootloader_calls);  // the response goes out first
    bus.now_ms = 1100;
    uih_app_tick(&app);
    ASSERT_EQ(1, bootloader_calls);
    request("{\"op\":\"reset\",\"seq\":14}");
    bus.now_ms = 1250;
    uih_app_tick(&app);
    ASSERT_EQ(1, reset_calls);
}

static void test_info_without_stm8(void) {
    fake_bus_init(&bus);
    bus.stm8_present = false;
    bus.pac_present = false;
    uih_hub_ops_s ops = fake_ops(&bus);
    uih_hub_init(&hub, &ops);
    uih_app_platform_s pf = {.tx = tx, .info = {.fw = "f", .git = "g", .idf = "i", .serial = "s"}};
    line_count = 0;
    uih_app_init(&app, &hub, &pf);
    uih_app_boot(&app, false, "wdt");
    ASSERT_HAS("\"defaults\":false");
    request("{\"op\":\"info\",\"seq\":1}");
    ASSERT_HAS("\"stm8\":null,\"meter\":false,\"display\":\"starting\",\"reset\":\"wdt\"");
}

int main(void) {
    RUN(test_boot_event);
    RUN(test_ping_info_status);
    RUN(test_set_reports_read_back);
    RUN(test_set_errors);
    RUN(test_bad_lines);
    RUN(test_stream);
    RUN(test_stream_does_not_burst_after_a_stall);
    RUN(test_hub_events_are_sent);
    RUN(test_bootloader_and_reset_after_response);
    RUN(test_info_without_stm8);
    return test_main_result("app");
}
