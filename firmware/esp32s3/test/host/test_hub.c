/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih_test.h"
#include "fake_i2c.h"

static fake_bus_s bus;
static uih_hub_s hub;

static void setup(void) {
    fake_bus_init(&bus);
    uih_hub_ops_s ops = fake_ops(&bus);
    uih_hub_init(&hub, &ops);
}

static void test_first_boot_applies_defaults(void) {
    setup();
    ASSERT_TRUE(uih_hub_boot(&hub, false));
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(0x52, bus.ch[i] & 0x7F);  // power, data, 1.0 A
    }
    ASSERT_EQ(1, bus.muxoe);
    ASSERT_TRUE(hub.stm8_ok);
    ASSERT_EQ(4, hub.stm8_version);
    ASSERT_FALSE(bus.first_boot);  // AUXREG was read
}

static void test_poweron_reset_applies_defaults(void) {
    setup();
    bus.first_boot = false;  // power-up state: every register 0
    ASSERT_TRUE(uih_hub_boot(&hub, true));
    ASSERT_EQ(0x52, bus.ch[1]);
}

static void test_poweron_reason_with_configured_ports_keeps_them(void) {
    // esptool's watchdog reset reports power-on (2026-10-20), so the ESP32
    // reset reason alone does not mean the hub just powered up.
    setup();
    bus.first_boot = false;
    bus.ch[0] = 0x52;
    bus.ch[1] = 0x42;  // port 2 off
    bus.ch[2] = 0x52;
    ASSERT_FALSE(uih_hub_boot(&hub, true));
    ASSERT_EQ(0x42, bus.ch[1]);
}

static void test_stm8_late_at_boot(void) {
    setup();
    bus.stm8_present = false;
    ASSERT_FALSE(uih_hub_boot(&hub, true));
    ASSERT_FALSE(hub.stm8_ok);
    bus.stm8_present = true;  // answers on a later poll, after power-up
    bus.event_count = 0;
    uih_hub_poll(&hub);
    ASSERT_TRUE(hub.stm8_ok);
    ASSERT_EQ(4, hub.stm8_version);
    ASSERT_EQ(0x52, bus.ch[0]);
    ASSERT_EQ(1, bus.event_count);
    ASSERT_EQ(UIH_HUB_EVENT_STM8_RESET, bus.events[0].type);
}

static void test_firmware_reset_keeps_ports(void) {
    setup();
    bus.first_boot = false;
    bus.ch[0] = 0x52;
    bus.ch[1] = 0x42;   // port 2 off, as left by the previous firmware
    bus.ch[2] = 0x12;   // port 3 data off
    ASSERT_FALSE(uih_hub_boot(&hub, false));
    ASSERT_EQ(0, bus.stm8_writes);
    uih_port_state_s ports[3];
    uih_aux_s aux;
    uih_hub_snapshot(&hub, ports, &aux);
    ASSERT_TRUE(ports[0].power);
    ASSERT_FALSE(ports[1].power);
    ASSERT_FALSE(ports[2].data);
    ASSERT_TRUE(aux.vhost);
    ASSERT_TRUE(aux.usb3);
}

static void test_meter_init_and_read(void) {
    setup();
    uih_hub_boot(&hub, true);
    ASSERT_TRUE(hub.meter_ok);
    ASSERT_EQ(UIH_PAC_CTRL, bus.pac_ctrl);
    ASSERT_EQ(UIH_PAC_NEG_PWR_FSR, bus.pac_fsr);
    ASSERT_EQ(UIH_PAC_SMBUS_SETTINGS, bus.pac_smbus);
    int refreshes = bus.pac_refreshes;
    ASSERT_TRUE(refreshes >= 1);
    bus.vbus[2] = 34939;               // 4798 mV on meter channel 3
    bus.vsense[2] = (uint16_t) -2316;  // 176.7 mA
    bus.vbus[0] = 32768;               // 4500 mV on meter channel 1
    bus.vbus[1] = 16384;               // 2250 mV on meter channel 2
    uih_hub_poll(&hub);
    ASSERT_EQ(refreshes + 1, bus.pac_refreshes);  // read, then refresh
    uih_port_state_s ports[3];
    uih_hub_snapshot(&hub, ports, NULL);
    // The board routes meter channels 3, 1, 2 to ports 1, 2, 3 (stock
    // meterBoardMap; confirmed on hardware 2026-10-20).
    ASSERT_EQ(4798, ports[0].mv);
    ASSERT_EQ(1767, ports[0].ma_x10);
    ASSERT_EQ(4500, ports[1].mv);
    ASSERT_EQ(2250, ports[2].mv);
}

