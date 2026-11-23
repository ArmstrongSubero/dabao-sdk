/*
 * main.c: BIO instruction set test for baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Core 0 runs a mix of 32 bit and compressed instructions on two numbers
 * from FIFO0 (pushed after the start, since restarting core 0 clears
 * FIFO0) and pushes 28 results to FIFO1; the host checks them against
 * its own arithmetic. Several 32 bit instructions start on a half word, so
 * the prefetch buffer and the second word fetch are exercised. PicoRV32 is
 * built with CATCH_MISALIGN off: a misaligned load or store uses the
 * aligned word, and a half word picks its half from address bit 1, so lh
 * from 0x203 returns bytes 2 and 3 and sh to 0x201 writes bytes 0 and 1.
 * Core 1 then shows three read side effects of the BIO registers: mul
 * stays in ld_rs1 for four cycles while the multiplier works and reads
 * x16 on each, lui reads the register named by its bits 19:15, fence reads
 * both of its fields, and an all zero half word is illegal and traps.
 */

#include "bao.h"
#include "hardware/bio.h"

static const uint32_t isa[] = {
    0x84C28442,  /* 000: c.mv x8,x16; c.mv x9,x16 */
    0x02940533,  /* 004: mul x10,x8,x9 */
    0x153388AA,  /* 008: c.mv x17,x10; mulh x10,x8,x9 */
    0x88AA0294,  /* 00C: c.mv x17,x10 */
    0x02942533,  /* 010: mulhsu x10,x8,x9 */
    0x353388AA,  /* 014: c.mv x17,x10; mulhu x10,x8,x9 */
    0x88AA0294,  /* 018: c.mv x17,x10 */
    0x40940533,  /* 01C: sub x10,x8,x9 */
    0x553388AA,  /* 020: c.mv x17,x10; sra x10,x8,x9 */
    0x88AA4094,  /* 024: c.mv x17,x10 */
    0x00945533,  /* 028: srl x10,x8,x9 */
    0x153388AA,  /* 02C: c.mv x17,x10; sll x10,x8,x9 */
    0x88AA0094,  /* 030: c.mv x17,x10 */
    0x00942533,  /* 034: slt x10,x8,x9 */
    0x353388AA,  /* 038: c.mv x17,x10; sltu x10,x8,x9 */
    0x88AA0094,  /* 03C: c.mv x17,x10 */
    0x00944533,  /* 040: xor x10,x8,x9 */
    0x85A288AA,  /* 044: c.mv x17,x10; c.mv x11,x8 */
    0x88AE858D,  /* 048: c.srai x11,0x3; c.mv x17,x11 */
    0x818D85A2,  /* 04C: c.mv x11,x8; c.srli x11,0x3 */
    0x85A288AE,  /* 050: c.mv x17,x11; c.mv x11,x8 */
    0x88AE99E5,  /* 054: c.andi x11,-7; c.mv x17,x11 */
    0x059685A2,  /* 058: c.mv x11,x8; c.slli x11,0x5 */
    0x251388AE,  /* 05C: c.mv x17,x11; slti x10,x8,-5 */
    0x88AAFFB4,  /* 060: c.mv x17,x10 */
    0x06443513,  /* 064: sltiu x10,x8,100 */
    0x202388AA,  /* 068: c.mv x17,x10; sw x8,512(x0) */
    0x05032080,  /* 06C: lb x10,513(x0) */
    0x88AA2010,  /* 070: c.mv x17,x10 */
    0x20304503,  /* 074: lbu x10,515(x0) */
    0x150388AA,  /* 078: c.mv x17,x10; lh x10,515(x0) */
    0x88AA2030,  /* 07C: c.mv x17,x10 */
    0x20105503,  /* 080: lhu x10,513(x0) */
    0x250388AA,  /* 084: c.mv x17,x10; lw x10,514(x0) */
    0x88AA2020,  /* 088: c.mv x17,x10 */
    0x209010A3,  /* 08C: sh x9,513(x0) */
    0x20900123,  /* 090: sb x9,514(x0) */
    0x20002503,  /* 094: lw x10,512(x0) */
    0x051788AA,  /* 098: c.mv x17,x10; auipc x10,0x0 */
    0x88AA0000,  /* 09C: c.mv x17,x10 */
    0x004000EF,  /* 0A0: jal x1,a4 <n1> */
    0x44638886,  /* 0A4: c.mv x17,x1; blt x8,x9,ae <l1> */
    0x45050094,  /* 0A8: c.li x10,1 */
    0x4509A011,  /* 0AC: c.j b0 <l2>; c.li x10,2 */
    0x061388AA,  /* 0B0: c.mv x17,x10; addi x12,x0,512 */
    0xC6042000,  /* 0B4: c.sw x9,8(x12) */
    0x88B64614,  /* 0B8: c.lw x13,8(x12); c.mv x17,x13 */
    0x00000717,  /* 0BC: auipc x14,0x0 */
    0x00C700E7,  /* 0C0: jalr x1,12(x14) */
    0x06300893,  /* 0C4: addi x17,x0,99 */
    0xA0018886,  /* 0C8: c.mv x17,x1; c.j ca <self> */
};

