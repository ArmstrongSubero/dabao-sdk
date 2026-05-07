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

#define BAO_OK              0
#define BAO_ERROR           (-1)
#define BAO_ERROR_TIMEOUT   (-2)

#endif
