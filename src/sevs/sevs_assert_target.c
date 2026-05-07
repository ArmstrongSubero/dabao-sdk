/**
 * @file sevs_assert_target.c
 * @brief Bare-metal / RTOS reference implementation of sevs_assert_fail.
 *
 * @sevs-runtime-impl
 *
 * For RISC-V firmware (CH32V003, CH32V103, CH32V203, CH32V307, CH32H417,
 * Baochip-1x). Behavior on failure:
 *
 *   1. Disable interrupts (best-effort; project may override).
 *   2. Persist a record to a fixed RAM location for post-mortem analysis.
 *   3. Optionally emit a single line to a configured UART using a tiny
 *      bit-banged write that does not depend on driver state.
 *   4. Halt: trigger a watchdog reset on production builds, or spin in a
 *      dedicated trap loop on debug builds so a debugger can catch it.
 *
 * Project-specific hooks let each target customize behavior without
 * editing this file. Provide your own:
 *   - sevs_target_disable_irq()    weakly defined here as no-op
 *   - sevs_target_uart_write_str() weakly defined here as no-op
 *   - sevs_target_halt()           weakly defined here as while(1)
 *
 * The persistent crash record at SEVS_CRASH_RECORD_ADDR is the
 * post-mortem mechanism; firmware should check this region on boot and
 * report any crash from the previous run.
 *
 * @copyright (c) 2026 Armstrong Subero. SEVS reference implementation.
 */

#include "sevs_runtime.h"

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------------ */
/* Configuration                                                            */
/* ------------------------------------------------------------------------ */

/* Where the crash record lives in RAM. Survives soft reset. Override per
 * target; default points at a fixed address near top of RAM. */
#ifndef SEVS_CRASH_RECORD_ADDR
#  define SEVS_CRASH_RECORD_ADDR ((uintptr_t)0x20003F00U)
#endif

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
    /* default: no-op. RISC-V projects typically use:
     *   __asm__ volatile ("csrci mstatus, 0x8");
     * Override per project. */
}

SEVS_WEAK void sevs_target_uart_write_str(const char *s)
{
    (void)s;
    /* default: no-op. Override to emit to a debug UART. */
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

void sevs_assert_fail(sevs_fail_kind_t kind,
                      const char       *expr,
                      const char       *file,
                      int               line,
                      const char       *func)
{
    /* Step 1: disable interrupts, best-effort. */
    sevs_target_disable_irq();

    /* Step 2: persist crash record. */
    sevs_crash_record_t *rec =
        (sevs_crash_record_t *)SEVS_CRASH_RECORD_ADDR;
    rec->magic = SEVS_CRASH_MAGIC;
    rec->kind  = (uint32_t)kind;
    rec->line  = (uint32_t)line;
    copy_clipped(rec->expr, sizeof(rec->expr), expr);
    copy_clipped(rec->file, sizeof(rec->file), file);
    copy_clipped(rec->func, sizeof(rec->func), func);
    rec->reserved = 0U;

    /* Step 3: emit a one-line summary to debug UART, if project hook
     * provides one. Format: [SEVS-KIND] file:line: in func: (expr) */
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
    pos += fmt_dec(&msg[pos], line);

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
    sevs_crash_record_t *rec =
        (sevs_crash_record_t *)SEVS_CRASH_RECORD_ADDR;
    if (rec->magic != SEVS_CRASH_MAGIC) {
        return 0;
    }
    *out = *rec;
    rec->magic = 0U;  /* clear so next boot doesn't re-report */
    return 1;
}
