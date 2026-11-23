/*
 * main.c: BIO DMA filter window and gutter test for baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Core 0 runs rounds: it takes a window address, a value and an outside
 * address from the FIFOs, stores the value inside the window and outside
 * it, loads the outside address back and loads the next word of the
 * window. A refused access is not dropped: axil_filter swaps the address
 * for the gutter, so the store lands in the gutter word and the load reads
 * it. SFR_CONFIG bit 6 is wired to nothing (the peripheral filter takes
 * bit 7 too), and bit 7 turns both filters off. SRAM is on the memory side
 * and IFRAM on the peripheral side, each with its own gutter. Core 1 then
 * stores to 0x1100, which is its own RAM at 0x100, and loads from
 * 0x20000000, which no bus decodes and so reads 0.
 *
 * With the SDK driver before the start race fix, core 1 never got there
 * on the chip. bio_init left it stopped in the fetch of its mem_init jump
 * to self at 0x018, and bio_start_cores let it run on for one pclk before
 * the restart reached it: that fetch returned word 0 (the RAM word the host
 * last addressed), so it ran lui x14,0x1 as if at 0x018, pushed x13 from
 * 0x01C and stored x15 to 0x1000, word 0 of its RAM, then trapped on that
 * word after the restart. The driver now starts only cores it left trapped
 * on an ebreak, so this should not happen; the last part still waits at
 * most 500 ms and if core 1 is stuck reports the FIFO level, the core's pc
 * and trap bit and the word it pushed.
 */

#include "bao.h"
#include "hardware/bio.h"

static const uint32_t rounder[] = {
    0x00080513,  /* 000: addi x10,x16,0 */
    0x00088593,  /* 004: addi x11,x17,0 */
    0x00080613,  /* 008: addi x12,x16,0 */
    0x00C52023,  /* 00C: sw x12,0(x10) */
    0x00C5A023,  /* 010: sw x12,0(x11) */
    0x0005A683,  /* 014: lw x13,0(x11) */
    0x00068913,  /* 018: addi x18,x13,0 */
    0x00452683,  /* 01C: lw x13,4(x10) */
    0x00068913,  /* 020: addi x18,x13,0 */
    0xFDDFF06F,  /* 024: jal x0,0 <loop> */
};

static const uint32_t aliaser[] = {
    0x00001737,  /* 000: lui x14,0x1 */
    0x05A00793,  /* 004: addi x15,x0,90 */
    0x10F72023,  /* 008: sw x15,256(x14) */
    0x10002683,  /* 00C: lw x13,256(x0) */
    0x00068813,  /* 010: addi x16,x13,0 */
    0x20000737,  /* 014: lui x14,0x20000 */
    0x00072683,  /* 018: lw x13,0(x14) */
    0x00068813,  /* 01C: addi x16,x13,0 */
    0x00F72023,  /* 020: sw x15,0(x14) */
    0x0000006F,  /* 024: jal x0,24 <self> */
};

static volatile uint32_t win_buf[1024] __attribute__((aligned(4096)));
static volatile uint32_t out_buf[1024] __attribute__((aligned(4096)));
static volatile uint32_t gut_buf[1024] __attribute__((aligned(4096)));
static volatile uint32_t ifr_out[1024] __attribute__((section(".dma_buffers"), aligned(4096)));
static volatile uint32_t ifr_gut[16] __attribute__((section(".dma_buffers"), aligned(64)));

static void flush(void)
{
    __asm__ volatile ("fence\n.word 0x500F\nnop\nnop\nnop\nnop\n" ::: "memory");
}

static void round_(uint32_t n, uint32_t config, volatile uint32_t *out, volatile uint32_t *gut)
{
    uint32_t v = 0x1000u + n, a, b;
    BIO_SFR_CONFIG = config;
    flush();
    bio_push_fifo0((uint32_t)(uintptr_t)win_buf);
    bio_push_fifo1((uint32_t)(uintptr_t)out);
    bio_push_fifo0(v);
    while (bio_fifo_level(2) < 2) { }
    a = bio_pop_fifo2();
    b = bio_pop_fifo2();
    flush();
    mini_printf("round %u: window %s, outside %s, gutter %s, load 0x%x, window load 0x%x\r\n", n,
                win_buf[0] == v ? "stored" : "missed",
                out[0] == v ? "stored" : "untouched",
                gut[0] == v ? "stored" : "untouched", a, b);
}

int main(void)
{
    uint32_t i, a, b, lvl;
    uint64_t t0;
    uint32_t base = BIO_CONFIG_CLOCKING_MODE(3);

    bao_init();
    for (i = 0; i < 16; i++) {
        ifr_out[i] = 0;
        ifr_gut[i] = 0;
    }
    win_buf[1] = 0xCAFE;
    bio_init(FCLK_HZ);
    bio_load_code_words(0, rounder, sizeof(rounder) / 4);
    bio_load_code_words(1, aliaser, sizeof(aliaser) / 4);
    bio_allow_range(0, (uint32_t)(uintptr_t)win_buf, 64);
    BIO_SFR_MEM_GUTTER = (uint32_t)(uintptr_t)gut_buf;
    BIO_SFR_PERI_GUTTER = (uint32_t)(uintptr_t)ifr_gut;
    mini_printf("gutters read back 0x%x 0x%x, filter base reads 0x%x\r\n",
                BIO_SFR_MEM_GUTTER, BIO_SFR_PERI_GUTTER, BIO_SFR_FILTER_BASE_0);

    bio_start_cores(0x1);
    round_(1, base, out_buf, gut_buf);
    round_(2, base | BIO_CONFIG_DISABLE_FILTER_PERI, out_buf, gut_buf);
    round_(3, base, ifr_out, ifr_gut);
    round_(4, base | BIO_CONFIG_DISABLE_FILTER_PERI, ifr_out, ifr_gut);
    round_(5, base | BIO_CONFIG_DISABLE_FILTER_MEM, out_buf, gut_buf);
    round_(6, base | BIO_CONFIG_DISABLE_FILTER_MEM, ifr_out, ifr_gut);
    bio_stop_cores(0x1);
    BIO_SFR_CONFIG = base;

    BIO_SFR_FIFO_CLR = 0xF;
    bio_start_cores(0x2);
    t0 = millis();
    while (bio_fifo_level(0) < 2 && millis() - t0 < 500) { }
    lvl = bio_fifo_level(0);
    if (lvl >= 2) {
        a = bio_pop_fifo0();
        b = bio_pop_fifo0();
        bio_stop_cores(0x2);
        mini_printf("low alias load 0x%x, RAM word 0x%x, undecoded load 0x%x\r\n", a, bio_imem(1)[0x40], b);
    } else {
        mini_printf("core 1 stuck: FIFO0 level %u, pc 0x%03x, trap %u\r\n", lvl, bio_core_pc(1), bio_core_trapped(1));
        if (lvl)
            mini_printf("first word 0x%x\r\n", bio_pop_fifo0());
        bio_stop_cores(0x2);
    }
    mini_printf("[Done] bio_filter\r\n");
    while (1) { }
}
