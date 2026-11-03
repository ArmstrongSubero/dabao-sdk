/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * bao_forbidden.h - regions no application may touch, enforced at build
 * time where possible and at run time otherwise.
 *
 * WHY THIS EXISTS
 *
 * A read only program with no writes, no SPI and no loop destroyed a
 * board. All it did was read the RRAM controller's own configuration
 * register at 0x40000000. The read did not return; the program stopped
 * part way through printing. The board has not booted since and never
 * will.
 *
 * That is a designed response, not a fault. rrc.sv drives a one way
 * counter in RRAM from hardware events, boot1 checks that counter
 * before it will run anything, and an application poking the RRAM
 * controller's configuration is exactly the signature a tamper counter
 * exists to record. The counter cannot be cleared. There is no
 * recovery, no reflash, no power cycle that helps.
 *
 * So the cost of one stray pointer into this region is a dead board.
 * Not a crash, not a hang, not a corrupted image. A dead board.
 *
 * WHAT THIS CATCHES AND WHAT IT DOES NOT
 *
 * A constant address becomes a compile error. That covers the case
 * that actually happened, someone writing a register definition by
 * hand from a datasheet or an RTL listing.
 *
 * A computed address is checked at run time and refused. That costs a
 * compare and cannot be folded away because the value is not known
 * until it is used.
 *
 * Neither catches a raw volatile pointer dereference written without
 * going through these macros. Nothing in a header can. That is what
 * the audit script is for: it sweeps the tree for literals in these
 * ranges regardless of how they are used.
 */

#ifndef BAO_FORBIDDEN_H
#define BAO_FORBIDDEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The regions                                                         */
/* ------------------------------------------------------------------ */

/*
 * RRAM controller configuration. rrc.sv, ahb_cr and ahb_sr at offsets
 * 0x00 to 0x0C. rrccr[31:16] is the one way counter event mask and
 * rrcfr carries the access error bits. This is the register block the
 * probe read, and reading it is what killed the board.
 */
#define BAO_FORBID_RRC_BASE     0x40000000UL
#define BAO_FORBID_RRC_END      0x40001000UL

/*
 * One way counters in RRAM. rrc.sv writes them itself:
 *   haddr_reg <= {20'h603D_B, 3'b111, oneway_counter_adr_ev}
 * Nothing in an application has any business here, reading or writing.
 */
#define BAO_FORBID_CTR_BASE     0x603DB000UL
#define BAO_FORBID_CTR_END      0x603DC000UL

/*
 * Security region in RRAM: key slots, ACLs, the IFR. Already guarded
 * against writes in rram.h; named here so reads are covered too and so
 * one header lists every forbidden range.
 */
#define BAO_FORBID_SEC_BASE     0x603DA000UL
#define BAO_FORBID_SEC_END      0x60400000UL

/*
 * The boot chain. boot0 at 0x60000000, boot1 at 0x60020000. An
 * application image starts at 0x60060000 and everything below that
 * belongs to the boot chain. Writing here bricks a board directly
 * rather than through a counter.
 */
#define BAO_FORBID_BOOT_BASE    0x60000000UL
#define BAO_FORBID_BOOT_END     0x60060000UL

#define BAO_IN_RANGE(a, lo, hi) ((uint32_t)(a) >= (lo) && (uint32_t)(a) < (hi))

#define BAO_ADDR_FORBIDDEN(a)                                   \
    (BAO_IN_RANGE((a), BAO_FORBID_RRC_BASE,  BAO_FORBID_RRC_END)  ||  \
     BAO_IN_RANGE((a), BAO_FORBID_CTR_BASE,  BAO_FORBID_CTR_END)  ||  \
     BAO_IN_RANGE((a), BAO_FORBID_SEC_BASE,  BAO_FORBID_SEC_END)  ||  \
     BAO_IN_RANGE((a), BAO_FORBID_BOOT_BASE, BAO_FORBID_BOOT_END))

/* ------------------------------------------------------------------ */
/* Guarded access                                                      */
/* ------------------------------------------------------------------ */

/*
 * Compile time refusal for a constant address, run time refusal for a
 * computed one.
 *
 * __builtin_constant_p folds to true only when the optimiser can see
 * the value, so this needs -Os or better to catch the constant case.
 * The runtime path is always present and always checked.
 */
extern void bao_forbidden_address(void)
    __attribute__((error(
        "This address is in a region that permanently destroys the part. "
        "Reading the RRAM controller configuration at 0x40000000, the one "
        "way counters at 0x603DB000, the security region at 0x603DA000, or "
        "the boot chain below 0x60060000, trips a tamper counter that "
        "cannot be cleared. boot1 refuses to boot afterwards. The board is "
        "not recoverable.")));

int  bao_guard_violation(uint32_t addr, int is_write);

#define BAO_READ32(addr)                                            \
    ({                                                              \
        uint32_t _a = (uint32_t)(addr);                             \
        if (__builtin_constant_p(_a) && BAO_ADDR_FORBIDDEN(_a))     \
            bao_forbidden_address();                                \
        (BAO_ADDR_FORBIDDEN(_a) && bao_guard_violation(_a, 0))      \
            ? 0u : *(volatile uint32_t *)_a;                        \
    })

#define BAO_WRITE32(addr, val)                                      \
    do {                                                            \
        uint32_t _a = (uint32_t)(addr);                             \
        if (__builtin_constant_p(_a) && BAO_ADDR_FORBIDDEN(_a))     \
            bao_forbidden_address();                                \
        if (!(BAO_ADDR_FORBIDDEN(_a) && bao_guard_violation(_a, 1))) \
            *(volatile uint32_t *)_a = (uint32_t)(val);             \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* BAO_FORBIDDEN_H */
