/*
 * main.c: BIO interrupt and DMA request test for baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Core 0 sets BIO event 5 each time the host hands it a token; IRQMASK_0
 * routes event 5 to pioirq0, which the CPU takes through irq array 10.
 * The handler clears the event. Then the irq_edge quirk: in bio_bdma.sv
 * the edge mode line is irq_agg[i] & ~irq_agg_q, which keeps only bit 0 of
 * irq_agg_q, so line 1 in edge mode stays high while line 0's source is
 * low and drops once line 0's source is high. EVMAP routes pioirq0 and
 * pioirq1 back in as DMA requests, so EVSTAT1 shows both lines. Last,
 * EVMAP routes the UART2 transmit event (irq array 5 bit 9, DMA request
 * 57) to BIO event 7 and core 1 waits for it on x30.
 */

#include "bao.h"
#include "hardware/bio.h"
#include "hardware/irq.h"

static const uint32_t raiser[] = {
    0x00080293,  /* 000: addi x5,x16,0 */
    0x02000E13,  /* 004: addi x28,x0,32 */
    0xFF9FF06F,  /* 008: jal x0,0 <loop> */
};

static const uint32_t dmawait[] = {
    0x08000E93,  /* 000: addi x29,x0,128 */
    0x08000D93,  /* 004: addi x27,x0,128 */
    0x000F0293,  /* 008: addi x5,x30,0 */
    0x00700893,  /* 00C: addi x17,x0,7 */
    0x0000006F,  /* 010: jal x0,10 <self> */
};

static volatile uint32_t hits, spurious;

static void on_bio(uint32_t ev)
{
    (void)ev;
    if (bio_event_status() & 0x20) {
        bio_event_clear(0x20);
        hits++;
    } else {
        spurious++;
    }
}

static uint32_t lines(void)
{
    (void)BIO_SFR_EVSTAT1;
    delay_us(1);
    return BIO_SFR_EVSTAT1 & 0x18;
}

int main(void)
{
    uint32_t i, a, b, c, d;

    bao_init();
    bio_init(FCLK_HZ);
    bio_load_code_words(0, raiser, sizeof(raiser) / 4);
    bio_load_code_words(1, dmawait, sizeof(dmawait) / 4);

    BIO_SFR_IRQMASK_0 = 0x20;
    irq_init();
    irq_set_handler(IRQ_ARRAY10, on_bio);
    irq_enable_events(IRQ_ARRAY10, 1u << 3);
    bio_start_cores(0x1);
    for (i = 0; i < 3; i++) {
        bio_push_fifo0(i);
        delay_us(20);
    }
    bio_stop_cores(0x1);
    mini_printf("pioirq0 handled %u events\r\n", hits);
    irq_disable(IRQ_ARRAY10);
    BIO_SFR_IRQMASK_0 = 0;

    BIO_SFR_EVMAP1 = (1u << 3) | (1u << 4);
    BIO_SFR_IRQMASK_0 = 0x20;
    BIO_SFR_IRQMASK_1 = 0x40;
    BIO_SFR_IRQ_EDGE = 0x3;
    bio_event_clear(0xFFFFFF);
    bio_event_set(0x40);
    a = lines();
    b = lines();
    bio_event_set(0x20);
    c = lines();
    BIO_SFR_IRQ_EDGE = 0;
    d = lines();
    mini_printf("edge mode lines: %02x %02x, after event 5: %02x, level mode: %02x\r\n", a, b, c, d);
    BIO_SFR_IRQMASK_0 = 0;
    BIO_SFR_IRQMASK_1 = 0;
    BIO_SFR_EVMAP1 = 0;
    bio_event_clear(0xFFFFFF);

    delay_ms(5);
    BIO_SFR_EVMAP4 = 1u << 25;
    BIO_SFR_FIFO_CLR = 0xF;
    bio_start_cores(0x2);
    delay_us(20);
    a = bio_fifo_level(1);
    mini_printf("uart2 transmit as BIO event 7\r\n");
    delay_ms(2);
    mini_printf("core 1 waited with level %u, woke with marker %u\r\n", a, bio_fifo_level(1) ? bio_pop_fifo1() : 0);
    bio_stop_cores(0x2);
    BIO_SFR_EVMAP4 = 0;
    mini_printf("[Done] bio_irq\r\n");
    while (1) { }
}
