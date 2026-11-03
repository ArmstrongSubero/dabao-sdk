/*
 * trap_regs_test: load every general register with a pattern, let the
 * rig deliver an interrupt through the real _trap_entry while nops run,
 * then dump the registers and have C count the ones that came back wrong.
 */

    .section .text
    .globl trap_regs_test
    .type trap_regs_test, @function
trap_regs_test:
    addi    sp, sp, -192
    sw      ra, 128(sp)
    sw      s0, 132(sp)
    sw      s1, 136(sp)
    sw      s2, 140(sp)
    sw      s3, 144(sp)
    sw      s4, 148(sp)
    sw      s5, 152(sp)
    sw      s6, 156(sp)
    sw      s7, 160(sp)
    sw      s8, 164(sp)
    sw      s9, 168(sp)
    sw      s10, 172(sp)
    sw      s11, 176(sp)
    sw      gp, 180(sp)
    sw      tp, 184(sp)

    /* Ask the rig for an interrupt a few instructions from now. */
    li      t0, 0xE00FF000
    li      t1, 0x40000000
    sw      t1, 0(t0)

    /* Pattern: register n holds 0x5A5A0000 ^ (n * 0x01010101) */
    li      x1,  0x5A5A0000 ^ (1 * 0x01010101)
    li      x5,  0x5A5A0000 ^ (5 * 0x01010101)
    li      x6,  0x5A5A0000 ^ (6 * 0x01010101)
    li      x7,  0x5A5A0000 ^ (7 * 0x01010101)
    li      x8,  0x5A5A0000 ^ (8 * 0x01010101)
    li      x9,  0x5A5A0000 ^ (9 * 0x01010101)
    li      x10, 0x5A5A0000 ^ (10 * 0x01010101)
    li      x11, 0x5A5A0000 ^ (11 * 0x01010101)
    li      x12, 0x5A5A0000 ^ (12 * 0x01010101)
    li      x13, 0x5A5A0000 ^ (13 * 0x01010101)
    li      x14, 0x5A5A0000 ^ (14 * 0x01010101)
    li      x15, 0x5A5A0000 ^ (15 * 0x01010101)
    li      x16, 0x5A5A0000 ^ (16 * 0x01010101)
    li      x17, 0x5A5A0000 ^ (17 * 0x01010101)
    li      x18, 0x5A5A0000 ^ (18 * 0x01010101)
    li      x19, 0x5A5A0000 ^ (19 * 0x01010101)
    li      x20, 0x5A5A0000 ^ (20 * 0x01010101)
    li      x21, 0x5A5A0000 ^ (21 * 0x01010101)
    li      x22, 0x5A5A0000 ^ (22 * 0x01010101)
    li      x23, 0x5A5A0000 ^ (23 * 0x01010101)
    li      x24, 0x5A5A0000 ^ (24 * 0x01010101)
    li      x25, 0x5A5A0000 ^ (25 * 0x01010101)
    li      x26, 0x5A5A0000 ^ (26 * 0x01010101)
    li      x27, 0x5A5A0000 ^ (27 * 0x01010101)
    li      x28, 0x5A5A0000 ^ (28 * 0x01010101)
    li      x29, 0x5A5A0000 ^ (29 * 0x01010101)
    li      x30, 0x5A5A0000 ^ (30 * 0x01010101)
    li      x31, 0x5A5A0000 ^ (31 * 0x01010101)

    /* The interrupt lands somewhere in here. */
    .rept 32
    nop
    .endr

    /* Dump everything, sp included, into the frame. */
    sw      x1,  4(sp)
    sw      x2,  8(sp)
    sw      x5,  20(sp)
    sw      x6,  24(sp)
    sw      x7,  28(sp)
    sw      x8,  32(sp)
    sw      x9,  36(sp)
    sw      x10, 40(sp)
    sw      x11, 44(sp)
    sw      x12, 48(sp)
    sw      x13, 52(sp)
    sw      x14, 56(sp)
    sw      x15, 60(sp)
    sw      x16, 64(sp)
    sw      x17, 68(sp)
    sw      x18, 72(sp)
    sw      x19, 76(sp)
    sw      x20, 80(sp)
    sw      x21, 84(sp)
    sw      x22, 88(sp)
    sw      x23, 92(sp)
    sw      x24, 96(sp)
    sw      x25, 100(sp)
    sw      x26, 104(sp)
    sw      x27, 108(sp)
    sw      x28, 112(sp)
    sw      x29, 116(sp)
    sw      x30, 120(sp)
    sw      x31, 124(sp)

    mv      a0, sp
    call    check_regs_c

    lw      ra, 128(sp)
    lw      s0, 132(sp)
    lw      s1, 136(sp)
    lw      s2, 140(sp)
    lw      s3, 144(sp)
    lw      s4, 148(sp)
    lw      s5, 152(sp)
    lw      s6, 156(sp)
    lw      s7, 160(sp)
    lw      s8, 164(sp)
    lw      s9, 168(sp)
    lw      s10, 172(sp)
    lw      s11, 176(sp)
    lw      gp, 180(sp)
    lw      tp, 184(sp)
    addi    sp, sp, 192
    ret
    .size trap_regs_test, .-trap_regs_test
