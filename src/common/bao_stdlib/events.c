/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * bao_events.c - drain every event array before the application runs.
 *
 * WHY
 *
 * The only code in the SDK that clears a pending event is this loop,
 * inside trap_dispatch:
 *
 *     for (i = 0; i < 20; i++)
 *         if (pending & (1 << i)) {
 *             *ev_pending = events;      // write one to clear
 *             if (irq_handlers[i]) irq_handlers[i](events);
 *         }
 *
 * trap_dispatch runs only when an interrupt is taken. Interrupts are
 * enabled only by irq_init. A program that never calls irq_init has it
 * removed by --gc-sections, and then nothing clears anything, ever.
 *
 * That is the state every one of the failing builds was in. Their maps
 * show irq_init discarded at address 0 while trap_dispatch survives,
 * because mtvec points at _trap_entry which calls it.
 *
 * Meanwhile events keep arriving. Array 6 is the SPI master and takes
 * one from every transfer that ends with SPI_CMD_EOT(evt = 1), which
 * until recently was every transfer the SDK made. Array 13 is bus
 * errors and SCE errors. Array 15 is a security sensor.
 *
 * Those last two are the reason this matters beyond tidiness. rrc.sv
 * drives a one way counter in RRAM from the event bus with no software
 * involvement, boot1 checks that counter before it will run anything,
 * and the counter cannot be cleared. Events that pile up unread are
 * not free.
 *
 * So this drains all twenty arrays plus Timer0 once, at startup, before
 * the application does anything. Whatever boot1 left pending is not
 * charged to your program, and a program that never enables interrupts
 * starts from a known state rather than an inherited one.
 *
 * WHERE TO CALL IT
 *
 * First thing in bao_init, before any peripheral is touched. Failing
 * that, first line of app_init. It must not be conditional on anything
 * the linker can garbage collect, which is exactly how irq_init went
 * missing.
 */

#include <stdint.h>

/*
 * Same table as bao1x_irq.c. Duplicated rather than shared because that
 * one is static, and making it extern would expose it to code that has
 * no business walking it.
 */
static const uint32_t ev_array_base[20] = {
    0xE0004000UL,  /*  0: MDMA, BIO                        */
    0xE0005000UL,  /*  1: USB                              */
    0xE0010000UL,  /*  2: QFC, MDMA, Mailbox, AO wakeup    */
    0xE0011000UL,  /*  3: Crypto                           */
    0xE0012000UL,  /*  4: Crypto dupes                     */
    0xE0013000UL,  /*  5: UART                             */
    0xE0014000UL,  /*  6: SPI master                       */
    0xE0015000UL,  /*  7: I2C                              */
    0xE0016000UL,  /*  8: SDIO, I2S, Camera, ADC           */
    0xE0017000UL,  /*  9: SCIF, SPI slave, PWM             */
    0xE0006000UL,  /* 10: IOX, USB, SDDC, BIO              */
    0xE0007000UL,  /* 11: I2S dupes                        */
    0xE0008000UL,  /* 12: I2C NACK/error                   */
    0xE0009000UL,  /* 13: Bus errors, SCE errors           */
    0xE000A000UL,  /* 14: UART2/3 dupes, TRNG              */
    0xE000B000UL,  /* 15: Security sensor                  */
    0xE000C000UL,  /* 16: Camera, I2S, SPIM dupes          */
    0xE000D000UL,  /* 17: I2C1, BIO, QFC, ADC dupes        */
    0xE000E000UL,  /* 18: BIO, I2C2, I2C NACK dupes        */
    0xE000F000UL,  /* 19: Mailbox, BIO, SDIO dupes         */
};

#define EV_PENDING_OFF   0x10
#define EV_ENABLE_OFF    0x14

/* From regs/addressmap.h and regs/timer.h. 0x40043000 is ATIMER, which
   is a different block; getting that wrong writes a register nobody
   meant to touch. */
#define TIMER0_BASE      0xE001C000UL
#define TIMER0_EV_PEND   (*(volatile uint32_t *)(TIMER0_BASE + 0x18))

/** @brief Drain every event array and leave every source disabled.
 *
 * Pending bits are write one to clear, so the read value goes straight
 * back. Enables are cleared afterwards so a source boot1 left armed
 * does not immediately refill the array behind us.
 *
 * Safe to call more than once and safe to call with interrupts off,
 * which is how it is meant to be called.
 *
 * @req REQ-DABAO-EVENTS-0001 */
void bao_events_clear(void)
{
    uint32_t i;

    for (i = 0; i < 20u; i++) {
        volatile uint32_t *pend =
            (volatile uint32_t *)(ev_array_base[i] + EV_PENDING_OFF);
        volatile uint32_t *en =
            (volatile uint32_t *)(ev_array_base[i] + EV_ENABLE_OFF);

        *en = 0u;             /* stop new ones arriving first */
        *pend = *pend;        /* write one to clear whatever is set */
    }

    TIMER0_EV_PEND = 1u;
}
