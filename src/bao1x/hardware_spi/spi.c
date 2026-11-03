/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI master driver for the Baochip-1x UDMA SPI.
 */

#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/udma.h"
#include "sevs_runtime.h"

/*
 * EOT event generation.
 *
 * SPI_CMD_EOT carries an event bit and this driver passed 1 in every
 * command list, so every transfer raised a UDMA SPI event on the SoC
 * event bus. Nothing here waits on that event: all three functions
 * poll the channel enable bits instead.
 *
 * evc.sv maps event bus lines onto RRC one way counters, and rrc.sv
 * turns a selected, enabled, masked event into a program cycle on a
 * one way counter in RRAM. Those counters are irreversible. Whether
 * any of that is armed on a stock boot cannot be checked from user
 * code, because the registers that would say are in the boot chain
 * region and reading them is itself a tamper event.
 *
 */
#ifndef SPI_EOT_EVT
#define SPI_EOT_EVT 0
#endif

static uint8_t spi_tx_buf[256] __attribute__((section(".dma_buffers")));
static uint8_t spi_rx_buf[256] __attribute__((section(".dma_buffers")));
static uint32_t spi_cmd_buf[8] __attribute__((section(".dma_buffers")));

/* SPI mode per instance, packed as (cpol << 1) | cpha. 0 is mode 0, the
 * reset value, so a caller that never sets a mode gets the same command
 * words as before. */
static uint8_t spi_mode[4];

static const uintptr_t spi_base[] = {
    UDMA_SPIM0_BASE,
    UDMA_SPIM1_BASE,
    UDMA_SPIM2_BASE,
    UDMA_SPIM3_BASE,
};

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

#define SPI_POLL_LIMIT 1000000

/*
 * The three DMA buffers are shared by all four instances, so before any
 * of them is refilled the instance that last used them must be idle. A
 * blocking call waits for its own transfer, so this only ever has to
 * wait for an async transfer that is still running.
 */
static uint s_buf_owner = 0xFFu;

