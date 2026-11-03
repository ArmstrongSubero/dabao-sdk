/*
 * crt0.S: C runtime startup and trap entry for Baochip-1x
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Entry masks every interrupt before touching memory. boot1 can hand over
 * with mstatus.MIE set and events pending, and a trap taken while .bss is
 * still being zeroed would call through an uninitialised handler table.
 * Interrupts are only turned back on by irq_init() or timer0_start_*().
 *
 * The trap handler has its own stack, _trap_stack_top down to
 * _main_stack_top, laid out by the linker script. The register frame and
 * the C dispatcher's frames both live there, so an interrupt can never
 * overwrite main()'s stack however deep the handler's call chain goes.
 * Interrupts stay masked for the whole handler (hardware clears MIE on
 * entry and mret restores it), and an exception inside the handler halts
 * in trap_dispatch, so the trap stack is never entered twice.
 *
 * Frame layout, 36 words so the C stack below it stays 16 byte aligned:
 *   slot  0       ra
 *   slot  1       original sp
 *   slots 2..30   x3..x31
 *   slot 31       mepc
 *   slot 32       mstatus
 *   slots 33..35  padding
 */

#define FRAME_WORDS 36
#define FRAME_BYTES (FRAME_WORDS * 4)

    .section .text.start
    .global _start
    .type _start, @function

_start:
    csrci   mstatus, 0x8
    csrw    mie, zero
    csrw    0xBC0, zero

    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    la      sp, _main_stack_top

    la      t0, _trap_entry
    csrw    mtvec, t0

    la      t0, _bss_start
    la      t1, _bss_end
1:
    beq     t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:

    la      t0, _data_lma
    la      t1, _data_start
    la      t2, _data_end
    beq     t0, t1, 4f
3:
    beq     t1, t2, 4f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    j       3b
4:
    fence
    fence.i

    call    main

_hang:
    wfi
    j       _hang


    .section .text
    .align 4
    .global _trap_entry
    .type _trap_entry, @function

_trap_entry:
    csrw    mscratch, sp
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

    csrr    t0, mepc
    sw      t0, 31*4(sp)
    csrr    t0, mstatus
    sw      t0, 32*4(sp)
    csrr    t0, mscratch
    sw      t0,  1*4(sp)

    call    trap_dispatch

    la      sp, _trap_stack_top
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
