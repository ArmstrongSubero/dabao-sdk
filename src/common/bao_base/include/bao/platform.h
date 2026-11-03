/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _BAO_PLATFORM_H
#define _BAO_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "hardware/regs/addressmap.h"

typedef unsigned int uint;

#define REG32(addr)     (*(volatile uint32_t *)(uintptr_t)(addr))

static inline void memory_fence(void)
{
    __asm__ volatile ("fence" ::: "memory");
}

/*
 * Critical sections.
 *
 * The RRAM controller stages a write into a buffer and commits it to the
 * last address touched. An interrupt taken between the staging stores and
 * the commit will redirect the write to whatever address the handler
 * touches, and the handler executes from RRAM. Any sequence that drives
 * RRC_CR into write command mode must therefore run with mstatus.MIE
 * clear from the first staging store through the return to normal mode.
 *
 * csrrci clears MIE and returns the previous mstatus in one instruction,
 * so these nest. Always pass the value returned by critical_enter back to
 * critical_exit rather than unconditionally re-enabling.
 *
 *     uint32_t s = critical_enter();
 *     ...
 *     critical_exit(s);
 */
static inline uint32_t critical_enter(void)
{
    uint32_t state;
    __asm__ volatile ("csrrci %0, mstatus, 0x8" : "=r" (state) :: "memory");
    return state;
}

static inline void critical_exit(uint32_t state)
{
    __asm__ volatile ("csrw mstatus, %0" :: "r" (state) : "memory");
}

#define BAO_OK              0
#define BAO_ERROR           (-1)
#define BAO_ERROR_TIMEOUT   (-2)

#endif