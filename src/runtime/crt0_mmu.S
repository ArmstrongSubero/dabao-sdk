/*
 * crt0_mmu.S: startup and trap entries for the BAO_MMU sandbox
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Used instead of crt0.S when an example is built with BAO_MMU. Startup
 * runs in machine mode exactly as crt0.S does (interrupts masked first,
 * .bss zeroed, .data copied, for both the supervisor and the machine
 * regions of bao1x_mmu.ld), then bao_mmu_init builds the page tables and
 * turns translation on, and mret enters main in supervisor mode on the
 * main stack at the bottom of SRAM. main returning lands in a supervisor
 * wfi loop.
 *
 * Two trap entries, each with the 36 word frame of crt0.S (ra, original
 * sp, x3 to x31, xepc, xstatus, padding):
 *
 * _m_trap_entry  mtvec. Every exception and the ecall services. MPRV is
 *                cleared before the first load or store, since a page
 *                fault inside a service arrives with it still set. The
 *                frame goes on the machine stack, which supervisor code
 *                has no mapping for, and bao_mmu_trap gets its address.
 *
 * _s_trap_entry  stvec. The delegated supervisor external interrupt. The
 *                frame goes on the supervisor trap stack and the SDK's
 *                trap_dispatch runs as it does in machine mode builds.
 */

#define FRAME_WORDS 36
#define FRAME_BYTES (FRAME_WORDS * 4)

#define MSTATUS_MPP     0x1800
#define MSTATUS_MPP_S   0x0800
#define MSTATUS_MPIE    0x0080
#define MSTATUS_MPRV    0x20000

    .section .text.entry, "ax"
    .global _entry
    .type _entry, @function
_entry:
    j       _start

    .section .text.start
    .global _start
    .type _start, @function

_start:
    csrci   mstatus, 0x8
    csrw    mie, zero
    csrw    0xBC0, zero
    csrw    0x9C0, zero
    csrw    satp, zero

    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    la      sp, _m_stack_top

    la      t0, _m_trap_entry
    csrw    mtvec, t0

    la      t0, _bss_start
    la      t1, _bss_end
1:
    beq     t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    la      t0, _m_bss_start
    la      t1, _m_bss_end
3:
    beq     t0, t1, 4f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       3b
4:
    la      t0, _data_lma
    la      t1, _data_start
    la      t2, _data_end
5:
    beq     t1, t2, 6f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    j       5b
6:
    la      t0, _m_data_lma
    la      t1, _m_data_start
    la      t2, _m_data_end
7:
    beq     t1, t2, 8f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    j       7b
8:
    fence
    fence.i

    call    bao_mmu_init

    la      t0, main
    csrw    mepc, t0
    li      t0, MSTATUS_MPP | MSTATUS_MPIE | MSTATUS_MPRV
    csrc    mstatus, t0
    li      t0, MSTATUS_MPP_S
    csrs    mstatus, t0
    la      sp, _main_stack_top
    la      ra, _s_hang
    mret

    .section .text
    .global _s_hang
    .type _s_hang, @function
_s_hang:
    wfi
    j       _s_hang


    .section .text
    .align 4
    .global _m_trap_entry
    .type _m_trap_entry, @function

