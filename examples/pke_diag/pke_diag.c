/*
 * pke_diag.c: run single PKE operations and print the SCE status registers
 *
 * Wiring:
 *   PB14 --> USB serial RX
 */

#include "bao.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sce.h"

#define SCE_AHBSOPT  REG32(SCE_CTRL_BASE + 0x08)
#define SCE_TICKCYC  REG32(SCE_CTRL_BASE + 0x20)
#define SCE_TICKCNT  REG32(SCE_CTRL_BASE + 0x24)

static void show(const char *tag)
{
    mini_printf("  %s\r\n", tag);
    mini_printf("    SCEMODE %08x  SUBCLKEN %08x  AHBSOPT %08x  BUSYSR %08x\r\n",
                SCE_SCEMODE, SCE_SUBCLKEN, SCE_AHBSOPT, SCE_BUSYSR);
    mini_printf("    DONEFR  %08x  ERRFR    %08x  TICKCYC %08x  TICKCNT %08x\r\n",
                SCE_DONEFR, SCE_ERRFR, SCE_TICKCYC, SCE_TICKCNT);
    mini_printf("    PKE CRFUNC %08x  FR %08x  SRMFSM %08x\r\n",
                PKE_CRFUNC, PKE_FR, PKE_SRMFSM);
    mini_printf("    PKE OPTNW %08x  OPTEW %08x  OPTLTX %08x  MIMMCR %08x\r\n",
                PKE_OPTNW, PKE_OPTEW, PKE_OPTLTX, PKE_MIMMCR);
}

static void ctrl_reset(void)
{
    SCE_RESETRAM = SCE_RESETRAM_CTRL;
    memory_fence();
    uint64_t t0 = millis();
    while ((SCE_BUSYSR & SCE_BUSY_RAMCLR) && millis() - t0 < 200) {
    }
    SCE_SCEMODE  = 0;
    SCE_SUBCLKEN = 0x1F;
    memory_fence();
}

static void run(uint32_t fn, const char *tag)
{
    PKE_CRFUNC = fn;
    PKE_FR = 0x0F;
    memory_fence();
    PKE_AR = 0x5A;
    memory_fence();

    uint64_t t0 = millis();
    while (!(PKE_FR & 1u)) {
        if (millis() - t0 > 200) {
            mini_printf("  %s: never finished\r\n", tag);
            show("after timeout");
            return;
        }
    }
    mini_printf("  %s: done in %u ms\r\n", tag, (uint32_t)(millis() - t0));
    show("after");
    PKE_FR |= 1u;
}

int main(void)
{
    bao_init();

    mini_printf("\r\nPKE Diagnostic 2\r\n\r\n");

    mini_printf("[1] At start\r\n");
    show("start");

    mini_printf("\r\n[2] RSAINIT, 128 bit modulus\r\n");
    ctrl_reset();
    PKE_OPTLTX  = 0;
    PKE_OPTMASK = 0;
    PKE_MIMMCR  = 0;
    PKE_OPTNW   = 128;
    PKE_PCON    = 0;
    for (uint32_t i = 0; i < 12; i++)
        PKE_SEG_PCON[i] = 0;
    PKE_SEG_PCON[0] = 0x00000027u;
    PKE_SEG_PCON[1] = 0x89ABCDEFu;
    PKE_SEG_PCON[2] = 0x12345678u;
    PKE_SEG_PCON[3] = 0xF0000000u;
    PKE_SEG_PCON[4 + 4] = 1u;
    run(PKE_FN_RSAINIT, "RSAINIT");

    mini_printf("\r\n[3] ECINIT, P-256\r\n");
    ctrl_reset();
    static const uint32_t p256_p_a[16] = {
        0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
        0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF,
        0xFFFFFFFC, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
        0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF
    };
    PKE_MIMMCR  = 0;
    PKE_OPTMASK = 0;
    PKE_OPTNW   = 256;
    PKE_OPTEW   = 0;
    PKE_OPTLTX  = 0;
    for (uint32_t i = 0; i < 26; i++)
        PKE_SEG_PCON[i] = 0;
    for (uint32_t i = 0; i < 16; i++)
        PKE_SEG_PCON[i] = p256_p_a[i];
    PKE_SEG_PCON[16 + 8] = 1u;
    PKE_PCON = 0;
    run(PKE_FN_ECINIT, "ECINIT");

    mini_printf("\r\n[4] gcd(12, 18)\r\n");
    ctrl_reset();
    PKE_OPTLTX  = 0;
    PKE_OPTMASK = 0;
    PKE_MIMMCR  = 0;
    PKE_OPTNW   = 64;
    for (uint32_t i = 0; i < 8; i++)
        PKE_SEG_PIB[i] = 0;
    PKE_SEG_PIB[0] = 12;
    PKE_SEG_PIB[2] = 18;
    PKE_PIB0 = 0;
    PKE_PIB1 = 2;
    PKE_POB  = 0;
    run(PKE_FN_GCD, "gcd");
    mini_printf("  result %u (expect 6)\r\n", PKE_SEG_POB[0]);

    ctrl_reset();
    mini_printf("\r\n[Done]\r\n");
    while (1) {
        __asm__ volatile ("wfi");
    }
}