static bool spi_channels_idle(uint inst)
{
    return (*spi_reg(inst, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN) == 0
        && (*spi_reg(inst, UDMA_TX_CFG_OFFSET)  & UDMA_CFG_EN) == 0
        && (*spi_reg(inst, UDMA_RX_CFG_OFFSET)  & UDMA_CFG_EN) == 0;
}

static bool spi_wait_idle(uint inst)
{
    for (int s_poll = 0; s_poll < SPI_POLL_LIMIT; s_poll++) {
        if (spi_channels_idle(inst))
            return true;
    }
    return false;
}

static bool spi_buffers_free(uint inst)
{
    if (s_buf_owner <= 3u && !spi_wait_idle(s_buf_owner))
        return false;
    return spi_wait_idle(inst);
}

/** @brief Initialize an SPI master instance and configure its pins.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @req REQ-DABAO-SPI-0001 */
void spi_init(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    if (instance > 3) return;

    spi_mode[instance] = 0;

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

/** @brief Select SPI mode for later transfers on this instance.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @param[in] cpol Clock idle level (0 low, 1 high).
 *  @param[in] cpha Sample edge (0 first, 1 second). */
void spi_set_mode(uint instance, uint8_t cpol, uint8_t cpha)
{
    if (instance > 3) return;
    spi_mode[instance] = (uint8_t)(((cpol & 1U) << 1) | (cpha & 1U));
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
    SEVS_ASSERT(instance <= 3);
    if (instance > 3 || data == NULL || len == 0 || len > 256) return;
    if (!spi_buffers_free(instance)) return;
    s_buf_owner = instance;

    for (uint32_t i = 0; i < len; i++)
        spi_tx_buf[i] = data[i];

    spi_cmd_buf[0] = SPI_CMD_CFG(clkdiv, (spi_mode[instance] >> 1) & 1, spi_mode[instance] & 1);
    spi_cmd_buf[1] = SPI_CMD_SOT(0);
    spi_cmd_buf[2] = SPI_CMD_TX_DATA(len, 8, 0);
    spi_cmd_buf[3] = SPI_CMD_EOT(SPI_EOT_EVT, 0);
    memory_fence();

    *spi_reg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_tx_buf;
    *spi_reg(instance, UDMA_TX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;

    *spi_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_cmd_buf;
    *spi_reg(instance, UDMA_CMD_SIZE_OFFSET)  = 4 * 4;
    *spi_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;

    (void)spi_wait_idle(instance);
}

/** @brief Full-duplex SPI transfer, blocking until complete.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @param[in] tx Transmit buffer or NULL to send zeros.
 *  @param[in,out] rx Receive buffer or NULL to discard.
 *  @param[in] len Byte count (1-256).
 *  @param[in] clkdiv Clock divider value.
 *  @req REQ-DABAO-SPI-0003 */
void spi_write_read_blocking(uint instance, const uint8_t *tx, uint8_t *rx,
                             uint32_t len, uint8_t clkdiv)
{
    SEVS_ASSERT(instance <= 3);
    if (instance > 3 || len == 0 || len > 256) return;
    if (!spi_buffers_free(instance)) return;
    s_buf_owner = instance;

    for (uint32_t i = 0; i < len; i++) {
        spi_tx_buf[i] = tx ? tx[i] : 0x00;
        spi_rx_buf[i] = 0;
    }

    spi_cmd_buf[0] = SPI_CMD_CFG(clkdiv, (spi_mode[instance] >> 1) & 1, spi_mode[instance] & 1);
    spi_cmd_buf[1] = SPI_CMD_SOT(0);
    spi_cmd_buf[2] = SPI_CMD_FULL_DUPLEX(len, 8, 0);
    spi_cmd_buf[3] = SPI_CMD_EOT(SPI_EOT_EVT, 0);
    memory_fence();

    *spi_reg(instance, UDMA_RX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_rx_buf;
    *spi_reg(instance, UDMA_RX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_RX_CFG_OFFSET)   = UDMA_CFG_EN;

    *spi_reg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_tx_buf;
    *spi_reg(instance, UDMA_TX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;

    *spi_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_cmd_buf;
    *spi_reg(instance, UDMA_CMD_SIZE_OFFSET)  = 4 * 4;
    *spi_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;

    if (!spi_wait_idle(instance)) return;

    memory_fence();
    if (rx) {
        for (uint32_t i = 0; i < len; i++)
            rx[i] = spi_rx_buf[i];
    }
}

/** @brief Start a non-blocking full-duplex SPI transfer.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @param[in] tx Transmit buffer or NULL to send zeros.
 *  @param[in] len Byte count (1-256).
 *  @param[in] clkdiv Clock divider value.
 *  @req REQ-DABAO-SPI-0004 */
void spi_write_read_async(uint instance, const uint8_t *tx, uint32_t len,
                          uint8_t clkdiv)
{
    SEVS_ASSERT(instance <= 3);
    if (instance > 3 || len == 0 || len > 256) return;
    if (!spi_buffers_free(instance)) return;
    s_buf_owner = instance;

    for (uint32_t i = 0; i < len; i++) {
        spi_tx_buf[i] = tx ? tx[i] : 0x00;
        spi_rx_buf[i] = 0;
    }

    spi_cmd_buf[0] = SPI_CMD_CFG(clkdiv, (spi_mode[instance] >> 1) & 1, spi_mode[instance] & 1);
    spi_cmd_buf[1] = SPI_CMD_SOT(0);
    spi_cmd_buf[2] = SPI_CMD_FULL_DUPLEX(len, 8, 0);
    spi_cmd_buf[3] = SPI_CMD_EOT(SPI_EOT_EVT, 0);
    memory_fence();

    *spi_reg(instance, UDMA_RX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_rx_buf;
    *spi_reg(instance, UDMA_RX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_RX_CFG_OFFSET)   = UDMA_CFG_EN;

    *spi_reg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_tx_buf;
    *spi_reg(instance, UDMA_TX_SIZE_OFFSET)  = len;
    *spi_reg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;

    *spi_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)spi_cmd_buf;
    *spi_reg(instance, UDMA_CMD_SIZE_OFFSET)  = 4 * 4;
    *spi_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
}

/** @brief Check whether an async SPI transfer has completed.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @return true if all DMA channels are idle.
 *  @req REQ-DABAO-SPI-0005 */
bool spi_async_done(uint instance)
{
    SEVS_ASSERT(instance <= 3);
    if (instance > 3) return true;
    return spi_channels_idle(instance);
}

/** @brief Block until an async SPI transfer completes.
 *  @param[in] instance SPI peripheral index (0-3).
 *  @req REQ-DABAO-SPI-0006 */
void spi_async_wait(uint instance)
{
    SEVS_ASSERT(instance <= 3);
    if (instance > 3) return;
    (void)spi_wait_idle(instance);
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
    if (rx == NULL) return;
    if (len > 256) len = 256;
    memory_fence();
    for (uint32_t i = 0; i < len; i++)
        rx[i] = spi_rx_buf[i];
}
