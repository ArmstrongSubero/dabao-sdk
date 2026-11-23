/*
 * main.c: BIO quantum divider test for baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cores 0 and 2 wait for a quantum and push x31, the aclk count since the
 * core was enabled, so the FIFO holds the quantum times. An integer
 * divider gives an exact period, a fraction of 128/256 alternates between
 * the two neighbouring periods (pio_divider.v), and two cores started in
 * one write run their dividers in step. Core 3 times two adds with a
 * divider of 1001: one with the immediate 21 and one with the immediate
 * 20. Bits 24:20 of the second read as register 20, and the BIO decodes
 * the quantum halt from those bits whatever the instruction is, so it
 * only clocks on quantum cycles while it is the decoded instruction. The
 * divider is odd because the RAM ready line toggles every cycle while the
 * core clock is gated; with an even divider the core can meet it low on
 * every quantum and never finish its fetch.
 */

#include "bao.h"
#include "hardware/bio.h"

static const uint32_t stamp0[] = {
    0x00000A13,  /* 000: addi x20,x0,0 */
    0x000F8813,  /* 004: addi x16,x31,0 */
    0xFF9FF06F,  /* 008: jal x0,0 <loop> */
};

static const uint32_t stamp2[] = {
    0x00000A13,  /* 000: addi x20,x0,0 */
    0x000F8913,  /* 004: addi x18,x31,0 */
    0xFF9FF06F,  /* 008: jal x0,0 <loop> */
};

static const uint32_t field20[] = {
    0x00000A13,  /* 000: addi x20,x0,0 */
    0x000F8993,  /* 004: addi x19,x31,0 */
    0x01528293,  /* 008: addi x5,x5,21 */
    0x000F8993,  /* 00C: addi x19,x31,0 */
    0x00000A13,  /* 010: addi x20,x0,0 */
    0x000F8993,  /* 014: addi x19,x31,0 */
    0x01428293,  /* 018: addi x5,x5,20 */
    0x000F8993,  /* 01C: addi x19,x31,0 */
    0x0000006F,  /* 020: jal x0,20 <self> */
};

static void periods(uint16_t di, uint8_t df)
{
    uint32_t prev, i, d, lo = 0xFFFFFFFF, hi = 0, sum = 0;
    BIO_SFR_FIFO_CLR = 0xF;
    bio_set_divider(0, di, df);
    bio_start_cores(0x1);
    while (bio_fifo_level(0) < 9) { }
    bio_stop_cores(0x1);
    prev = bio_pop_fifo0();
    for (i = 0; i < 8; i++) {
        uint32_t v = bio_pop_fifo0();
        d = (v - prev) & 0x3FFFFFFF;
        prev = v;
        sum += d;
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
    mini_printf("divider %u+%u/256: periods %u to %u, 8 periods %u\r\n", di, df, lo, hi, sum);
}

int main(void)
{
    uint32_t i, same = 0, t[4];

    bao_init();
    bio_init(FCLK_HZ);
    bio_load_code_words(0, stamp0, sizeof(stamp0) / 4);
    bio_load_code_words(2, stamp2, sizeof(stamp2) / 4);
    bio_load_code_words(3, field20, sizeof(field20) / 4);

    periods(1000, 0);
    periods(1000, 128);
    periods(37, 64);

    BIO_SFR_FIFO_CLR = 0xF;
    bio_set_divider(0, 700, 0);
    bio_set_divider(2, 700, 0);
    bio_start_cores(0x5);
    while (bio_fifo_level(2) < 9) { }
    bio_stop_cores(0x5);
    for (i = 0; i < 9; i++)
        same += (bio_pop_fifo0() & 0x3FFFFFFF) == (bio_pop_fifo2() & 0x3FFFFFFF);
    mini_printf("started together: %u of 9 quanta equal\r\n", same);

    BIO_SFR_FIFO_CLR = 0xF;
    bio_set_divider(3, 1001, 0);
    bio_start_cores(0x8);
    while (bio_fifo_level(3) < 4) { }
    bio_stop_cores(0x8);
    for (i = 0; i < 4; i++)
        t[i] = bio_pop_fifo3();
    mini_printf("addi 21 took %u cycles, addi 20 took %u quanta\r\n",
                (t[1] - t[0]) & 0x3FFFFFFF, (((t[3] - t[2]) & 0x3FFFFFFF) + 500) / 1001);
    mini_printf("[Done] bio_div\r\n");
    while (1) { }
}
