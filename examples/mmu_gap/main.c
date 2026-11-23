/*
 * main.c: a store into the unmapped SRAM gap under the BAO_MMU sandbox
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Built with the SDK's BAO_MMU sandbox (the file bao_mmu next to this one
 * selects it). Supervisor data starts at 0x61021000 and ends a few KB
 * later; nothing from there to the end of SRAM at 0x61200000 is mapped. The program first
 * stores to its own .bss, which must work, then stores to 0x61100000 in
 * the gap, which must end in the sandbox's fault report (store page fault
 * at 0x61100000) and a halt. If the store went through it would land in
 * unused SRAM.
 */

#include "bao.h"

#ifndef BAO_MMU
#error "this program needs the BAO_MMU sandbox: keep the bao_mmu file next to it"
#endif

#define GAP_ADDR 0x61100000u

extern char _s_data_end[];

static volatile uint32_t s_mine;

int main(void)
{
    bao_init();
    mini_printf("\r\nmmu_gap: supervisor data ends at 0x%08x\r\n",
                (unsigned)(uint32_t)_s_data_end);
    s_mine = 0x5A5A1234u;
    mini_printf("mmu_gap: store to .bss read back 0x%08x\r\n", (unsigned)s_mine);
    mini_printf("mmu_gap: storing to 0x%08x\r\n", (unsigned)GAP_ADDR);
    *(volatile uint32_t *)GAP_ADDR = 0x12345678u;
    mini_printf("mmu_gap: FAIL, the store went through\r\n");
    for (;;) {
    }
}
