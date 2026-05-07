/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware True Random Number Generator API.
 */

#ifndef _HARDWARE_TRNG_H
#define _HARDWARE_TRNG_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the TRNG subsystem. */
void trng_init(void);

/*
 * Generate word_count random 32-bit words into buf.
 * Returns 0 on success, negative on error (timeout).
 */
int trng_generate(uint32_t *buf, uint32_t word_count);

/* Convenience: get a single random 32-bit value. */
uint32_t trng_random(void);

#ifdef __cplusplus
}
#endif

#endif
