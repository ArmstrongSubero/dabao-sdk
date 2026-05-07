/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI master driver for the Baochip-1x UDMA SPI.
 * Extracted from working main_mcp41010.c, verified on Dabao hardware.
 */

#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/udma.h"
#include "sevs_runtime.h"

/* DMA buffers in IFRAM */
static uint8_t spi_tx_buf[256] __attribute__((section(".dma_buffers")));
static uint8_t spi_rx_buf[256] __attribute__((section(".dma_buffers")));
static uint32_t spi_cmd_buf[8] __attribute__((section(".dma_buffers")));

/* Instance base addresses */
static const uintptr_t spi_base[] = {
    UDMA_SPIM0_BASE,
    UDMA_SPIM1_BASE,
    UDMA_SPIM2_BASE,
    UDMA_SPIM3_BASE,
};

/* UDMA clock gate bits */
static const uint32_t spi_cg[] = {
    UDMA_CG_SPIM0,
    UDMA_CG_SPIM1,
    UDMA_CG_SPIM2,
    UDMA_CG_SPIM3,
};

static inline volatile uint32_t *spi_reg(uint inst, uint offset)
{
    return (volatile uint32_t *)(spi_base[inst] + offset);
}

/** @brief Initialize an SPI master instance and configure its pins.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @req REQ-DABAO-SPI-0001 */
void spi_init(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    if (instance > 3) return;

    /* Open UDMA clock gate */
    REG32(UDMA_CTRL_BASE) |= spi_cg[instance];

    /* Pin muxing for SPI2 (Dabao default) */
    if (instance == 2) {
        /* PC0=CLK, PC1=MOSI, PC2=MISO: all AF2 */
        gpio_set_function(GPIO_PORT_C, 0, GPIO_FUNC_AF2);
        gpio_set_function(GPIO_PORT_C, 1, GPIO_FUNC_AF2);
        gpio_set_function(GPIO_PORT_C, 2, GPIO_FUNC_AF2);

        gpio_set_dir(GPIO_PORT_C, 0, true);   /* CLK output */
        gpio_set_dir(GPIO_PORT_C, 1, true);   /* MOSI output */
        gpio_set_dir(GPIO_PORT_C, 2, false);  /* MISO input */
        gpio_pull_up(GPIO_PORT_C, 2);         /* MISO pull-up */
    }
}

/** @brief Transmit data over SPI, blocking until complete.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @param[in] data Transmit buffer.
 *  @param[in] len Byte count (1-256).
 *  @param[in] clkdiv Clock divider value.
 *  @req REQ-DABAO-SPI-0002 */
void spi_write_blocking(uint instance, const uint8_t *data, uint32_t len,
                        uint8_t clkdiv)
{
    SEVS_REQUIRE_NOT_NULL(data);
    if (len == 0 || len > 256) return;

    for (uint32_t i = 0; i < len; i++)
        spi_tx_buf[i] = data[i];

    spi_cmd_buf[0] = SPI_CMD_CFG(clkdiv, 0, 0);
    spi_cmd_buf[1] = SPI_CMD_SOT(0);
    spi_cmd_buf[2] = SPI_CMD_TX_DATA(len, 8, 0);
    spi_cmd_buf[3] = SPI_CMD_EOT(1, 0);

    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_TX_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }

    *spi_reg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_tx_buf;
    *spi_reg(instance, UDMA_TX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;

    *spi_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_cmd_buf;
    *spi_reg(instance, UDMA_CMD_SIZE_OFFSET)  = 4 * 4;
    *spi_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
    memory_fence();

    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_TX_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
}

/** @brief Full-duplex SPI transfer, blocking until complete.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @param[in] tx Transmit buffer or NULL for clock-only.
 *  @param[in,out] rx Receive buffer or NULL to discard.
 *  @param[in] len Byte count (1-256).
 *  @param[in] clkdiv Clock divider value.
 *  @req REQ-DABAO-SPI-0003 */
