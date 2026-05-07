/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Interrupt dispatch system for the Baochip-1x.
 *
 * Provides the trap_dispatch function that the crt0 startup calls.
 * Walks the VexRiscv MIP register to find pending IRQ arrays,
 * reads each array's EV_PENDING, and dispatches to registered handlers.
 */

/* @sevs-callbacks */
#include "hardware/irq.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/timer.h"
#include "sevs_runtime.h"

/* IRQ array base addresses (from bao_irq.h) */
static const uint32_t irq_array_base[20] = {
    0xE0004000,  /*  0: MDMA, BIO */
    0xE0005000,  /*  1: USB */
    0xE0010000,  /*  2: QFC, MDMA, Mailbox, AO wakeup */
    0xE0011000,  /*  3: Crypto */
    0xE0012000,  /*  4: Crypto dupes */
    0xE0013000,  /*  5: UART */
    0xE0014000,  /*  6: SPI master */
    0xE0015000,  /*  7: I2C */
    0xE0016000,  /*  8: SDIO, I2S, Camera, ADC */
    0xE0017000,  /*  9: SCIF, SPI slave, PWM */
    0xE0006000,  /* 10: IOX, USB, SDDC, BIO */
    0xE0007000,  /* 11: I2S dupes */
    0xE0008000,  /* 12: I2C NACK/error */
    0xE0009000,  /* 13: Bus errors, SCE errors */
    0xE000A000,  /* 14: UART2/3 dupes, TRNG */
    0xE000B000,  /* 15: Security sensor */
    0xE000C000,  /* 16: Camera, I2S, SPIM dupes */
    0xE000D000,  /* 17: I2C1, BIO, QFC, ADC dupes */
    0xE000E000,  /* 18: BIO, I2C2, I2C NACK dupes */
    0xE000F000,  /* 19: Mailbox, BIO, SDIO dupes */
};

#define IRQ_EV_PENDING      0x10
#define IRQ_EV_ENABLE       0x14

/* Callback table: 20 IRQ arrays + slots 20-29 unused + slot 30 for Timer0 */
#define IRQ_TABLE_SIZE      31
static irq_handler_t irq_handlers[IRQ_TABLE_SIZE];

static inline uint32_t csr_read_mim(void) {
    uint32_t val;
    __asm__ volatile ("csrr %0, 0xBC0" : "=r"(val));
    return val;
}

static inline void csr_write_mim(uint32_t val) {
    __asm__ volatile ("csrw 0xBC0, %0" :: "r"(val));
}

static inline uint32_t csr_read_mip_vex(void) {
    uint32_t val;
    __asm__ volatile ("csrr %0, 0xFC0" : "=r"(val));
    return val;
}

/** @brief Initialize the interrupt dispatch system and enable global interrupts.
 *  @req REQ-DABAO-IRQ-0001 */
void irq_init(void)
{
    SEVS_ASSERT(IRQ_TABLE_SIZE == 31);

    /* Clear all handlers */
    for (int i = 0; i < IRQ_TABLE_SIZE; i++)
        irq_handlers[i] = (irq_handler_t)0;

    /* Disable all IRQ lines in MIM */
    csr_write_mim(0);

    /* Enable machine external interrupts (mie.MEIE, bit 11) */
    __asm__ volatile ("csrs mie, %0" :: "r"(1 << 11));

    /* Enable global interrupts (mstatus.MIE, bit 3) */
    __asm__ volatile ("csrsi mstatus, 0x8");
}

/** @brief Register a callback for an IRQ source.
 *  @param[in] irq_no IRQ source number (0-19 for arrays, 30 for Timer0).
 *  @param[in] handler Callback function, or NULL to unregister.
 *  @req REQ-DABAO-IRQ-0002 */
void irq_set_handler(uint32_t irq_no, irq_handler_t handler)
{

    if (irq_no < (uint32_t)IRQ_TABLE_SIZE)
        irq_handlers[irq_no] = handler;
}

/** @brief Enable an IRQ source in its event array and in the MIM register.
 *  @param[in] irq_no IRQ source number.
 *  @req REQ-DABAO-IRQ-0003 */
void irq_enable(uint32_t irq_no)
{
    SEVS_ASSERT(irq_no <= 30);

    /* Enable all event bits in the IRQ array */
    if (irq_no < 20) {
        volatile uint32_t *ev_enable =
            (volatile uint32_t *)(irq_array_base[irq_no] + IRQ_EV_ENABLE);
        *ev_enable = 0xFFFF;
    }

    /* Enable this IRQ line in the VexRiscv MIM register */
    csr_write_mim(csr_read_mim() | (1 << irq_no));
}

/** @brief Enable specific event bits within an IRQ array.
 *  @param[in] irq_no IRQ array number (0-19).
 *  @param[in] event_mask Bitmask of events to enable.
 *  @req REQ-DABAO-IRQ-0003 */
void irq_enable_events(uint32_t irq_no, uint32_t event_mask)
{

    if (irq_no < 20) {
        volatile uint32_t *ev_enable =
            (volatile uint32_t *)(irq_array_base[irq_no] + IRQ_EV_ENABLE);
        *ev_enable |= event_mask;
    }

    /* Enable this IRQ line in MIM */
    csr_write_mim(csr_read_mim() | (1 << irq_no));
}

/** @brief Disable an IRQ source in the MIM register.
 *  @param[in] irq_no IRQ source number.
 *  @req REQ-DABAO-IRQ-0004 */
void irq_disable(uint32_t irq_no)
{
    SEVS_ASSERT(irq_no <= 30);

    csr_write_mim(csr_read_mim() & ~(1 << irq_no));
}

/*
 * Main trap dispatch. Overrides the weak default in stdlib.c.
 * Called by crt0 on any trap (interrupt or exception).
 */
/** @brief Main trap handler called by crt0 on any interrupt or exception.
 *  @req REQ-DABAO-IRQ-0005 */
void trap_dispatch(void)
{

    uint32_t mcause;
    __asm__ volatile ("csrr %0, mcause" : "=r"(mcause));

    if (mcause == 0x8000000B) {
        /* Machine external interrupt */
        uint32_t pending = csr_read_mip_vex();

        /* Check Timer0 (bit 30) */
        if ((pending & (1 << 30)) && irq_handlers[30]) {
            TIMER0_EV_PENDING = 1;
            irq_handlers[30](1);
        }

        /* Check IRQ arrays 0-19 */
        for (int i = 0; i < 20; i++) {
            if (pending & (1 << i)) {
                volatile uint32_t *ev_pending =
                    (volatile uint32_t *)(irq_array_base[i] + IRQ_EV_PENDING);
                uint32_t events = *ev_pending;

                /* Clear the pending events */
                *ev_pending = events;

                /* Call handler if registered */
                if (irq_handlers[i])
                    irq_handlers[i](events);
            }
        }

        /* Re-enable machine external interrupts */
        __asm__ volatile ("csrs mie, %0" :: "r"(1 << 11));

    } else {
        /* Exception: light PB1 and halt */
        /* Inline to avoid pulling in GPIO driver dependency */
        volatile uint32_t *crafsel2 = (volatile uint32_t *)(IOX_BASE + 0x008);
        volatile uint32_t *gpiooe   = (volatile uint32_t *)(IOX_BASE + 0x14C);
        volatile uint32_t *gpioout  = (volatile uint32_t *)(IOX_BASE + 0x134);
        *crafsel2 &= ~(0x03 << 2);
        *gpiooe |= (1 << 1);
        *gpioout |= (1 << 1);
        for (volatile int s_halt = 0; s_halt == 0; s_halt = 0) { /* intentional halt */ }
    }
}
