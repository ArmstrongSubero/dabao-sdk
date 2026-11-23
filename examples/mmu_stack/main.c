/*
 * main.c: a runaway recursion under the BAO_MMU sandbox
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Built with the SDK's BAO_MMU sandbox (the file bao_mmu next to this one
 * selects it). main runs in supervisor mode with its 8 KB stack at
 * 0x6101D000 to 0x6101F000 and an unmapped guard page below it. The
 * recursion never returns, so the stack runs down into the guard page and
 * the first push there must end in the sandbox's fault report: a store
 * page fault at an address in 0x6101C000 to 0x6101CFFF, then a halt.
 *
 * Every store stays inside the main stack and its guard page even if the
 * page table were not in force: each level checks its own frame address
 * and stops 3 KB into the guard page, and says so.
 */

#include "bao.h"

#ifndef BAO_MMU
#error "this program needs the BAO_MMU sandbox: keep the bao_mmu file next to it"
#endif

#define STOP_BELOW ((uint32_t)_main_stack_bottom - 0xC00u)

extern char _main_stack_bottom[], _main_stack_top[];

static volatile uint32_t s_depth;
static volatile uint32_t s_max_depth = 0xFFFFFFFFu;

static uint32_t dive(uint32_t n)
{
    volatile uint32_t frame[12];
    uint32_t i;

    if ((uint32_t)__builtin_frame_address(0) < STOP_BELOW) {
        mini_printf("mmu_stack: FAIL, depth %u reached 0x%08x without a fault\r\n",
                    (unsigned)n, (unsigned)(uint32_t)__builtin_frame_address(0));
        for (;;) {
        }
    }
    for (i = 0; i < 12u; i++)
        frame[i] = n + i;
    s_depth = n;
    if ((n & 31u) == 0u)
        mini_printf("depth %u\r\n", (unsigned)n);
    if (n >= s_max_depth)
        return frame[0];
    return dive(n + 1u) + frame[n % 12u];
}

int main(void)
{
    bao_init();
    mini_printf("\r\nmmu_stack: main stack 0x%08x to 0x%08x, running in supervisor mode\r\n",
                (unsigned)(uint32_t)_main_stack_bottom, (unsigned)(uint32_t)_main_stack_top);
    mini_printf("mmu_stack: recursing until the guard page stops it\r\n");
    mini_printf("mmu_stack: returned %u\r\n", (unsigned)dive(1u));
    mini_printf("mmu_stack: FAIL, recursion returned\r\n");
    for (;;) {
    }
}