void spi_write_read_blocking(uint instance, const uint8_t *tx, uint8_t *rx,
                             uint32_t len, uint8_t clkdiv)
{
    SEVS_REQUIRE_NOT_NULL(tx);
    SEVS_REQUIRE_NOT_NULL(rx);
    if (len == 0 || len > 256) return;

    /* Fill TX buffer */
    if (tx) {
        for (uint32_t i = 0; i < len; i++)
            spi_tx_buf[i] = tx[i];
    } else {
        for (uint32_t i = 0; i < len; i++)
            spi_tx_buf[i] = 0x00;
    }

    /* Clear RX buffer */
    for (uint32_t i = 0; i < len; i++)
        spi_rx_buf[i] = 0;

    /* Full-duplex command sequence */
    spi_cmd_buf[0] = SPI_CMD_CFG(clkdiv, 0, 0);
    spi_cmd_buf[1] = SPI_CMD_SOT(0);
    spi_cmd_buf[2] = SPI_CMD_FULL_DUPLEX(len, 8, 0);
    spi_cmd_buf[3] = SPI_CMD_EOT(1, 0);

    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_TX_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_RX_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }

    /* Enqueue RX first */
    *spi_reg(instance, UDMA_RX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_rx_buf;
    *spi_reg(instance, UDMA_RX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_RX_CFG_OFFSET)   = UDMA_CFG_EN;

    /* Enqueue TX */
    *spi_reg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_tx_buf;
    *spi_reg(instance, UDMA_TX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;

    /* Enqueue commands */
    *spi_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_cmd_buf;
    *spi_reg(instance, UDMA_CMD_SIZE_OFFSET)  = 4 * 4;
    *spi_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
    memory_fence();

    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_TX_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_RX_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }

    /* Copy RX data out of IFRAM */
    if (rx) {
        for (uint32_t i = 0; i < len; i++)
            rx[i] = spi_rx_buf[i];
    }
}

/** @brief Start a non-blocking full-duplex SPI transfer.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @param[in] tx Transmit buffer or NULL.
 *  @param[in] len Byte count (1-256).
 *  @param[in] clkdiv Clock divider value.
 *  @req REQ-DABAO-SPI-0004 */
void spi_write_read_async(uint instance, const uint8_t *tx, uint32_t len,
                          uint8_t clkdiv)
{
    SEVS_REQUIRE_NOT_NULL(tx);
    if (len == 0 || len > 256) return;

    /* Fill TX buffer */
    if (tx) {
        for (uint32_t i = 0; i < len; i++)
            spi_tx_buf[i] = tx[i];
    } else {
        for (uint32_t i = 0; i < len; i++)
            spi_tx_buf[i] = 0x00;
    }

    /* Clear RX buffer */
    for (uint32_t i = 0; i < len; i++)
        spi_rx_buf[i] = 0;

    /* Full-duplex command sequence */
    spi_cmd_buf[0] = SPI_CMD_CFG(clkdiv, 0, 0);
    spi_cmd_buf[1] = SPI_CMD_SOT(0);
    spi_cmd_buf[2] = SPI_CMD_FULL_DUPLEX(len, 8, 0);
    spi_cmd_buf[3] = SPI_CMD_EOT(1, 0);

    /* Wait for channels to be free */
    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_TX_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_RX_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
    for (int s_poll = 0; s_poll < 1000000 && (*spi_reg(instance, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }

    /* Enqueue RX first */
    *spi_reg(instance, UDMA_RX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_rx_buf;
    *spi_reg(instance, UDMA_RX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_RX_CFG_OFFSET)   = UDMA_CFG_EN;

    /* Enqueue TX */
    *spi_reg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_tx_buf;
    *spi_reg(instance, UDMA_TX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;

    /* Enqueue commands - kicks off the transfer */
    *spi_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_cmd_buf;
    *spi_reg(instance, UDMA_CMD_SIZE_OFFSET)  = 4 * 4;
    *spi_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
    memory_fence();

    /* Return immediately - transfer runs in background */
}

/** @brief Check whether an async SPI transfer has completed.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @return true if all DMA channels are idle.
 *  @req REQ-DABAO-SPI-0005 */
bool spi_async_done(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    /* All three channels (CMD, TX, RX) must be finished */
    if (*spi_reg(instance, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN) return false;
    if (*spi_reg(instance, UDMA_TX_CFG_OFFSET) & UDMA_CFG_EN) return false;
    if (*spi_reg(instance, UDMA_RX_CFG_OFFSET) & UDMA_CFG_EN) return false;
    return true;
}

/** @brief Block until an async SPI transfer completes.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @req REQ-DABAO-SPI-0006 */
void spi_async_wait(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    for (int s_poll = 0; s_poll < 1000000 && !spi_async_done(instance); s_poll++) { /* bounded poll */ }
}

/** @brief Copy received data from the SPI DMA buffer after an async transfer.
 *  @param[in] instance SPI peripheral index (unused, buffer is shared).
 *  @param[out] rx Destination buffer.
 *  @param[in] len Number of bytes to copy.
 *  @req REQ-DABAO-SPI-0007 */
void spi_async_read(uint instance, uint8_t *rx, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(rx);
    (void)instance;
    if (len > 256) len = 256;
    for (uint32_t i = 0; i < len; i++)
        rx[i] = spi_rx_buf[i];
}
