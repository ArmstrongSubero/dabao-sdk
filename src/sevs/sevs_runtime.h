/**
 * @file sevs_runtime.h
 * @brief SEVS runtime support: assertions, invariants, fault handlers.
 *
 * This header is part of the Subero Embedded Verification Standard (SEVS).
 * Travels with SEVS-compliant projects. Provides a uniform assertion macro
 * across all SEVS projects so that JPL Power of Ten Rule 5 (assertion
 * density) and Rule 7 (parameter validation) can be enforced consistently.
 *
 * Design principles:
 *   1. Assertions are ALWAYS compiled. They are not removed by NDEBUG.
 *      Production firmware retains safety checks.
 *   2. The ACTION taken on failure is platform-defined via
 *      sevs_assert_fail(). Each project links its own implementation.
 *   3. Three severities: ASSERT (recoverable bug), INVARIANT (state
 *      corruption, halt advised), CHECK (diagnostic, log only).
 *   4. No printf, no malloc inside the assertion path. Suitable for
 *      bare-metal, ISR context, post-fault handlers.
 *
 * @copyright (c) 2026 Armstrong Subero. SEVS reference implementation.
 */

#ifndef SEVS_RUNTIME_H
#define SEVS_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Failure categories                                                       */
/* ------------------------------------------------------------------------ */

typedef enum {
    SEVS_FAIL_ASSERT     = 0,  /* parameter or precondition violated */
    SEVS_FAIL_INVARIANT  = 1,  /* internal state corruption */
    SEVS_FAIL_CHECK      = 2,  /* diagnostic, recoverable */
    SEVS_FAIL_REACHED    = 3,  /* unreachable code reached */
} sevs_fail_kind_t;

/* ------------------------------------------------------------------------ */
/* Failure handler                                                          */
/*                                                                          */
/* Each project implements this. The default behavior is platform-specific. */
/* ------------------------------------------------------------------------ */

void sevs_assert_fail(sevs_fail_kind_t kind,
                      const char       *expr,
                      const char       *file,
                      int               line,
                      const char       *func);

/* ------------------------------------------------------------------------ */
/* Compile-time configuration                                               */
/* ------------------------------------------------------------------------ */

/* SEVS_ASSERT_LEVEL controls which assertions are evaluated at runtime:
 *
 *   3 (default for dev):  ASSERT, INVARIANT, CHECK all active
 *   2 (default for rel):  ASSERT, INVARIANT active; CHECK compiled out
 *   1 (size-constrained): INVARIANT active only
 *   0 (forbidden):        all compiled out (FAILS SEVS-Core)
 *
 * SEVS does NOT permit level 0 in shipping code. Level 1 requires explicit
 * deviation per SEVS Section 9.
 */
#ifndef SEVS_ASSERT_LEVEL
#  define SEVS_ASSERT_LEVEL 3
#endif

#if SEVS_ASSERT_LEVEL < 1 || SEVS_ASSERT_LEVEL > 3
#  error "SEVS_ASSERT_LEVEL must be 1, 2, or 3."
#endif

/* GCC/Clang-friendly function name fallback */
#if defined(__GNUC__) || defined(__clang__)
#  define SEVS_FUNC_ __extension__ __PRETTY_FUNCTION__
#else
#  define SEVS_FUNC_ ((const char *)0)
#endif

/* ------------------------------------------------------------------------ */
/* Public macros                                                            */
/* ------------------------------------------------------------------------ */

/**
 * SEVS_ASSERT(expr): parameter / precondition / postcondition check.
 * Always compiled (level >= 2). Calls sevs_assert_fail on violation.
 * Use for parameter validation, return-value checks, post-condition checks.
 */
#if SEVS_ASSERT_LEVEL >= 2
#  define SEVS_ASSERT(expr)                                            \
      ((expr) ? (void)0                                                \
              : sevs_assert_fail(SEVS_FAIL_ASSERT, #expr,              \
                                 __FILE__, __LINE__, SEVS_FUNC_))
#else
#  define SEVS_ASSERT(expr) ((void)0)
#endif

/**
 * SEVS_INVARIANT(expr): internal-state invariant.
 * Always compiled (level >= 1). Most critical category. Failure indicates
 * state corruption; recovery typically requires halt or reset.
 */
#if SEVS_ASSERT_LEVEL >= 1
#  define SEVS_INVARIANT(expr)                                         \
      ((expr) ? (void)0                                                \
              : sevs_assert_fail(SEVS_FAIL_INVARIANT, #expr,           \
                                 __FILE__, __LINE__, SEVS_FUNC_))
#else
#  define SEVS_INVARIANT(expr) ((void)0)
#endif

/**
 * SEVS_CHECK(expr): diagnostic check, log-only on failure.
 * Compiled only at level 3. For things you want to verify in dev/test
 * but accept in production (e.g. weak preconditions you handle gracefully).
 */
#if SEVS_ASSERT_LEVEL >= 3
#  define SEVS_CHECK(expr)                                             \
      ((expr) ? (void)0                                                \
              : sevs_assert_fail(SEVS_FAIL_CHECK, #expr,               \
                                 __FILE__, __LINE__, SEVS_FUNC_))
#else
#  define SEVS_CHECK(expr) ((void)0)
#endif

/**
 * SEVS_UNREACHABLE(): mark code that should never execute.
 * Always compiled. If reached, calls fail handler with SEVS_FAIL_REACHED.
 */
#define SEVS_UNREACHABLE()                                             \
      sevs_assert_fail(SEVS_FAIL_REACHED, "unreachable",               \
                       __FILE__, __LINE__, SEVS_FUNC_)

/**
 * SEVS_STATIC_ASSERT(expr, msg): compile-time check.
 * Maps to _Static_assert in C11. No runtime cost.
 */
#define SEVS_STATIC_ASSERT(expr, msg) _Static_assert((expr), msg)

/**
 * SEVS_REQUIRE_NOT_NULL(p): convenience for the most common parameter
 * validation. Equivalent to SEVS_ASSERT((p) != NULL) but reads better
 * at function entry points.
 */
#define SEVS_REQUIRE_NOT_NULL(p) SEVS_ASSERT((p) != NULL)

/**
 * SEVS_REQUIRE_RANGE(v, lo, hi): convenience for range checks.
 * Inclusive on both ends.
 */
#define SEVS_REQUIRE_RANGE(v, lo, hi) \
    SEVS_ASSERT(((v) >= (lo)) && ((v) <= (hi)))

#ifdef __cplusplus
}
#endif

#endif /* SEVS_RUNTIME_H */
