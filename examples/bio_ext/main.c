/*
 * main.c: BIO external clock test for baoemu, core 1 clocked by core 0 on BIO pin 2
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bao.h"
#include "hardware/bio.h"

static const uint32_t c0[] = { 0x00400293,0x00028D13,0x00028C13,0x00028B13,0x00000A13,0x00000B93,0x00000A13,0xFF1FF06F };
static const uint32_t c1[] = { 0x00000313,0x00000A13,0x00130313,0x00030813,0xFF5FF06F };

int main(void)
{
    bao_init();
    bio_init(FCLK_HZ);
    bio_load_code_words(0, c0, sizeof(c0) / 4);
    bio_load_code_words(1, c1, sizeof(c1) / 4);
    bio_map_pin(2);
    bio_set_divider(0, 7000, 0);
    BIO_SFR_EXTCLOCK = BIO_EXTCLK_USE(1) | BIO_EXTCLK_GPIO_1(2);
    bio_start_cores(0x3);
    uint64_t t0 = millis();
    uint32_t n = 0, last = 0, bad = 0;
    while (millis() - t0 < 10) {
        if (bio_fifo_level(0)) {
            uint32_t v = bio_pop_fifo0();
            if (v != last + 1) bad++;
            last = v;
            n++;
        }
    }
    mini_printf("edges counted by core 1: %u, out of order: %u\r\n", n, bad);
    while (1) { }
}
