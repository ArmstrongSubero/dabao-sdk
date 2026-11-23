/*
 * main.c: machine mode services and interrupts under the BAO_MMU sandbox
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Built with the SDK's BAO_MMU sandbox (the file bao_mmu next to this one
 * selects it). From supervisor mode it
 *
 *   hashes "abc" with the SDK SHA driver, which runs in machine mode
 *   behind an ecall, and prints the digest (FIPS 180-2 vector);
 *   draws two words from the TRNG the same way;
 *   takes Timer0 interrupts through the SDK irq code, delegated to
 *   supervisor mode, and counts them;
 *   prints satp and reads the root page table, which is mapped read only;
 *   finally passes the page table as the digest buffer, which the service
 *   dispatcher must refuse with a report naming the pointer, then halt.
 *
 * Only SDK driver calls reach the SCE. The refused call, if it went
 * through, would write 32 bytes over unused root entries in SRAM.
 */

#include "bao.h"

#ifndef BAO_MMU
#error "this program needs the BAO_MMU sandbox: keep the bao_mmu file next to it"
#endif
#include "hardware/sha.h"
#include "hardware/trng.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

extern char _mmu_tables_start[];

static volatile uint32_t s_ticks;

static void on_timer0(uint32_t pending)
{
    (void)pending;
    s_ticks++;
}

int main(void)
{
    static const uint8_t msg[3] = { 'a', 'b', 'c' };
    uint8_t digest[32];
    uint32_t satp, root, i;
    int rc;

    bao_init();
    mini_printf("\r\nmmu_svc: running in supervisor mode\r\n");

    sha256_init();
    rc = sha256_hash(msg, 3, digest);
    mini_printf("sha256 rc %d: ", rc);
    for (i = 0; i < 32u; i++)
        mini_printf("%02x", digest[i]);
    mini_printf("\r\n");

    trng_init();
    mini_printf("trng words differ: %u\r\n", (unsigned)(trng_random() != trng_random()));

    irq_init();
    irq_set_handler(IRQ_TIMER0, on_timer0);
    irq_enable(IRQ_TIMER0);
    timer0_start_periodic_ms(1);
    delay_ms(20);
    timer0_stop();
    mini_printf("timer0 interrupts in supervisor mode: %u\r\n", (unsigned)(s_ticks >= 10u));

    __asm__ volatile ("csrr %0, satp" : "=r"(satp));
    root = (satp & 0x3FFFFFu) << 12;
    mini_printf("satp 0x%08x, root at 0x%08x\r\n", (unsigned)satp, (unsigned)root);
    mini_printf("root entry for SRAM is a pointer: %u\r\n",
                (unsigned)((((volatile uint32_t *)root)[0x61000000u >> 22] & 0xFu) == 1u));

    mini_printf("mmu_svc: passing the page table as the digest buffer\r\n");
    rc = sha256_hash(msg, 3, (uint8_t *)_mmu_tables_start);
    mini_printf("mmu_svc: FAIL, the call returned %d\r\n", rc);
    for (;;) {
    }
}
