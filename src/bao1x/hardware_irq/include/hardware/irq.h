/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Interrupt dispatch system for the Baochip-1x.
 *
 * The VexRiscv has 20 IRQ arrays (irqarray0-19) plus Timer0 at
 * MIM/MIP bit 30. All sources arrive as mcause 0x8000000B (machine
 * external interrupt). This driver provides a per-array callback
 * table so users can register handlers without editing trap_dispatch.
 *
 * Each IRQ array aggregates up to 16 event sources. The handler
 * receives the EV_PENDING word and is responsible for clearing
 * the bits it services.
 *
 * Usage:
 *   irq_init();
 *   irq_set_handler(IRQ_TIMER0, my_timer_handler);
 *   irq_enable(IRQ_TIMER0);
 *   // ... handler gets called on interrupt
 */

#ifndef _HARDWARE_IRQ_H
#define _HARDWARE_IRQ_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IRQ source numbers.
 * 0-19 correspond to irqarray0-19 (MIM/MIP bits 0-19).
 * 30 is Timer0.
 */
#define IRQ_ARRAY0      0   /* MDMA, BIO */
#define IRQ_ARRAY1      1   /* USB */
#define IRQ_ARRAY2      2   /* QFC, MDMA, Mailbox, AO wakeup */
#define IRQ_ARRAY3      3   /* Crypto (TRNG, AES, PKE, Hash) */
#define IRQ_ARRAY4      4   /* Crypto dupes */
#define IRQ_UART        5   /* UART (all 4 UARTs) */
#define IRQ_SPI         6   /* SPI master (all 4 SPIMs) */
#define IRQ_I2C         7   /* I2C (all 4 I2Cs) */
#define IRQ_ARRAY8      8   /* SDIO, I2S, Camera, ADC, Filter */
#define IRQ_ARRAY9      9   /* SCIF, SPI slave, PWM */
#define IRQ_ARRAY10    10   /* IOX (GPIO), USB, SDDC, BIO */
#define IRQ_ARRAY11    11   /* I2S dupes */
#define IRQ_I2C_ERR    12   /* I2C NACK/error */
#define IRQ_ARRAY13    13   /* Bus errors, SCE errors, security */
#define IRQ_ARRAY14    14   /* UART2/3 dupes, TRNG done dupe */
#define IRQ_ARRAY15    15   /* Security sensor */
#define IRQ_ARRAY16    16   /* Camera, I2S, SPIM1/2, I2C0 dupes */
#define IRQ_ARRAY17    17   /* I2C1, BIO, QFC, ADC, IOX dupes */
#define IRQ_ARRAY18    18   /* BIO, I2C2, I2C NACK/err dupes */
#define IRQ_ARRAY19    19   /* Mailbox, BIO, SDIO dupes */
#define IRQ_TIMER0     30

/*
 * UART event bits within IRQ_UART (IRQARRAY5).
 * 4 events per UART: rx_done, tx_done, rx_char, error.
 */
#define UART0_EVT_RX        (1 << 0)
#define UART0_EVT_TX        (1 << 1)
#define UART0_EVT_RX_CHAR   (1 << 2)
#define UART0_EVT_ERR       (1 << 3)
#define UART1_EVT_RX        (1 << 4)
#define UART1_EVT_TX        (1 << 5)
#define UART2_EVT_RX        (1 << 8)
#define UART2_EVT_TX        (1 << 9)
#define UART2_EVT_RX_CHAR   (1 << 10)
#define UART2_EVT_ERR       (1 << 11)
#define UART3_EVT_RX        (1 << 12)
#define UART3_EVT_TX        (1 << 13)

/*
 * SPI event bits within IRQ_SPI (IRQARRAY6).
 * 4 events per SPI: rx_done, tx_done, cmd_done, eot.
 */
#define SPIM0_EVT_RX        (1 << 0)
#define SPIM0_EVT_TX        (1 << 1)
#define SPIM0_EVT_CMD       (1 << 2)
#define SPIM0_EVT_EOT       (1 << 3)
#define SPIM2_EVT_RX        (1 << 8)
#define SPIM2_EVT_TX        (1 << 9)
#define SPIM2_EVT_CMD       (1 << 10)
#define SPIM2_EVT_EOT       (1 << 11)

/*
 * I2C event bits within IRQ_I2C (IRQARRAY7).
 * 4 events per I2C: rx_done, tx_done, cmd_done, eot.
 */
#define I2C0_EVT_RX         (1 << 0)
#define I2C0_EVT_TX         (1 << 1)
#define I2C0_EVT_CMD        (1 << 2)
#define I2C0_EVT_EOT        (1 << 3)

/*
 * Handler function type.
 * For IRQ arrays 0-19: pending = the EV_PENDING value.
 * For Timer0: pending = 1.
 */
typedef void (*irq_handler_t)(uint32_t pending);

/*
 * Initialize the interrupt dispatch system.
 * Sets up mtvec, enables mstatus.MIE and mie.MEIE.
 * Call once at startup.
 */
void irq_init(void);

/*
 * Register a handler for an IRQ source.
 * irq_no: IRQ_ARRAY0..IRQ_ARRAY19, or IRQ_TIMER0.
 * handler: function to call, or NULL to unregister.
 */
void irq_set_handler(uint32_t irq_no, irq_handler_t handler);

/*
 * Enable an IRQ source in its array and in the VexRiscv MIM register.
 * For IRQ arrays: also enables all 16 event bits in EV_ENABLE.
 * For Timer0: enables Timer0 event generation.
 */
void irq_enable(uint32_t irq_no);

/*
 * Enable specific event bits within an IRQ array.
 * irq_no: IRQ_ARRAY0..IRQ_ARRAY19.
 * event_mask: which bits to enable in EV_ENABLE.
 */
void irq_enable_events(uint32_t irq_no, uint32_t event_mask);

/*
 * Disable an IRQ source in the VexRiscv MIM register.
 */
void irq_disable(uint32_t irq_no);

#ifdef __cplusplus
}
#endif

#endif
