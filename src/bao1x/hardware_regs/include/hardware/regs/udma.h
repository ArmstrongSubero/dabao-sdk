/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * UDMA subsystem register offsets.
 * Shared layout used by UART, SPI, I2C channels.
 */

#ifndef _HARDWARE_REGS_UDMA_H
#define _HARDWARE_REGS_UDMA_H

/* UDMA control register (clock gating) */
#define UDMA_CTRL_CG_OFFSET     0x000

/* Per-channel register offsets (same layout for UART, SPI, I2C) */
#define UDMA_RX_SADDR_OFFSET    0x00
#define UDMA_RX_SIZE_OFFSET     0x04
#define UDMA_RX_CFG_OFFSET      0x08
#define UDMA_TX_SADDR_OFFSET    0x10
#define UDMA_TX_SIZE_OFFSET     0x14
#define UDMA_TX_CFG_OFFSET      0x18

/* SPI/I2C command channel */
#define UDMA_CMD_SADDR_OFFSET   0x20
#define UDMA_CMD_SIZE_OFFSET    0x24
#define UDMA_CMD_CFG_OFFSET     0x28

/* UART-specific registers */
#define UDMA_UART_STATUS_OFFSET 0x20
#define UDMA_UART_SETUP_OFFSET  0x24
#define UDMA_UART_ERROR_OFFSET  0x28
#define UDMA_UART_IRQ_EN_OFFSET 0x2C
#define UDMA_UART_VALID_OFFSET  0x30
#define UDMA_UART_DATA_OFFSET   0x34

/* CFG register bits */
#define UDMA_CFG_EN             (1 << 4)
#define UDMA_CFG_PENDING        (1 << 5)
#define UDMA_CFG_CLR            (1 << 6)
#define UDMA_CFG_SIZE_8         (0 << 1)
#define UDMA_CFG_SIZE_16        (1 << 1)
#define UDMA_CFG_SIZE_32        (2 << 1)
#define UDMA_CFG_CONTINUOUS     (1 << 0)

/* UART STATUS bits */
#define UART_STATUS_TX_BUSY     (1 << 0)
#define UART_STATUS_RX_BUSY     (1 << 1)

/* UART SETUP: 8N1 with TX and RX enabled */
#define UART_SETUP_8N1  0x0316    /* was 0x0306 bit 4 enables poll mode for RX */

/* Clock gate bits (for UDMA_CTRL_CG) */
#define UDMA_CG_UART0           (1 << 0)
#define UDMA_CG_UART1           (1 << 1)
#define UDMA_CG_UART2           (1 << 2)
#define UDMA_CG_UART3           (1 << 3)
#define UDMA_CG_SPIM0           (1 << 4)
#define UDMA_CG_SPIM1           (1 << 5)
#define UDMA_CG_SPIM2           (1 << 6)
#define UDMA_CG_SPIM3           (1 << 7)
#define UDMA_CG_I2C0            (1 << 8)
#define UDMA_CG_I2C1            (1 << 9)
#define UDMA_CG_I2C2            (1 << 10)
#define UDMA_CG_I2C3            (1 << 11)
#define UDMA_CG_SDIO            (1 << 12)
#define UDMA_CG_ADC             (1 << 19)

#endif