_m_trap_entry:
    csrw    mscratch, t0
    li      t0, MSTATUS_MPRV
    csrc    mstatus, t0
    csrr    t0, mscratch
    csrw    mscratch, sp
    la      sp, _m_stack_top
    addi    sp, sp, -FRAME_BYTES

    sw      x1,   0*4(sp)
    sw      x3,   2*4(sp)
    sw      x4,   3*4(sp)
    sw      x5,   4*4(sp)
    sw      x6,   5*4(sp)
    sw      x7,   6*4(sp)
    sw      x8,   7*4(sp)
    sw      x9,   8*4(sp)
    sw      x10,  9*4(sp)
    sw      x11, 10*4(sp)
    sw      x12, 11*4(sp)
    sw      x13, 12*4(sp)
    sw      x14, 13*4(sp)
    sw      x15, 14*4(sp)
    sw      x16, 15*4(sp)
    sw      x17, 16*4(sp)
    sw      x18, 17*4(sp)
    sw      x19, 18*4(sp)
    sw      x20, 19*4(sp)
    sw      x21, 20*4(sp)
    sw      x22, 21*4(sp)
    sw      x23, 22*4(sp)
    sw      x24, 23*4(sp)
    sw      x25, 24*4(sp)
    sw      x26, 25*4(sp)
    sw      x27, 26*4(sp)
    sw      x28, 27*4(sp)
    sw      x29, 28*4(sp)
    sw      x30, 29*4(sp)
    sw      x31, 30*4(sp)

    csrr    t0, mepc
    sw      t0, 31*4(sp)
    csrr    t0, mstatus
    sw      t0, 32*4(sp)
    csrr    t0, mscratch
    sw      t0,  1*4(sp)

    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    mv      a0, sp
    call    bao_mmu_trap

    la      sp, _m_stack_top
    addi    sp, sp, -FRAME_BYTES

    lw      t0, 31*4(sp)
    csrw    mepc, t0
    lw      t0, 32*4(sp)
    csrw    mstatus, t0

    lw      x1,   0*4(sp)
    lw      x3,   2*4(sp)
    lw      x4,   3*4(sp)
    lw      x5,   4*4(sp)
    lw      x6,   5*4(sp)
    lw      x7,   6*4(sp)
    lw      x8,   7*4(sp)
    lw      x9,   8*4(sp)
    lw      x10,  9*4(sp)
    lw      x11, 10*4(sp)
    lw      x12, 11*4(sp)
    lw      x13, 12*4(sp)
    lw      x14, 13*4(sp)
    lw      x15, 14*4(sp)
    lw      x16, 15*4(sp)
    lw      x17, 16*4(sp)
    lw      x18, 17*4(sp)
    lw      x19, 18*4(sp)
    lw      x20, 19*4(sp)
    lw      x21, 20*4(sp)
    lw      x22, 21*4(sp)
    lw      x23, 22*4(sp)
    lw      x24, 23*4(sp)
    lw      x25, 24*4(sp)
    lw      x26, 25*4(sp)
    lw      x27, 26*4(sp)
    lw      x28, 27*4(sp)
    lw      x29, 28*4(sp)
    lw      x30, 29*4(sp)
    lw      x31, 30*4(sp)

    lw      x2,   1*4(sp)

    mret


    .section .text
    .align 4
    .global _s_trap_entry
    .type _s_trap_entry, @function

_s_trap_entry:
    csrw    sscratch, sp
    la      sp, _trap_stack_top
    addi    sp, sp, -FRAME_BYTES

    sw      x1,   0*4(sp)
    sw      x3,   2*4(sp)
    sw      x4,   3*4(sp)
    sw      x5,   4*4(sp)
    sw      x6,   5*4(sp)
    sw      x7,   6*4(sp)
    sw      x8,   7*4(sp)
    sw      x9,   8*4(sp)
    sw      x10,  9*4(sp)
    sw      x11, 10*4(sp)
    sw      x12, 11*4(sp)
    sw      x13, 12*4(sp)
    sw      x14, 13*4(sp)
    sw      x15, 14*4(sp)
    sw      x16, 15*4(sp)
    sw      x17, 16*4(sp)
    sw      x18, 17*4(sp)
    sw      x19, 18*4(sp)
    sw      x20, 19*4(sp)
    sw      x21, 20*4(sp)
    sw      x22, 21*4(sp)
    sw      x23, 22*4(sp)
    sw      x24, 23*4(sp)
    sw      x25, 24*4(sp)
    sw      x26, 25*4(sp)
    sw      x27, 26*4(sp)
    sw      x28, 27*4(sp)
    sw      x29, 28*4(sp)
    sw      x30, 29*4(sp)
    sw      x31, 30*4(sp)

    csrr    t0, sepc
    sw      t0, 31*4(sp)
    csrr    t0, sstatus
    sw      t0, 32*4(sp)
    csrr    t0, sscratch
    sw      t0,  1*4(sp)

    call    trap_dispatch

    la      sp, _trap_stack_top
    addi    sp, sp, -FRAME_BYTES

    lw      t0, 31*4(sp)
    csrw    sepc, t0
    lw      t0, 32*4(sp)
    csrw    sstatus, t0

    lw      x1,   0*4(sp)
    lw      x3,   2*4(sp)
    lw      x4,   3*4(sp)
    lw      x5,   4*4(sp)
    lw      x6,   5*4(sp)
    lw      x7,   6*4(sp)
    lw      x8,   7*4(sp)
    lw      x9,   8*4(sp)
    lw      x10,  9*4(sp)
    lw      x11, 10*4(sp)
    lw      x12, 11*4(sp)
    lw      x13, 12*4(sp)
    lw      x14, 13*4(sp)
    lw      x15, 14*4(sp)
    lw      x16, 15*4(sp)
    lw      x17, 16*4(sp)
    lw      x18, 17*4(sp)
    lw      x19, 18*4(sp)
    lw      x20, 19*4(sp)
    lw      x21, 20*4(sp)
    lw      x22, 21*4(sp)
    lw      x23, 22*4(sp)
    lw      x24, 23*4(sp)
    lw      x25, 24*4(sp)
    lw      x26, 25*4(sp)
    lw      x27, 26*4(sp)
    lw      x28, 27*4(sp)
    lw      x29, 28*4(sp)
    lw      x30, 29*4(sp)
    lw      x31, 30*4(sp)

    lw      x2,   1*4(sp)

    sret
