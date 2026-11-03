/**
 * @file sevs_assert_target.c
 * @brief Bare-metal / RTOS reference implementation of sevs_assert_fail.
 *
 * @sevs-runtime-impl
 *
 * For RISC-V firmware (CH32V003, CH32V103, CH32V203, CH32V307, CH32H417,
 * Baochip-1x). Behavior on failure:
 *
 *   1. Disable interrupts.
 *   2. Persist a record to a linker-reserved RAM location for post-mortem
 *      analysis.
 *   3. Emit a single line to a configured UART.
 *   4. Halt.
 *
 * Project-specific hooks let each target customize behavior without
 * editing this file. Provide your own:
 *   - sevs_target_disable_irq()    defined here for RISC-V
 *   - sevs_target_uart_write_str() weakly defined here, writes to UART2
 *   - sevs_target_halt()           weakly defined here as while(1)
 *
 * A no-op uart hook makes every assertion in the SDK a silent stop, which
 * is the worst possible failure mode for a safety framework. Boards must
 * provide a real one. See sevs_hooks_bao1x.c for the Baochip-1x version.
 *
 * The crash record lives in its own linker section so that no other object
 * can be placed over it. A hardcoded absolute address will eventually land
 * inside somebody's .bss.
 *
 * @copyright (c) 2026 Armstrong Subero. SEVS reference implementation.
 */

#include "sevs_runtime.h"
#include "hardware/uart.h"
#include "boards/dabao.h"

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------------ */
/* Configuration                                                            */
/* ------------------------------------------------------------------------ */

/* Magic value identifying a valid crash record. */
#define SEVS_CRASH_MAGIC ((uint32_t)0x53455653U)  /* "SEVS" */

/* ------------------------------------------------------------------------ */
/* Crash record                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint32_t magic;
    uint32_t kind;
    uint32_t line;
    /* Truncated strings stored inline; longer values are clipped. */
    char     expr[64];
    char     file[64];
    char     func[48];
    uint32_t reserved;
} sevs_crash_record_t;

SEVS_STATIC_ASSERT(sizeof(sevs_crash_record_t) <= 192,
                   "crash record must fit in reserved RAM slot");

/*
 * Placed by the linker into .sevs_crash, a NOLOAD section the startup code
 * must not clear. If a target has no such section defined, override
 * SEVS_CRASH_RECORD_ADDR at build time and this definition is skipped.
 */
#ifdef SEVS_CRASH_RECORD_ADDR
#  define sevs_crash_slot (*(sevs_crash_record_t *)(uintptr_t)SEVS_CRASH_RECORD_ADDR)
#else
static sevs_crash_record_t sevs_crash_record
    __attribute__((section(".sevs_crash"), used));
#  define sevs_crash_slot sevs_crash_record
#endif

/* ------------------------------------------------------------------------ */
/* Weak hooks - projects override                                           */
/* ------------------------------------------------------------------------ */

#if defined(__GNUC__) || defined(__clang__)
#  define SEVS_WEAK __attribute__((weak))
#else
#  define SEVS_WEAK
#endif

SEVS_WEAK void sevs_target_disable_irq(void)
{
    /* RISC-V: clear mstatus.MIE. Every SEVS target is RISC-V. */
    __asm__ volatile ("csrci mstatus, 0x8" ::: "memory");
}

/* Studio does not compile sevs_hooks_bao1x.c, so the Baochip default
 * writes to the board console itself. A strong override still wins. */
SEVS_WEAK void sevs_target_uart_write_str(const char *s)
{
    size_t n = 0U;

    if (s == NULL) {
        return;
    }
    while (n < 256U && s[n] != '\0') {
        n++;
    }
    if (n != 0U) {
        uart_write(BAO_DEFAULT_UART, (const uint8_t *)s, (uint32_t)n);
    }
}

