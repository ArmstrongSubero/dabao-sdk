/*
 * main.c: BIO core start test for baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Checks that a core started with bio_start_cores runs nothing before pc 0
 * of the program loaded into it, whatever it was doing when it stopped.
 *
 * The witness program pushes 0 to 7 into FIFO3 (one push per word, then a
 * jump to self). FIFO3 is not core 0's, so a start does not clear it. If
 * the core ran on from its old state before its reset (bio_bdma.sv 447 to
 * 449 against 1842 to 1849: the enable lands a pclk before the restart),
 * it would execute some witness word at its old pc and push an extra value,
 * so FIFO3 would not read exactly 0 to 7.
 *
 * Core 0 is stopped in each of these states before the witness starts:
 *   after bio_init (its mem_init program)
 *   the witness itself, restarted without loading it again
 *   waiting for a quantum with x20 in rs1 and a divider of 600, far longer
 *     than the two pclk of a reset; this is
 *     the one stall that resetn does not lift (bio_bdma.sv 2199)
 *   a loop reading x20 at full speed that never goes back to word 0; a
 *     reset while the decoder holds x20 takes the stale RAM output as the
 *     first instruction (bio_bdma.sv 1835 to 1839)
 *   a loop with one x20 read and three other instructions, stopped by a
 *     plain SFR_CTRL write at sixteen different times and then parked by
 *     bio_stop_cores; when the reset lands while the decoder holds x20 the
 *     first park attempt runs a stale word and goes on in the loop, so the
 *     second attempt (every RAM word ebreak) is needed; in the emulator one
 *     of the sixteen does that. The test checks the RAM is the same after.
 *   stalled reading an empty FIFO1
 *   stalled pushing into a full FIFO0 (the stop keeps FIFO0's nine words)
 *   stopped by a plain SFR_CTRL write, then loaded with bio_load_code_words
 *   stopped because bio_start_cores started core 1 alone
 */

#include "bao.h"
#include "hardware/bio.h"

static const uint32_t witness[] = {
    0x00000993,  /* 000: addi x19,x0,0 */
    0x00100993,  /* 004: addi x19,x0,1 */
    0x00200993,  /* 008: addi x19,x0,2 */
    0x00300993,  /* 00C: addi x19,x0,3 */
    0x00400993,  /* 010: addi x19,x0,4 */
    0x00500993,  /* 014: addi x19,x0,5 */
    0x00600993,  /* 018: addi x19,x0,6 */
    0x00700993,  /* 01C: addi x19,x0,7 */
    0x0000006F,  /* 020: jal x0,20 <self> */
};

static const uint32_t quanta[] = {
    0x000A0293,  /* 000: addi x5,x20,0 */
    0xFFDFF06F,  /* 004: jal x0,0 */
};

static const uint32_t loop20[] = {
    0x00000013,  /* 000: addi x0,x0,0 */
    0x000A0293,  /* 004: addi x5,x20,0 */
    0xFFDFF06F,  /* 008: jal x0,4 */
};

static const uint32_t loopx[] = {
    0x00000013,  /* 000: addi x0,x0,0 */
    0x000A0293,  /* 004: addi x5,x20,0 */
    0x00100313,  /* 008: addi x6,x0,1 */
    0x00100393,  /* 00C: addi x7,x0,1 */
    0x00100413,  /* 010: addi x8,x0,1 */
    0xFF1FF06F,  /* 014: jal x0,4 */
};

static const uint32_t reader[] = {
    0x00088293,  /* 000: addi x5,x17,0 */
    0xFFDFF06F,  /* 004: jal x0,0 */
};

static const uint32_t pusher[] = {
    0x00500813,  /* 000: addi x16,x0,5 */
    0xFFDFF06F,  /* 004: jal x0,0 */
};

static const uint32_t halt[] = {
    0x0000006F,  /* 000: jal x0,0 */
};

/* start the witness on core 0 and print what it pushed into FIFO3 */
static void run_witness(const char *label)
{
    uint32_t n, i;

    bio_set_divider(0, 0, 0);
    bio_start_cores(0x1);
    delay_us(5);
    n = bio_fifo_level(3);
    mini_printf("%s: %u words:", label, n);
    for (i = 0; i < n; i++)
        mini_printf(" %u", bio_pop_fifo3());
    mini_printf("\r\n");
    bio_stop_cores(0x1);
}

