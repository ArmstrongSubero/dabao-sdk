/*
 * main.c: BIO FIFO and event test for baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Covers what bio_blink and bio_dma leave alone: the FIFOs hold nine words
 * (eight storage words and the output register in regfifo.v), a host read
 * of an empty FIFO returns the last word that left it, host writes to a
 * full FIFO are dropped, the FIFO level events in EVENT_STATUS, one
 * instruction reading the same FIFO on both ports pops it once, restarting
 * a core clears the FIFO with its number, x26 starts at all ones, x27 reads
 * back the event mask, x30 only waits when the mask is not zero, and a core
 * waiting on x30 wakes on a FIFO level event.
 */

#include "bao.h"
#include "hardware/bio.h"

static const uint32_t filler[] = {
    0x000D0993,  /* 000: addi x19,x26,0 */
    0x00100293,  /* 004: addi x5,x0,1 */
    0x00028813,  /* 008: addi x16,x5,0 */
    0x00128293,  /* 00C: addi x5,x5,1 */
    0xFF9FF06F,  /* 010: jal x0,8 <loop> */
};

static const uint32_t doubler[] = {
    0x010802B3,  /* 000: add x5,x16,x16 */
    0x00028893,  /* 004: addi x17,x5,0 */
    0x010802B3,  /* 008: add x5,x16,x16 */
    0x00028893,  /* 00C: addi x17,x5,0 */
    0x0000006F,  /* 010: jal x0,10 <self> */
};

static const uint32_t halt[] = {
    0x0000006F,  /* 000: jal x0,0 */
};

static const uint32_t waiter[] = {
    0x00000D93,  /* 000: addi x27,x0,0 */
    0x000F0293,  /* 004: addi x5,x30,0 */
    0x00100913,  /* 008: addi x18,x0,1 */
    0x01000DB7,  /* 00C: lui x27,0x1000 */
    0x000D8993,  /* 010: addi x19,x27,0 */
    0x000F0293,  /* 014: addi x5,x30,0 */
    0x00200913,  /* 018: addi x18,x0,2 */
    0x0000006F,  /* 01C: jal x0,1c <self> */
};

int main(void)
{
    char codes[16];
    uint32_t i, first, last, a, b, woke = 0;

    bao_init();
    bio_init(FCLK_HZ);
    bio_load_code_words(0, filler, sizeof(filler) / 4);
    bio_load_code_words(1, doubler, sizeof(doubler) / 4);
    bio_load_code_words(2, halt, 1);
    bio_load_code_words(3, waiter, sizeof(waiter) / 4);

    bio_start_cores(0x1);
    delay_us(5);
    bio_stop_cores(0x1);
    mini_printf("core fill level: %u\r\n", bio_fifo_level(0));
    mini_printf("x26 at reset: 0x%08x\r\n", bio_pop_fifo3());
    first = bio_pop_fifo0();
    for (i = 1; i < 9; i++)
        last = bio_pop_fifo0();
    mini_printf("core words: %u to %u, level now %u\r\n", first, last, bio_fifo_level(0));
    mini_printf("empty read returns: %u\r\n", bio_pop_fifo0());

    BIO_SFR_FIFO_CLR = 0xF;
    BIO_SFR_ELEVEL = (6u << 4) | 4u;
    BIO_SFR_ETYPE = (1u << 8) | (1u << 17);
    for (i = 0; i < 12; i++) {
        uint32_t ev;
        bio_push_fifo0(i + 1);
        ev = (bio_event_status() >> 24) & 3;
        codes[i] = ev == 0 ? '0' : ev == 1 ? 'E' : ev == 2 ? 'G' : 'B';
    }
    codes[12] = 0;
    mini_printf("level events: %s\r\n", codes);
    mini_printf("host fill level: %u\r\n", bio_fifo_level(0));
    first = bio_pop_fifo0();
    for (i = 1; i < 9; i++)
        last = bio_pop_fifo0();
    mini_printf("host words kept: %u to %u\r\n", first, last);
    BIO_SFR_ETYPE = 0;

    BIO_SFR_FIFO_CLR = 0xF;
    bio_push_fifo0(10);
    bio_push_fifo0(20);
    bio_push_fifo0(30);
    bio_start_cores(0x2);
    while (bio_fifo_level(1) < 2) { }
    a = bio_pop_fifo1();
    b = bio_pop_fifo1();
    mini_printf("double read: %u %u, FIFO0 left %u\r\n", a, b, bio_fifo_level(0));
    bio_stop_cores(0x2);

    BIO_SFR_FIFO_CLR = 0xF;
    bio_push_fifo2(1);
    bio_push_fifo2(2);
    bio_push_fifo2(3);
    a = bio_fifo_level(2);
    bio_start_cores(0x4);
    mini_printf("restart clears FIFO2: %u then %u\r\n", a, bio_fifo_level(2));
    bio_stop_cores(0x4);

    BIO_SFR_FIFO_CLR = 0xF;
    BIO_SFR_ELEVEL = 3u;
    BIO_SFR_ETYPE = 1u << 8;
    bio_start_cores(0x8);
    while (bio_fifo_level(3) < 1) { }
    mini_printf("x30 with mask 0 ran: %u\r\n", bio_pop_fifo2());
    mini_printf("x27 reads: 0x%08x\r\n", bio_pop_fifo3());
    for (i = 1; i <= 4 && !woke; i++) {
        bio_push_fifo0(i);
        delay_us(2);
        if (bio_fifo_level(2))
            woke = i;
    }
    mini_printf("x30 woke after push %u, marker %u\r\n", woke, bio_pop_fifo2());
    bio_stop_cores(0x8);
    mini_printf("[Done] bio_fifo\r\n");
    while (1) { }
}
