/*
 * Baochip-1x memory map rig: guard and trap path checks.
 *
 * Runs inside the unicorn model in baochip_emu.py, never on hardware.
 * Every result lands in results[] which the host reads back.
 */

#include "bao.h"
#include "hardware/rram.h"
#include "hardware/bio.h"
#include "hardware/bio_dma.h"
#include "hardware/irq.h"
#include "sevs_runtime.h"

#include <stdint.h>

volatile uint32_t results[64] __attribute__((aligned(16)));

/* .data and .bss sanity */
uint32_t init_data = 0x12345678u;
uint32_t bss_var;

/* Set by the host rig when it wants an interrupt delivered. */
#define RIG_INJECT_IRQ   (*(volatile uint32_t *)0xE00FF000u)
#define RIG_CONSOLE      (*(volatile uint32_t *)0xE00FF004u)

/* Written by the host before the run, outside .bss so startup keeps it. */
#define RIG_CASE         (*(volatile uint32_t *)0x50030000u)

extern uint32_t trap_regs_test(void);
extern uint32_t _main_stack_top[];
extern uint32_t _trap_stack_top[];

static volatile uint32_t irq_hits;
static volatile uint32_t irq_sp_seen;

/* Called by trap_regs_test with the register dump. Slot n holds xn. */
uint32_t check_regs_c(const uint32_t *frame)
{
    uint32_t bad = 0;
    uint32_t n;
    for (n = 1; n < 32; n++) {
        uint32_t expect;
        if (n == 2) {
            expect = (uint32_t)(uintptr_t)frame;   /* sp at dump time */
        } else if (n == 3 || n == 4) {
            continue;                               /* gp, tp untouched */
        } else {
            expect = 0x5A5A0000u ^ (n * 0x01010101u);
        }
        if (frame[n] != expect) bad |= (1u << n);
    }
    return bad;
}

static void timer_handler(uint32_t events)
{
    uint32_t sp;
    (void)events;
    __asm__ volatile ("mv %0, sp" : "=r"(sp));
    irq_sp_seen = sp;
    irq_hits++;
}

static uint32_t read_mstatus(void)
{
    uint32_t v;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(v));
    return v;
}

static uint8_t dma_src[256] __attribute__((aligned(16)));
static uint8_t dma_dst[256] __attribute__((aligned(16)));

void test_done(void) __attribute__((noinline, noreturn, used));

