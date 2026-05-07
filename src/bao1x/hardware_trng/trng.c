/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * TRNG driver for the Baochip-1x SCE (Secure Crypto Engine).
 * Extracted from working main_trng.c, verified on Dabao hardware.
 */

#include "hardware/trng.h"
#include "bao/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sce.h"
#include "sevs_runtime.h"

/** @brief Maximum word count per TRNG request. */
#define TRNG_MAX_WORDS 256

/** @brief TRNG generation timeout in milliseconds. */
#define TRNG_TIMEOUT_MS 1000

/** @brief Initialize the TRNG subsystem.
 *  @req REQ-DABAO-TRNG-0001 */
void trng_init(void)
{
    SEVS_ASSERT(TRNG_MAX_WORDS > 0);
    SEVS_INVARIANT(TRNG_TIMEOUT_MS > 0);
    FIFO5_ENABLE();
    memory_fence();
}

/** @brief Generate random words from the hardware TRNG.
 *  @param[out] buf        Output buffer for random words.
 *  @param[in]  word_count Number of 32-bit words to generate (1-256).
 *  @return 0 on success, -1 on invalid count, -2 on timeout.
 *  @req REQ-DABAO-TRNG-0002 */
int trng_generate(uint32_t *buf, uint32_t word_count)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    SEVS_ASSERT(word_count > 0);
    SEVS_ASSERT(word_count <= TRNG_MAX_WORDS);

    if (word_count == 0 || word_count > TRNG_MAX_WORDS) {
        return -1;
    }

    FIFO5_CLEAR();
    memory_fence();

    /* Configure TRNG */
    TRNG_CRSRC    = 0xFFFF;       /* all ring oscillator sources */
    TRNG_CRANA    = 0xFFFF;
    TRNG_OPT      = 0x10040;     /* RNG B output, generate 1024 bytes */
    TRNG_POSTPROC = 0xF821;      /* post-processing with health test */
    TRNG_CHAIN0   = 0xFFFFFFFE;
    TRNG_CHAIN1   = 0xFFFFFFFC;
    memory_fence();

    /* Start */
    TRNG_AR = 0x5A;
    memory_fence();

    /* Wait for enough words with timeout */
    uint64_t t0 = millis();
    for (int s_poll = 0; s_poll < 10000000 && FIFO5_COUNT() < word_count; s_poll++) {
        if (millis() - t0 > TRNG_TIMEOUT_MS) {
            TRNG_AR = 0xA5;
            return -2;
        }
    }

    /* Read from buffer B */
    for (uint32_t i = 0; i < word_count; i++) {
        buf[i] = RNG_BUF_B[i];
    }

    /* Stop and reset for next call */
    TRNG_AR = 0xA5;
    FIFO5_DISABLE();
    memory_fence();
    FIFO5_ENABLE();

    return 0;
}

/** @brief Generate a single random 32-bit word.
 *  @return Random value from hardware TRNG.
 *  @req REQ-DABAO-TRNG-0003 */
uint32_t trng_random(void)
{
    uint32_t val = 0;
    SEVS_ASSERT(sizeof(val) == 4);
    SEVS_INVARIANT(TRNG_MAX_WORDS >= 1);
    trng_generate(&val, 1);
    return val;
}