/* run a program on core 0 for a while, then stop it with bio_stop_cores */
static void run_then_stop(const uint32_t *code, uint32_t words)
{
    bio_load_code_words(0, code, words);
    bio_start_cores(0x1);
    delay_us(5);
    bio_stop_cores(0x1);
}

static volatile uint32_t sink;

int main(void)
{
    bao_init();
    bio_init(FCLK_HZ);
    BIO_SFR_FIFO_CLR = 0xF;

    bio_load_code_words(0, witness, sizeof(witness) / 4);
    run_witness("after bio_init");
    run_witness("restart, no reload");

    bio_load_code_words(0, quanta, sizeof(quanta) / 4);
    bio_set_divider(0, 600, 0);
    bio_start_cores(0x1);
    delay_us(5);
    bio_stop_cores(0x1);
    mini_printf("quantum wait stopped: trap %u\r\n", bio_core_trapped(0));
    bio_load_code_words(0, witness, sizeof(witness) / 4);
    run_witness("after quantum wait");

    bio_set_divider(0, 0, 0);
    run_then_stop(loop20, sizeof(loop20) / 4);
    mini_printf("x20 loop stopped: trap %u\r\n", bio_core_trapped(0));
    bio_load_code_words(0, witness, sizeof(witness) / 4);
    run_witness("after x20 loop");

    {
        uint32_t k, w, spin, parked = 0, kept = 0, same;
        for (k = 0; k < 16; k++) {
            bio_load_code_words(0, loopx, sizeof(loopx) / 4);
            bio_start_cores(0x1);
            for (spin = 0; spin < 50 + 3 * k; spin++)
                sink++;
            BIO_SFR_CTRL = 0;
            bio_stop_cores(0x1);
            parked += (uint32_t)bio_core_trapped(0);
            same = 1;
            for (w = 0; w < sizeof(loopx) / 4; w++)
                if (bio_imem(0)[w] != loopx[w])
                    same = 0;
            /* words 6 to 8 still hold the end of the witness */
            for (w = sizeof(loopx) / 4; w < sizeof(witness) / 4; w++)
                if (bio_imem(0)[w] != witness[w])
                    same = 0;
            for (w = sizeof(witness) / 4; w < BIO_IMEM_WORDS; w++)
                if (bio_imem(0)[w] != BIO_HALT_WORD)
                    same = 0;
            kept += same;
        }
        mini_printf("x20 loops stopped: %u of 16 trapped, RAM kept %u of 16\r\n", parked, kept);
    }
    bio_load_code_words(0, witness, sizeof(witness) / 4);
    run_witness("after x20 loops");

    run_then_stop(reader, sizeof(reader) / 4);
    bio_load_code_words(0, witness, sizeof(witness) / 4);
    run_witness("after FIFO read stall");

    run_then_stop(pusher, sizeof(pusher) / 4);
    mini_printf("full FIFO0 kept: %u\r\n", bio_fifo_level(0));
    bio_load_code_words(0, witness, sizeof(witness) / 4);
    run_witness("after FIFO write stall");
    mini_printf("FIFO0 after start: %u\r\n", bio_fifo_level(0));

    bio_load_code_words(0, pusher, sizeof(pusher) / 4);
    bio_start_cores(0x1);
    delay_us(5);
    BIO_SFR_CTRL = 0;
    bio_load_code_words(0, witness, sizeof(witness) / 4);
    run_witness("after plain CTRL stop");

    bio_load_code_words(0, reader, sizeof(reader) / 4);
    bio_load_code_words(1, halt, 1);
    bio_start_cores(0x1);
    delay_us(5);
    bio_start_cores(0x2);
    mini_printf("core 0 when core 1 started: enabled %u, trap %u\r\n",
                BIO_SFR_CTRL & 1u, bio_core_trapped(0));
    bio_load_code_words(0, witness, sizeof(witness) / 4);
    run_witness("after start of core 1");

    mini_printf("[Done] bio_start\r\n");
    while (1) { }
}
