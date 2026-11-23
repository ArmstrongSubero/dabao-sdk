/*
 * bao_mmu_stubs.S: supervisor side of the BAO_MMU machine mode services
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * One stub per line of bao_mmu_services.h, under the SDK function's own
 * name. The caller's arguments are already in a0 to a7, so a stub only
 * loads its service number into t0 and executes ecall; machine mode
 * returns the result in a0. t0 is a temporary in the calling convention,
 * so nothing has to be saved.
 */

#include "bao_mmu_services.h"

    .section .text.bao_mmu_stubs, "ax"
    .option push
    .option norvc

    .set bao_svc_n, 0

#define BAO_MMU_STUB(name, group, rd, wr, bytes, words) \
    .globl name;                            \
    .type name, @function;                  \
    .balign 4;                              \
name:                                       \
    li t0, bao_svc_n;                       \
    ecall;                                  \
    ret;                                    \
    .size name, . - name;                   \
    .set bao_svc_n, bao_svc_n + 1;

BAO_MMU_SERVICES(BAO_MMU_STUB)

    .globl bao_mmu_service_count
    .set bao_mmu_service_count, bao_svc_n

    .option pop
