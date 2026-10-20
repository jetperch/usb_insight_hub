/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih_test.h"
#include "uih/regs.h"

static void test_channel_encode_matches_stock(void) {
    // Stock: ilim << 1 | 0x40 if data | 0x10 if power (BaseMCU::encodeCHREG).
    ASSERT_EQ(0x52, uih_stm8_channel_encode(true, true, UIH_ILIM_1_0));
    ASSERT_EQ(0x12, uih_stm8_channel_encode(true, false, UIH_ILIM_1_0));
    ASSERT_EQ(0x42, uih_stm8_channel_encode(false, true, UIH_ILIM_1_0));
    ASSERT_EQ(0x00, uih_stm8_channel_encode(false, false, UIH_ILIM_0_5));
    ASSERT_EQ(0x56, uih_stm8_channel_encode(true, true, UIH_ILIM_2_0));
}

static void test_channel_decode(void) {
    uih_stm8_channel_s ch = uih_stm8_channel_decode(0xD2);
    ASSERT_TRUE(ch.fault);
    ASSERT_TRUE(ch.data);
    ASSERT_TRUE(ch.power);
    ASSERT_EQ(UIH_ILIM_1_0, ch.ilim);
    ch = uih_stm8_channel_decode(0x04);
    ASSERT_FALSE(ch.fault || ch.data || ch.power);
    ASSERT_EQ(UIH_ILIM_1_5, ch.ilim);
}

static void test_channel_matches_ignores_fault(void) {
    ASSERT_TRUE(uih_stm8_channel_matches(0x52, 0xD2));   // fault bit is read-only
    ASSERT_FALSE(uih_stm8_channel_matches(0x52, 0x42));  // power differs
    ASSERT_FALSE(uih_stm8_channel_matches(0x52, 0x50));  // ilim differs
    ASSERT_TRUE(uih_stm8_channel_matches(0x52, 0x53));   // bit 0 is unused
}

static void test_aux_decode(void) {
    uih_aux_s aux = uih_stm8_aux_decode(0x1D);  // vhost, mux oe, mux sel, first boot
    ASSERT_TRUE(aux.vhost);
    ASSERT_FALSE(aux.vext);
    ASSERT_TRUE(aux.usb3);
    ASSERT_TRUE(uih_stm8_aux_first_boot(0x1D));
    ASSERT_FALSE(uih_stm8_aux_first_boot(0x0F));
    aux = uih_stm8_aux_decode(0x02);
    ASSERT_FALSE(aux.vhost);
    ASSERT_TRUE(aux.vext);
    ASSERT_FALSE(aux.usb3);
}

static void test_pac_voltage(void) {
    ASSERT_EQ(0, uih_pac_vbus_mv(0));
    ASSERT_EQ(4500, uih_pac_vbus_mv(0x8000));  // half of 9 V
    ASSERT_EQ(9000, uih_pac_vbus_mv(0xFFFF));  // 8999.86 rounds up
    ASSERT_EQ(4798, uih_pac_vbus_mv(34939));   // 34939 / 65536 * 9000 = 4798.1
}

static void test_pac_current_sign_and_scale(void) {
    // VSENSE is signed; the shunt is wired reversed, so negative raw is
    // current flowing to the device (PAC194x::readAvgMeter).
    ASSERT_EQ(0, uih_pac_vsense_ma_x10(0));
    ASSERT_EQ(25000, uih_pac_vsense_ma_x10(0x8000));    // -32768: +2500.0 mA
    ASSERT_EQ(-24999, uih_pac_vsense_ma_x10(0x7FFF));   // +32767: -2499.9 mA
    ASSERT_EQ(1767, uih_pac_vsense_ma_x10((uint16_t) -2316));   // 176.7 mA
    ASSERT_EQ(-3, uih_pac_vsense_ma_x10(4));                     // -0.3 mA
}

int main(void) {
    RUN(test_channel_encode_matches_stock);
    RUN(test_channel_decode);
    RUN(test_channel_matches_ignores_fault);
    RUN(test_aux_decode);
    RUN(test_pac_voltage);
    RUN(test_pac_current_sign_and_scale);
    return test_main_result("regs");
}