SEVS_WEAK void sevs_target_halt(void)
{
    /* default: spin. Production firmware should override to trigger a
     * watchdog reset after persisting telemetry. */
    for (;;) {
        /* Some toolchains optimize empty loops away; force a barrier. */
        __asm__ volatile ("" ::: "memory");
    }
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static void copy_clipped(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (src == NULL) {
        if (cap > 0U) { dst[0] = '\0'; }
        return;
    }
    for (i = 0U; i + 1U < cap && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static const char *kind_name(sevs_fail_kind_t k)
{
    switch (k) {
        case SEVS_FAIL_ASSERT:    return "ASSERT";
        case SEVS_FAIL_INVARIANT: return "INVARIANT";
        case SEVS_FAIL_CHECK:     return "CHECK";
        case SEVS_FAIL_REACHED:   return "UNREACHABLE";
        default:                  return "UNKNOWN";
    }
}

/* Tiny integer formatter for embedded use: writes decimal v into buf and
 * returns the length. buf must hold at least 12 bytes. */
static size_t fmt_dec(char *buf, int v)
{
    char    tmp[12];
    size_t  n = 0U;
    int     neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) { tmp[n++] = '0'; }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    size_t pos = 0U;
    if (neg) { buf[pos++] = '-'; }
    while (n > 0U) {
        buf[pos++] = tmp[--n];
    }
    return pos;
}

/* ------------------------------------------------------------------------ */
/* The handler                                                              */
/* ------------------------------------------------------------------------ */

/*
 * Reentrancy guard. The uart hook calls driver code, and driver code
 * contains SEVS_ASSERT. Without this, one assertion inside the console
 * path recurses until the stack is gone.
 */
static volatile uint32_t sevs_in_handler = 0U;

void sevs_assert_fail(sevs_fail_kind_t kind,
                      const char       *expr,
                      const char       *file,
                      int               line,
                      const char       *func)
{
    /* Step 1: disable interrupts. The previous mstatus.MIE state is kept
     * so that a recoverable SEVS_CHECK can hand it back on the way out;
     * otherwise one failed check leaves the whole program running with
     * interrupts masked (timers, UART RX and the tick all stop). */
    uint32_t saved_mstatus;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(saved_mstatus) :: "memory");
    sevs_target_disable_irq();

    /* Nested failure: the record from the first one is the useful one.
     * Do not touch it, do not try the console again, just stop. */
    if (sevs_in_handler != 0U) {
        for (;;) {
            __asm__ volatile ("" ::: "memory");
        }
    }
    sevs_in_handler = 1U;

    /* Step 2: persist crash record. */
    sevs_crash_record_t *rec = &sevs_crash_slot;
    rec->magic = SEVS_CRASH_MAGIC;
    rec->kind  = (uint32_t)kind;
    rec->line  = (uint32_t)line;
    copy_clipped(rec->expr, sizeof(rec->expr), expr);
    copy_clipped(rec->file, sizeof(rec->file), file);
    copy_clipped(rec->func, sizeof(rec->func), func);
    rec->reserved = 0U;

    /* Step 3: emit a one-line summary to debug UART.
     * Format: [SEVS-KIND] file:line: in func: (expr) */
    char    msg[256];
    size_t  pos = 0U;
    const char *prefix = "[SEVS-";
    const char *kn     = kind_name(kind);

    /* Manual concatenation to avoid stdio. */
    while (*prefix != '\0' && pos + 1U < sizeof(msg)) {
        msg[pos++] = *prefix++;
    }
    while (*kn != '\0' && pos + 1U < sizeof(msg)) {
        msg[pos++] = *kn++;
    }
    if (pos + 2U < sizeof(msg)) { msg[pos++] = ']'; msg[pos++] = ' '; }

    if (file != NULL) {
        const char *p = file;
        while (*p != '\0' && pos + 1U < sizeof(msg)) { msg[pos++] = *p++; }
    }
    if (pos + 1U < sizeof(msg)) { msg[pos++] = ':'; }
    if (pos + 12U < sizeof(msg)) { pos += fmt_dec(&msg[pos], line); }

    if (func != NULL) {
        const char *p = ": in ";
        while (*p != '\0' && pos + 1U < sizeof(msg)) { msg[pos++] = *p++; }
        p = func;
        while (*p != '\0' && pos + 1U < sizeof(msg)) { msg[pos++] = *p++; }
    }

    if (pos + 4U < sizeof(msg)) {
        msg[pos++] = ':'; msg[pos++] = ' '; msg[pos++] = '(';
    }
    if (expr != NULL) {
        const char *p = expr;
        while (*p != '\0' && pos + 1U < sizeof(msg)) { msg[pos++] = *p++; }
    }
    if (pos + 3U < sizeof(msg)) {
        msg[pos++] = ')'; msg[pos++] = '\n'; msg[pos] = '\0';
    } else {
        msg[sizeof(msg) - 1U] = '\0';
    }

    sevs_target_uart_write_str(msg);

    /* Step 4: action by kind. CHECK is recoverable; others halt. */
    if (kind == SEVS_FAIL_CHECK) {
        sevs_in_handler = 0U;
        if ((saved_mstatus & 0x8U) != 0U) {
            __asm__ volatile ("csrsi mstatus, 0x8" ::: "memory");
        }
        return;
    }

    sevs_target_halt();

    /* Should never reach. If the override returned, spin. */
    for (;;) {
        __asm__ volatile ("" ::: "memory");
    }
}

/* ------------------------------------------------------------------------ */
/* Boot-time crash record retrieval                                         */
/* ------------------------------------------------------------------------ */

/**
 * Call this early in main() (after RAM init, before normal logging starts).
 * If a previous run crashed, the record is preserved across soft reset and
 * can be read here, logged, and cleared.
 *
 * Returns 1 if a crash record was found and copied to *out, 0 otherwise.
 */
int sevs_target_pop_crash_record(sevs_crash_record_t *out)
{
    if (out == NULL) {
        return 0;
    }
    sevs_crash_record_t *rec = &sevs_crash_slot;
    if (rec->magic != SEVS_CRASH_MAGIC) {
        return 0;
    }
    *out = *rec;
    rec->magic = 0U;  /* clear so next boot doesn't re-report */
    return 1;
}