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

/*
 * Privilege level of SDK code.
 *
 * Without BAO_MMU everything runs in machine mode. With BAO_MMU (see
 * src/mmu/bao_mmu.h) main and the SDK drivers it calls run in supervisor
 * mode, and the same code uses the supervisor views: sstatus.SIE for
 * critical sections, sie.SEIE and the VexRiscv SIM and SIP arrays (0x9C0,
 * 0xDC0) for interrupts, and the user cycle counter, which this core lets
 * every mode read. All of these are legal in machine mode as well.
 */
#ifdef BAO_MMU
#define BAO_CSR_STATUS      "sstatus"
#define BAO_CSR_IE          "sie"
#define BAO_CSR_CAUSE       "scause"
#define BAO_CSR_XIM         "0x9C0"
#define BAO_CSR_XIP         "0xDC0"
#define BAO_CSR_CYCLE       "cycle"
#define BAO_CSR_CYCLEH      "cycleh"
#define BAO_STATUS_IE_IMM   "0x2"
#define BAO_STATUS_IE       0x2u
#define BAO_EXT_IE_BIT      9
#define BAO_CAUSE_EXT_IRQ   0x80000009u
#else
#define BAO_CSR_STATUS      "mstatus"
#define BAO_CSR_IE          "mie"
#define BAO_CSR_CAUSE       "mcause"
#define BAO_CSR_XIM         "0xBC0"
#define BAO_CSR_XIP         "0xFC0"
#define BAO_CSR_CYCLE       "mcycle"
#define BAO_CSR_CYCLEH      "mcycleh"
#define BAO_STATUS_IE_IMM   "0x8"
#define BAO_STATUS_IE       0x8u
#define BAO_EXT_IE_BIT      11
#define BAO_CAUSE_EXT_IRQ   0x8000000Bu
#endif

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
 * With BAO_MMU the RRAM write driver runs in machine mode behind an ecall,
 * where machine interrupts are already masked, and these calls work on
 * sstatus.SIE for supervisor code.
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
    __asm__ volatile ("csrrci %0, " BAO_CSR_STATUS ", " BAO_STATUS_IE_IMM
                      : "=r" (state) :: "memory");
    return state;
}

static inline void critical_exit(uint32_t state)
{
    __asm__ volatile ("csrw " BAO_CSR_STATUS ", %0" :: "r" (state) : "memory");
}

#define BAO_OK              0
#define BAO_ERROR           (-1)
#define BAO_ERROR_TIMEOUT   (-2)

#endif