/*
 * main.c: BIO GPIO mask, event register, clock count and trap test for baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Core 0 drives BIO pins 4 to 7 through the gpio mask and reads them back
 * through x21 (one instruction later, since the pads lag the write by two
 * cycles and the input synchronizer by two more). x21 sets and clears the
 * masked pins, x22 sets, x23 clears where the value is 0, x24 and x25 set
 * and clear the direction. Core 1 sets and clears events with x28 and x29
 * while the host does the same through EVENT_SET and EVENT_CLR. Core 2
 * reads x31 twice in a row. Core 3 runs ebreak and traps. DBG_PADOUT and
 * DBG_PADOE are left out of the read mux in the RTL and read 0.
 */

#include "bao.h"
#include "hardware/bio.h"

static const uint32_t masker[] = {
    0x0F000D13,  /* 000: addi x26,x0,240 */
    0xFFF00C13,  /* 004: addi x24,x0,-1 */
    0x0A500A93,  /* 008: addi x21,x0,165 */
    0x00000013,  /* 00C: addi x0,x0,0 */
    0x000A8813,  /* 010: addi x16,x21,0 */
    0x01000B13,  /* 014: addi x22,x0,16 */
    0x00000013,  /* 018: addi x0,x0,0 */
    0x000A8813,  /* 01C: addi x16,x21,0 */
    0x07F00B93,  /* 020: addi x23,x0,127 */
    0x00000013,  /* 024: addi x0,x0,0 */
    0x000A8813,  /* 028: addi x16,x21,0 */
    0x03000D13,  /* 02C: addi x26,x0,48 */
    0x00000A93,  /* 030: addi x21,x0,0 */
    0x00000013,  /* 034: addi x0,x0,0 */
    0x000A8813,  /* 038: addi x16,x21,0 */
    0x02000C93,  /* 03C: addi x25,x0,32 */
    0x000D0893,  /* 040: addi x17,x26,0 */
    0x0000006F,  /* 044: jal x0,44 <self> */
};

static const uint32_t eventer[] = {
    0x00090293,  /* 000: addi x5,x18,0 */
    0x12300E13,  /* 004: addi x28,x0,291 */
    0x00100993,  /* 008: addi x19,x0,1 */
    0x00090293,  /* 00C: addi x5,x18,0 */
    0x10000E93,  /* 010: addi x29,x0,256 */
    0x00200993,  /* 014: addi x19,x0,2 */
    0x0000006F,  /* 018: jal x0,18 <self> */
};

static const uint32_t counter[] = {
    0x000F8813,  /* 000: addi x16,x31,0 */
    0x000F8813,  /* 004: addi x16,x31,0 */
    0x0000006F,  /* 008: jal x0,8 <self> */
};

static const uint32_t trapper[] = {
    0x00100293,  /* 000: addi x5,x0,1 */
    0x00100073,  /* 004: ebreak */
    0x0000006F,  /* 008: jal x0,8 <self> */
};

int main(void)
{
    uint32_t i, a, b;

    bao_init();
    bio_init(FCLK_HZ);
    bio_load_code_words(0, masker, sizeof(masker) / 4);
    bio_load_code_words(1, eventer, sizeof(eventer) / 4);
    bio_load_code_words(2, counter, sizeof(counter) / 4);
    bio_load_code_words(3, trapper, sizeof(trapper) / 4);
    for (i = 4; i < 8; i++)
        bio_map_pin(i);

    bio_start_cores(0x1);
    while (bio_fifo_level(1) < 1) { }
    mini_printf("pins 7..4:");
    for (i = 0; i < 4; i++)
        mini_printf(" %02x", bio_pop_fifo0() & 0xF0);
    mini_printf(", mask 0x%02x\r\n", bio_pop_fifo1());
    mini_printf("core 0 pc 0x%03x trap %u\r\n", bio_core_pc(0), bio_core_trapped(0));
    mini_printf("DBG_PADOUT 0x%08x DBG_PADOE 0x%08x\r\n", BIO_SFR_DBG_PADOUT, BIO_SFR_DBG_PADOE);

    BIO_SFR_FIFO_CLR = 0xF;
    bio_event_clear(0xFFFFFF);
    bio_start_cores(0x2);
    bio_push_fifo2(1);
    while (bio_fifo_level(3) < 1) { }
    (void)bio_pop_fifo3();
    mini_printf("core set: 0x%06x\r\n", bio_event_status() & 0xFFFFFF);
    bio_event_clear(0x003);
    mini_printf("host clear: 0x%06x, EVENT_CLR reads 0x%06x\r\n", bio_event_status() & 0xFFFFFF, BIO_SFR_EVENT_CLR);
    bio_push_fifo2(2);
    while (bio_fifo_level(3) < 1) { }
    (void)bio_pop_fifo3();
    mini_printf("core clear: 0x%06x\r\n", bio_event_status() & 0xFFFFFF);
    bio_event_set(0x400000);
    mini_printf("host set: 0x%06x, EVENT_SET reads 0x%06x\r\n", bio_event_status() & 0xFFFFFF, BIO_SFR_EVENT_SET);
    bio_event_clear(0xFFFFFF);
    bio_stop_cores(0x2);

    BIO_SFR_FIFO_CLR = 0xF;
    bio_start_cores(0x4);
    while (bio_fifo_level(0) < 2) { }
    a = bio_pop_fifo0();
    b = bio_pop_fifo0();
    mini_printf("x31: core %u, step %u\r\n", a >> 30, (b - a) & 0x3FFFFFFF);
    bio_stop_cores(0x4);

    bio_start_cores(0x8);
    delay_us(2);
    mini_printf("core 3 trap %u at pc 0x%03x\r\n", bio_core_trapped(3), bio_core_pc(3));
    bio_stop_cores(0x8);
    mini_printf("[Done] bio_gpio\r\n");
    while (1) { }
}