static const uint32_t quirks[] = {
    0x00300313,  /* 000: addi x6,x0,3 */
    0x026802B3,  /* 004: mul x5,x16,x6 */
    0x00028893,  /* 008: addi x17,x5,0 */
    0x000803B7,  /* 00C: lui x7,0x80 */
    0x0330000F,  /* 010: fence rw,rw */
    0x00100893,  /* 014: addi x17,x0,1 */
    0x00000000,  /* 018: .word 0 */
};

#define N_RESULTS 28

int main(void)
{
    uint32_t a = 0x87654321u, b = 0xFFFFFF9Bu, sh = b & 31;
    uint32_t got[N_RESULTS], want[N_RESULTS];
    uint32_t i, n = 0, ok = 0, r, m;

    want[0] = a * b;
    want[1] = (uint32_t)(((int64_t)(int32_t)a * (int64_t)(int32_t)b) >> 32);
    want[2] = (uint32_t)(((int64_t)(int32_t)a * (int64_t)(uint64_t)b) >> 32);
    want[3] = (uint32_t)(((uint64_t)a * b) >> 32);
    want[4] = a - b;
    want[5] = (uint32_t)((int32_t)a >> sh);
    want[6] = a >> sh;
    want[7] = a << sh;
    want[8] = (int32_t)a < (int32_t)b;
    want[9] = a < b;
    want[10] = a ^ b;
    want[11] = (uint32_t)((int32_t)a >> 3);
    want[12] = a >> 3;
    want[13] = a & 0xFFFFFFF9u;
    want[14] = a << 5;
    want[15] = (int32_t)a < -5;
    want[16] = a < 100;
    want[17] = 0x43;
    want[18] = 0x87;
    want[19] = 0xFFFF8765u;
    want[20] = 0x4321;
    want[21] = 0x87654321u;
    want[22] = 0x879BFF9Bu;
    want[23] = 0x09A;
    want[24] = 0x0A4;
    want[25] = 2;
    want[26] = b;
    want[27] = 0x0C4;

    bao_init();
    bio_init(FCLK_HZ);
    bio_load_code_words(0, isa, sizeof(isa) / 4);
    bio_load_code_words(1, quirks, sizeof(quirks) / 4);

    bio_start_cores(0x1);
    bio_push_fifo0(a);
    bio_push_fifo0(b);
    while (n < N_RESULTS) {
        if (bio_fifo_level(1))
            got[n++] = bio_pop_fifo1();
    }
    bio_stop_cores(0x1);
    for (i = 0; i < N_RESULTS; i++) {
        if (got[i] == want[i])
            ok++;
        else
            mini_printf("result %u: got 0x%08x want 0x%08x\r\n", i, got[i], want[i]);
    }
    mini_printf("isa: %u of %u match\r\n", ok, N_RESULTS);
    mini_printf("misaligned: lh 0x203 = %08x, lw 0x202 = %08x, after sh and sb %08x\r\n",
                got[19], got[21], got[22]);

    BIO_SFR_FIFO_CLR = 0xF;
    for (i = 1; i <= 6; i++)
        bio_push_fifo0(i);
    bio_push_fifo3(7);
    bio_start_cores(0x2);
    while (bio_fifo_level(1) < 2) { }
    delay_us(2);
    r = bio_pop_fifo1();
    m = bio_pop_fifo1();
    mini_printf("mul with x16: %u, marker %u, FIFO0 left %u, FIFO3 left %u\r\n",
                r, m, bio_fifo_level(0), bio_fifo_level(3));
    mini_printf("illegal half word: trap %u at pc 0x%03x\r\n", bio_core_trapped(1), bio_core_pc(1));
    bio_stop_cores(0x2);
    mini_printf("[Done] bio_isa\r\n");
    while (1) { }
}