static void test_missing_devices(void) {
    setup();
    bus.stm8_present = false;
    bus.pac_present = false;
    ASSERT_FALSE(uih_hub_boot(&hub, true));
    ASSERT_FALSE(hub.stm8_ok);
    ASSERT_EQ(-1, hub.stm8_version);
    ASSERT_FALSE(hub.meter_ok);
    ASSERT_EQ(UIH_PROTO_IO, uih_hub_set(&hub, 0x01, 0, -1));
    uih_hub_poll(&hub);  // must not crash
}

static void test_set_power_one_port(void) {
    setup();
    uih_hub_boot(&hub, true);
    int writes = bus.stm8_writes;
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_set(&hub, 0x02, 0, -1));
    ASSERT_EQ(writes + 1, bus.stm8_writes);  // only port 2's register
    ASSERT_EQ(0x42, bus.ch[1]);
    ASSERT_EQ(0x52, bus.ch[0]);
    uih_port_state_s ports[3];
    uih_hub_snapshot(&hub, ports, NULL);
    ASSERT_FALSE(ports[1].power);
    ASSERT_TRUE(ports[1].data);
}

static void test_set_all_data(void) {
    setup();
    uih_hub_boot(&hub, true);
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_set(&hub, UIH_PORT_ALL, -1, 0));
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(0x12, bus.ch[i]);
    }
}

static void test_set_is_idempotent(void) {
    setup();
    uih_hub_boot(&hub, true);
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_set(&hub, 0x01, 1, 1));
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_set(&hub, 0x01, 1, 1));
    ASSERT_EQ(0x52, bus.ch[0]);
}

static void test_set_retries_lost_write(void) {
    setup();
    uih_hub_boot(&hub, true);
    bus.drop_channel_writes = 2;
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_set(&hub, 0x04, 0, -1));
    ASSERT_EQ(0x42, bus.ch[2]);
}

static void test_set_gives_up(void) {
    setup();
    uih_hub_boot(&hub, true);
    bus.drop_channel_writes = 100;
    ASSERT_EQ(UIH_PROTO_NOT_APPLIED, uih_hub_set(&hub, 0x04, 0, -1));
    uih_port_state_s ports[3];
    uih_hub_snapshot(&hub, ports, NULL);
    ASSERT_TRUE(ports[2].power);  // the snapshot shows the real state
}

static void test_set_retries_bus_errors(void) {
    setup();
    uih_hub_boot(&hub, true);
    bus.fail_transfers = 2;
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_set(&hub, 0x01, 0, -1));
    bus.fail_transfers = 100;
    ASSERT_EQ(UIH_PROTO_IO, uih_hub_set(&hub, 0x01, 1, -1));
}

static void test_set_retries_failed_write_and_verify(void) {
    setup();
    uih_hub_boot(&hub, true);
    bus.fail_skip = 1;       // the channel read passes, the write fails
    bus.fail_transfers = 1;
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_set(&hub, 0x01, 0, -1));
    ASSERT_EQ(0x42, bus.ch[0]);
    bus.fail_skip = 2;       // the read and write pass, the verify read fails
    bus.fail_transfers = 1;
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_set(&hub, 0x01, 1, -1));
    ASSERT_EQ(0x52, bus.ch[0]);
}

static void test_set_clears_stale_fault_bit_harmlessly(void) {
    setup();
    uih_hub_boot(&hub, true);
    bus.fault_pin[0] = true;
    uih_hub_poll(&hub);
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_set(&hub, 0x01, 1, -1));  // fault bit ignored
}

