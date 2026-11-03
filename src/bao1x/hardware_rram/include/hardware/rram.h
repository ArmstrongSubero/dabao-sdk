/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * ReRAM (RRAM) non-volatile storage driver for the Baochip-1x.
 *
 * The Baochip-1x has 4 MB of on-chip ReRAM at 0x60000000. It retains data
 * across power cycles and can be read as ordinary memory (just dereference
 * a pointer). Writes require a special command sequence through the RRC
 * controller and a VexRiscv data cache flush.
 *
 * The bottom of RRAM holds boot0 and boot1, and the top holds the one way
 * counters and key slots. A write into either can destroy the board, so
 * this driver refuses both. Everything in between, the program image and
 * the runtime storage above it, may be written. Overwriting the running
 * image only makes boot1 refuse it at the next boot, which the REPL can
 * fix, so that is allowed and left to the caller's judgement.
 *
 * Write granularity is 32 bytes (one page). The driver handles arbitrary
 * addresses and lengths by doing read-modify-write on partial pages. Every
 * write is verified by readback.
 *
 * ADDRESS SAFETY
 *
 * Three layers, outermost first.
 *
 *   compile time  A call with a constant address in boot0, boot1 or the
 *                 security region does not build. See the guards further
 *                 down. This is the one that matters, because it removes
 *                 the mistake from the binary rather than reporting it at
 *                 run time.
 *
 *   run time      A computed address outside the writable range returns -1
 *                 and writes nothing. The checks are written so that no
 *                 address plus length can wrap around 32 bits.
 *
 *   hardware      Interrupts are masked across the stage and commit
 *                 sequence in write_page_raw, because the controller
 *                 commits to the last address touched and an interrupt
 *                 handler executing from RRAM counts as a touch.
 *
 * Writing into boot0 or boot1 is not a recoverable mistake. Every recovery
 * mechanism on this part (the PROG button, USB mass storage, the serial
 * REPL) is implemented inside boot1, so a board with a damaged boot1 has no
 * way back in.
 */

#ifndef _HARDWARE_RRAM_H
#define _HARDWARE_RRAM_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RRAM_BASE           0x60000000UL
#define RRAM_SIZE           0x00400000UL    /* 4 MB */
#define RRAM_PAGE_SIZE      32              /* bytes per page */

/* Layout, for reference and for the guards below. */
#define RRAM_BOOT0_BASE     0x60000000UL
#define RRAM_BOOT1_BASE     0x60020000UL
#define RRAM_PROGRAM_BASE   0x60060000UL
#define RRAM_PROGRAM_TOP    0x60160000UL

/*
 * Runtime storage, the region link.ld keeps the image out of. Programs
 * that store data at run time should use this range.
 */
#define RRAM_USER_BASE      0x60160000UL
#define RRAM_USER_TOP       0x603DA000UL    /* one way counters above this */
#define RRAM_USER_SIZE      (RRAM_USER_TOP - RRAM_USER_BASE)  /* ~2.5 MB */

/*
 * Writable range. Writes outside it are rejected: below is boot0 and boot1,
 * at and above RRAM_USER_TOP are the counters and key slots.
 */
#define RRAM_WRITE_BASE     RRAM_PROGRAM_BASE
#define RRAM_WRITE_TOP      RRAM_USER_TOP

/*
 * Read bytes from RRAM. This is just a memcpy from the RRAM address space,
 * but goes through volatile reads to bypass the data cache. Reads are
 * unrestricted; only writes are guarded.
 */
void rram_read(uint32_t addr, uint8_t *buf, uint32_t len);

/*
 * Write bytes to RRAM at any address and length.
 * Handles unaligned addresses and non-page-sized lengths by doing
 * read-modify-write on partial pages. Verifies each page by readback.
 *
 * addr: absolute RRAM address, must be within the writable range
 * data: source buffer
 * len:  number of bytes to write
 *
 * Returns 0 on success, -1 if the address is out of bounds, -2 if
 * verification failed after retries.
 */
int rram_write(uint32_t addr, const uint8_t *data, uint32_t len);

/*
 * Write a single 32-byte page. The address must be 32-byte aligned and
 * within the writable range.
 *
 * Returns 0 on success, -1 on error.
 */
int rram_write_page(uint32_t page_addr, const uint8_t *data);

/*
 * Erase a page by writing all 0xFF. RRAM does not require a separate erase
 * step (any value can be written at any time), but this is provided for
 * compatibility with EEPROM-style usage patterns.
 */
int rram_erase_page(uint32_t page_addr);

/* ---------------------------------------------------------------------- */
/* Compile-time address guards                                            */
/* ---------------------------------------------------------------------- */
/*
 * If the address is a compile-time constant and falls in boot0, boot1 or
 * the security region, the build fails with a message naming the region.
 *
 * How it works: the declarations below carry the GCC error attribute, so
 * referencing one is a hard error unless the optimiser has already deleted
 * the call. __builtin_constant_p is false for a computed address, and a
 * constant address inside the writable range makes the branch condition
 * false, so in both of those cases the call is removed before it can
 * trigger. Only a constant address in a forbidden region survives.
 *
 * This needs optimisation on to fold the branches. -Os and -O2 are fine.
 * At -O0 the guards silently do nothing and only the run-time check
 * applies, which is another reason not to ship -O0 builds.
 *
 * rram.c defines RRAM_DRIVER_IMPL before including this header so the
 * macro at the bottom does not rewrite the function definitions.
 */

#if defined(__GNUC__) && !defined(RRAM_DRIVER_IMPL)

extern void __rram_write_target_is_bootloader(void)
    __attribute__((error(
        "rram_write() target is inside boot0 or boot1 (below 0x60060000). "
        "Writing there permanently bricks the board: PROG, USB and the "
        "serial REPL all live inside boot1. Writes start at "
        "RRAM_WRITE_BASE (0x60060000), runtime storage at RRAM_USER_BASE "
        "(0x60160000).")));

extern void __rram_write_target_is_security(void)
    __attribute__((error(
        "rram_write() target is at or above RRAM_USER_TOP (0x603DA000). "
        "That region holds the one-way counters and key slots. Writing "
        "there is irreversible.")));

static inline void __rram_check_addr(uint32_t addr)
{
    if (__builtin_constant_p(addr)) {
        if (addr < RRAM_WRITE_BASE) {
            __rram_write_target_is_bootloader();
        } else if (addr >= RRAM_WRITE_TOP) {
            __rram_write_target_is_security();
        }
    }
}

static inline int __rram_write_guarded(uint32_t addr, const uint8_t *data,
                                       uint32_t len)
{
    __rram_check_addr(addr);
    return rram_write(addr, data, len);
}

static inline int __rram_write_page_guarded(uint32_t page_addr,
                                            const uint8_t *data)
{
    __rram_check_addr(page_addr);
    return rram_write_page(page_addr, data);
}

static inline int __rram_erase_page_guarded(uint32_t page_addr)
{
    __rram_check_addr(page_addr);
    return rram_erase_page(page_addr);
}

/* Call sites are unchanged; these route through the guards. Wrap a call in
 * parentheses, as in (rram_write)(a, b, c), to bypass a guard deliberately. */
#define rram_write(a, d, l)     __rram_write_guarded((a), (d), (l))
#define rram_write_page(a, d)   __rram_write_page_guarded((a), (d))
#define rram_erase_page(a)      __rram_erase_page_guarded((a))

#endif /* __GNUC__ && !RRAM_DRIVER_IMPL */

#ifdef __cplusplus
}
#endif

#endif