/*
 * main.c: BIO snap to quantum test for baoemu, input and output
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Core 1 toggles BIO pin 6 every few cycles. Core 0 waits for its quantum,
 * reads the pins, waits about forty cycles and reads them again, pushing
 * the two samples to FIFO0 and FIFO1. Without snap input the two samples
 * of a pair often differ; with snap input on core 0's quantum both come
 * from the same strobe and always agree. Then core 2 toggles BIO pin 7
 * freely with snap output on core 3's divider of 1000, so PB7 may only
 * change on those strobes, every 500 CPU cycles.
 */

#include "bao.h"
#include "hardware/bio.h"

static const uint32_t sampler[] = {
    0x00000A13,  /* 000: addi x20,x0,0 */
    0x000A8813,  /* 004: addi x16,x21,0 */
    0x00A00293,  /* 008: addi x5,x0,10 */
    0xFFF28293,  /* 00C: addi x5,x5,-1 */
    0xFE029EE3,  /* 010: bne x5,x0,c <d> */
    0x000A8893,  /* 014: addi x17,x21,0 */
    0xFE9FF06F,  /* 018: jal x0,0 <loop> */
};

static const uint32_t toggle6[] = {
    0x04000D13,  /* 000: addi x26,x0,64 */
    0x04000C13,  /* 004: addi x24,x0,64 */
    0x04000B13,  /* 008: addi x22,x0,64 */
    0x00000B93,  /* 00C: addi x23,x0,0 */
    0xFF9FF06F,  /* 010: jal x0,8 <loop> */
};

static const uint32_t toggle7[] = {
    0x08000D13,  /* 000: addi x26,x0,128 */
    0x08000C13,  /* 004: addi x24,x0,128 */
    0x08000B13,  /* 008: addi x22,x0,128 */
    0x00000B93,  /* 00C: addi x23,x0,0 */
    0xFF9FF06F,  /* 010: jal x0,8 <loop> */
};

static uint32_t sample_pairs(uint32_t config)
{
    uint32_t differ = 0, i;
    BIO_SFR_CONFIG = config;
    BIO_SFR_FIFO_CLR = 0xF;
    bio_start_cores(0x3);
    while (bio_fifo_level(1) < 9) { }
    bio_stop_cores(0x3);
    for (i = 0; i < 9; i++) {
        uint32_t a = bio_pop_fifo0(), b = bio_pop_fifo1();
        if ((a ^ b) & (1u << 6))
            differ++;
    }
    return differ;
}

int main(void)
{
    bao_init();
    bio_init(FCLK_HZ);
    bio_load_code_words(0, sampler, sizeof(sampler) / 4);
    bio_load_code_words(1, toggle6, sizeof(toggle6) / 4);
    bio_load_code_words(2, toggle7, sizeof(toggle7) / 4);
    bio_map_pin(6);
    bio_map_pin(7);
    bio_set_divider(0, 4000, 0);
    bio_set_divider(3, 1000, 0);

    mini_printf("snap input off: %u of 9 pairs differ\r\n",
                sample_pairs(BIO_CONFIG_CLOCKING_MODE(3)));
    mini_printf("snap input on: %u of 9 pairs differ\r\n",
                sample_pairs(BIO_CONFIG_CLOCKING_MODE(3) | BIO_CONFIG_SNAP_IN_EN | BIO_CONFIG_SNAP_IN_WHICH(0)));

    BIO_SFR_CONFIG = BIO_CONFIG_CLOCKING_MODE(3) | BIO_CONFIG_SNAP_OUT_EN | BIO_CONFIG_SNAP_OUT_WHICH(3);
    BIO_SFR_CTRL = BIO_CTRL_CLKDIV_RESTART(3);
    bio_start_cores(0x4);
    delay_us(200);
    bio_stop_cores(0x4);
    mini_printf("snap output done\r\n");
    while (1) { }
}
