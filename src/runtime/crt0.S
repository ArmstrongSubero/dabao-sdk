/*
 * crt0.S - C Runtime Startup for Baochip-1x
 *
 * This version includes a full trap handler that saves/restores all
 * registers and dispatches to a C interrupt handler.
 *
 * Scratch area for register saves is at the top of SRAM minus 512 bytes.
 * This keeps it separate from the main stack.
 */

    .section .text.start
    .global _start
    .type _start, @function

_start:
    /* Set global pointer (for linker relaxation) */
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    /* Set stack pointer to top of SRAM minus 512 (reserve scratch area) */
    la      sp, _stack_top
    addi    sp, sp, -512

    /* Install trap handler */
    la      t0, _trap_entry
    csrw    mtvec, t0

    /* Zero BSS section */
    la      t0, _bss_start
    la      t1, _bss_end
1:
    beq     t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:

    /* Copy .data from RRAM (LMA) to SRAM (VMA) */
    la      t0, _data_lma
    la      t1, _data_start
    la      t2, _data_end
3:
    beq     t1, t2, 4f
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    j       3b
4:

    /* Call main */
    call    main

    /* If main returns, halt */
_hang:
    wfi
    j       _hang


/* ================================================================== */
/*                        Trap Entry Point                             */
/* ================================================================== */
/*
 * When an interrupt or exception occurs, the CPU jumps here.
 * We save ALL 31 general-purpose registers (x1-x31) plus mepc
 * to a scratch area, then call the C dispatcher, then restore
 * everything and return with mret.
 *
 * The scratch area is at the top of SRAM (_stack_top).
 * We use mscratch to save/restore the original sp.
 */

    .align 4
    .global _trap_entry
    .type _trap_entry, @function

_trap_entry:
    /* Save original sp to mscratch */
    csrw    mscratch, sp

    /* Load scratch area pointer (top of SRAM) */
    la      sp, _stack_top

    /* Allocate 34 words: x1-x31 (31) + mepc (1) + mstatus (1) + orig_sp (1) */
    addi    sp, sp, -(34*4)

    /* Save all general-purpose registers except sp (x2) */
    sw      x1,   0*4(sp)      /* ra */
    /* x2 (sp) saved later from mscratch */
    sw      x3,   2*4(sp)      /* gp */
    sw      x4,   3*4(sp)      /* tp */
    sw      x5,   4*4(sp)      /* t0 */
    sw      x6,   5*4(sp)      /* t1 */
    sw      x7,   6*4(sp)      /* t2 */
    sw      x8,   7*4(sp)      /* s0/fp */
    sw      x9,   8*4(sp)      /* s1 */
    sw      x10,  9*4(sp)      /* a0 */
    sw      x11, 10*4(sp)      /* a1 */
    sw      x12, 11*4(sp)      /* a2 */
    sw      x13, 12*4(sp)      /* a3 */
    sw      x14, 13*4(sp)      /* a4 */
    sw      x15, 14*4(sp)      /* a5 */
    sw      x16, 15*4(sp)      /* a6 */
    sw      x17, 16*4(sp)      /* a7 */
    sw      x18, 17*4(sp)      /* s2 */
    sw      x19, 18*4(sp)      /* s3 */
    sw      x20, 19*4(sp)      /* s4 */
    sw      x21, 20*4(sp)      /* s5 */
    sw      x22, 21*4(sp)      /* s6 */
    sw      x23, 22*4(sp)      /* s7 */
    sw      x24, 23*4(sp)      /* s8 */
    sw      x25, 24*4(sp)      /* s9 */
    sw      x26, 25*4(sp)      /* s10 */
    sw      x27, 26*4(sp)      /* s11 */
    sw      x28, 27*4(sp)      /* t3 */
    sw      x29, 28*4(sp)      /* t4 */
    sw      x30, 29*4(sp)      /* t5 */
    sw      x31, 30*4(sp)      /* t6 */

    /* Save mepc */
    csrr    t0, mepc
    sw      t0, 31*4(sp)

    /* Save mstatus (we need to restore MIE state on exit) */
    csrr    t0, mstatus
    sw      t0, 32*4(sp)

    /* Save original sp from mscratch */
    csrr    t0, mscratch
    sw      t0,  1*4(sp)       /* slot 1 = original sp */

    /* Set up a proper C stack for the dispatcher.
     * Use the scratch area sp minus another 256 bytes for the C stack.
     * This lets the C handler call functions safely.
     */
    addi    sp, sp, -256

    /* Call the C dispatcher */
    call    trap_dispatch

    /* Restore sp to the register save area */
    la      sp, _stack_top
    addi    sp, sp, -(34*4)

    /* Restore mepc */
    lw      t0, 31*4(sp)
    csrw    mepc, t0

    /* Restore mstatus */
    lw      t0, 32*4(sp)
    csrw    mstatus, t0

    /* Restore all general-purpose registers */
    lw      x1,   0*4(sp)
    /* x2 (sp) restored last */
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

    /* Restore original sp LAST */
    lw      x2,   1*4(sp)

    /* Return from trap */
    mret