int main(void)
{
    uint8_t buf[64];
    uint32_t i;
    int n = 0;

    for (i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)(0xA0u + i);

    /* Addresses go through a volatile so the compile time guard in
     * rram.h cannot see them (it refuses the constant forms outright,
     * which is checked separately by the host). This exercises the
     * runtime guards in the driver itself. */
    volatile uint32_t a[16];
    a[0] = 0x60000000u; a[1] = 0x60020000u; a[2] = 0x6005FFF0u; a[3] = 0x603DA000u;
    a[4] = 0x603D9FF0u; a[5] = 0x60300000u; a[6] = 0x60040000u; a[7] = 0x603D9FE0u;
    a[8] = 0x60060000u; a[9] = 0xFFFFFFF0u; a[10] = 0x5FFF0000u; a[11] = 0x603D0000u;
    a[12] = 0x61000000u;

    /*
     * Cases that must end in sevs_target_halt rather than return. The
     * host selects one, runs, and expects the halt with a crash record.
     * Case 6 and 7 are rig self tests: a misbehaving application that
     * touches boot0 or the RRC directly must be caught by the model.
     */
    switch (RIG_CASE) {
    case 1: results[0] = (uint32_t)bio_allow_range(0, a[10], 0x20000u); break;   /* overlaps boot0 */
    case 2: results[0] = (uint32_t)bio_allow_range(0, a[11], 0x10000u); break;   /* overlaps security */
    case 3: results[0] = (uint32_t)bio_allow_range(0, a[0], 0x60000u); break;    /* exactly boot0+boot1 */
    case 4: results[0] = (uint32_t)bio_dma_start(dma_dst, dma_src, 63); break;   /* len not a word multiple */
    case 5: results[0] = (uint32_t)bio_load_code(5, dma_src, 16); break;         /* bad core */
    case 6: *(volatile uint32_t *)(uintptr_t)a[0] = 1u; break;                   /* app store into boot0 */
    case 7: results[0] = *(volatile uint32_t *)(uintptr_t)(a[9] & 0x40000000u); break; /* app read of RRC */
    case 8: results[0] = *(volatile uint32_t *)(uintptr_t)a[3]; break;           /* app read of security region */
    case 9: results[0] = (uint32_t)bio_allow_range(4, a[12], 0x1000u); break;    /* bad window */
    default: break;
    }
    if (RIG_CASE != 0) {
        results[1] = 0xBAD0BAD0u;   /* a halt case came back instead of stopping */
        test_done();
    }

    /* 0,1: runtime brought .data and .bss up correctly */
    results[n++] = init_data;
    results[n++] = bss_var;

    /* RRAM write guards: every one of these must be refused (-1) */
    results[n++] = (uint32_t)rram_write(a[0], buf, 32);          /* 2 boot0 */
    results[n++] = (uint32_t)rram_write(a[1], buf, 32);          /* 3 boot1 */
    results[n++] = (uint32_t)rram_write(a[2], buf, 32);          /* 4 straddles boot1 end */
    results[n++] = (uint32_t)rram_write(a[3], buf, 32);          /* 5 security region */
    results[n++] = (uint32_t)rram_write(a[4], buf, 32);          /* 6 straddles into security */
    results[n++] = (uint32_t)rram_write(a[5], buf, a[9]); /* 7 wrapping length */
    results[n++] = (uint32_t)rram_write_page(a[0], buf);         /* 8 page into boot0 */
    results[n++] = (uint32_t)rram_write_page(a[4], buf);         /* 9 unaligned, straddles */
    results[n++] = (uint32_t)rram_erase_page(a[6]);              /* 10 erase in boot1 */

    /* RRAM writes that must be allowed (0) */
    results[n++] = (uint32_t)rram_write(a[5], buf, 40);          /* 11 user storage */
    results[n++] = (uint32_t)rram_write_page(a[7], buf);         /* 12 last valid page */
    results[n++] = (uint32_t)rram_write(a[8], buf, 32);          /* 13 first program page */

    /* BIO window guards: refused with -1 */
    results[n++] = (uint32_t)bio_allow_range(0, a[9], 0x20u);               /* 14 wraps */
    results[n++] = 0xFFFFFFFFu;                                             /* 15 (halt case 9) */
    results[n++] = (uint32_t)bio_allow_range(0, a[12], 0u);                 /* 16 zero length */
    results[n++] = 0xFFFFFFFFu;                                             /* 17 (halt cases below) */
    results[n++] = (uint32_t)bio_allow_range(0, a[12], 0x200000u);          /* 18 SRAM, allowed */
    results[n++] = (uint32_t)bio_allow_range(0, a[8], a[3] - a[8]);         /* 19 program+storage, allowed */

    /* BIO DMA argument guards */
    results[n++] = (uint32_t)bio_dma_start(dma_dst + 1, dma_src, 64);   /* 20 misaligned dst */
    results[n++] = (uint32_t)bio_dma_start(dma_dst, dma_src + 2, 64);   /* 21 misaligned src */
    results[n++] = (uint32_t)bio_dma_start(dma_dst, (const void *)(uintptr_t)a[9], 64); /* 22 src wraps */

    /* Interrupt path through the real irq driver */
    irq_init();
    irq_set_handler(IRQ_TIMER0, timer_handler);
    irq_enable(IRQ_TIMER0);
    results[n++] = read_mstatus() & 0x8u;                                 /* 23 MIE on */
    RIG_INJECT_IRQ = (1u << 30);
    __asm__ volatile ("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop" ::: "memory");
    results[n++] = irq_hits;                                              /* 24 handler ran */
    results[n++] = irq_sp_seen;                                           /* 25 handler sp */
    results[n++] = read_mstatus() & 0x8u;                                 /* 26 MIE still on */

    /* Trap entry and exit: every register restored, trap stack used */
    results[n++] = trap_regs_test();                                      /* 27 mismatch bits */
    results[n++] = irq_hits;                                              /* 28 now 2 */
    results[n++] = (uint32_t)(uintptr_t)_main_stack_top;                  /* 29 */
    results[n++] = (uint32_t)(uintptr_t)_trap_stack_top;                  /* 30 */

    /* Recoverable SEVS_CHECK hands interrupts back */
    sevs_assert_fail(SEVS_FAIL_CHECK, "rig", "test_main.c", 1, "main");
    results[n++] = read_mstatus() & 0x8u;                                 /* 31 MIE after CHECK */
    results[n++] = (uint32_t)RIG_CONSOLE;                                 /* 32 rig console marker */

    results[n++] = 0xC0DEC0DEu;                                           /* 33 end marker */
    test_done();
    return 0;
}

void test_done(void)
{
    for (;;) {
        __asm__ volatile ("" ::: "memory");
    }
}