static void test_toggle_emits_button_event(void) {
    setup();
    uih_hub_boot(&hub, true);
    bus.event_count = 0;
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_toggle(&hub, 2));
    ASSERT_EQ(0x42, bus.ch[1]);
    ASSERT_EQ(1, bus.event_count);
    ASSERT_EQ(UIH_HUB_EVENT_BUTTON, bus.events[0].type);
    ASSERT_EQ(2, bus.events[0].port);
    ASSERT_FALSE(bus.events[0].value);
    ASSERT_EQ(UIH_PROTO_OK, uih_hub_toggle(&hub, 2));
    ASSERT_TRUE(bus.events[1].value);
    ASSERT_EQ(UIH_PROTO_ARG, uih_hub_toggle(&hub, 0));
    ASSERT_EQ(UIH_PROTO_ARG, uih_hub_toggle(&hub, 4));
}

static void test_fault_edges(void) {
    setup();
    uih_hub_boot(&hub, true);
    bus.event_count = 0;
    bus.fault_pin[2] = true;
    uih_hub_poll(&hub);
    uih_hub_poll(&hub);  // no repeat while it stays asserted
    bus.fault_pin[2] = false;
    uih_hub_poll(&hub);
    ASSERT_EQ(2, bus.event_count);
    ASSERT_EQ(UIH_HUB_EVENT_FAULT, bus.events[0].type);
    ASSERT_EQ(3, bus.events[0].port);
    ASSERT_TRUE(bus.events[0].value);
    ASSERT_FALSE(bus.events[1].value);
    ASSERT_EQ(0x52, bus.ch[2] & 0x7F);  // report-only: the port stays on
}

static void test_stm8_reset_reapplies_defaults(void) {
    setup();
    uih_hub_boot(&hub, true);
    uih_hub_set(&hub, 0x01, 0, -1);  // the host turned port 1 off
    bus.event_count = 0;
    // The STM8 browns out: registers cleared, first-boot flag set.
    bus.ch[0] = bus.ch[1] = bus.ch[2] = 0;
    bus.first_boot = true;
    uih_hub_poll(&hub);
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(0x52, bus.ch[i]);  // a normal hub at power-up: all on
    }
    ASSERT_EQ(1, bus.event_count);
    ASSERT_EQ(UIH_HUB_EVENT_STM8_RESET, bus.events[0].type);
}

static void test_poll_tracks_external_changes(void) {
    setup();
    uih_hub_boot(&hub, true);
    bus.ch[1] = 0x40;  // something turned port 2 off
    uih_hub_poll(&hub);
    uih_port_state_s ports[3];
    uih_hub_snapshot(&hub, ports, NULL);
    ASSERT_FALSE(ports[1].power);
}

static void test_poll_survives_bus_errors(void) {
    setup();
    uih_hub_boot(&hub, true);
    bus.fail_transfers = 3;
    uih_hub_poll(&hub);
    uih_port_state_s ports[3];
    uih_hub_snapshot(&hub, ports, NULL);
    ASSERT_TRUE(ports[0].power);  // previous state kept
}

int main(void) {
    RUN(test_first_boot_applies_defaults);
    RUN(test_poweron_reset_applies_defaults);
    RUN(test_poweron_reason_with_configured_ports_keeps_them);
    RUN(test_stm8_late_at_boot);
    RUN(test_firmware_reset_keeps_ports);
    RUN(test_meter_init_and_read);
    RUN(test_missing_devices);
    RUN(test_set_power_one_port);
    RUN(test_set_all_data);
    RUN(test_set_is_idempotent);
    RUN(test_set_retries_lost_write);
    RUN(test_set_gives_up);
    RUN(test_set_retries_bus_errors);
    RUN(test_set_retries_failed_write_and_verify);
    RUN(test_set_clears_stale_fault_bit_harmlessly);
    RUN(test_toggle_emits_button_event);
    RUN(test_fault_edges);
    RUN(test_stm8_reset_reapplies_defaults);
    RUN(test_poll_tracks_external_changes);
    RUN(test_poll_survives_bus_errors);
    return test_main_result("hub");
}
