/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * bao_forbidden.c: run time refusal for BAO_READ32 and BAO_WRITE32.
 *
 * bao_forbidden.h turns a constant forbidden address into a compile error.
 * A computed one can only be caught here, at the moment it is used. The
 * access is refused, never performed, and a line goes out on the console.
 *
 * Weak, so a project can replace it, for example to halt instead.
 */

#include <stdint.h>
#include "bao/bao_forbidden.h"
#include "bao/stdlib.h"

/** @brief Refuse an access to a forbidden address.
 *  @param[in] addr     The address that was asked for.
 *  @param[in] is_write 1 for a write, 0 for a read.
 *  @return Nonzero, which tells the caller not to perform the access.
 *  @req REQ-DABAO-FORBID-0001 */
__attribute__((weak)) int bao_guard_violation(uint32_t addr, int is_write)
{
    mini_printf("blocked %s at 0x%08x\r\n", is_write ? "write" : "read",
                (unsigned)addr);
    return 1;
}
