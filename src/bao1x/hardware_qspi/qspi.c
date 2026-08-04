/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * QSPI driver for the Baochip-1x UDMA SPI master.
 *
 * Uses the UDMA command-based protocol with the full command set
 * including SEND_CMD (opcode 0x2), DUMMY (opcode 0x4), and
 * quad-mode TX/RX via bit 27 of the command word.
 *
 * Pin assignments derived from the CrossBar pinmap HAL:
 *   SPIM1: PC7=D0, PC8=D1, PC9=D2, PC10=D3, PC11=CLK,
 *          PC12=CS0, PC13=CS1 (all AF1)
 */

#include "hardware/qspi.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/udma.h"
#include "sevs_runtime.h"

/* DMA buffers in IFRAM (separate from the standard SPI driver) */
static uint8_t  qspi_tx_buf[256]  __attribute__((section(".dma_buffers")));
static uint8_t  qspi_rx_buf[256]  __attribute__((section(".dma_buffers")));
static uint32_t qspi_cmd_buf[16]  __attribute__((section(".dma_buffers")));

static const uintptr_t spim_base[] = {
    UDMA_SPIM0_BASE,
    UDMA_SPIM1_BASE,
    UDMA_SPIM2_BASE,
    UDMA_SPIM3_BASE,
};

static const uint32_t spim_cg[] = {
    UDMA_CG_SPIM0,
    UDMA_CG_SPIM1,
    UDMA_CG_SPIM2,
    UDMA_CG_SPIM3,
};

/*
 * Per-instance timeout flag.
 *
 * Every wait loop in this driver is bounded rather than infinite, which is
 * deliberate: a hung device must not lock the CPU. The cost is that an
 * exhausted loop used to return normally, so a caller could not tell a
 * completed transfer from an abandoned one.
 *
 * Any bounded loop that exhausts now sets this flag. Call qspi_timed_out()
 * after a transfer to check, and qspi_clear_timeout() to reset it.
 */
static bool qspi_timeout[4];

static inline volatile uint32_t *qreg(uint inst, uint offset)
{
    return (volatile uint32_t *)(spim_base[inst] + offset);
}

static void qspi_wait_cmd(uint inst)
{
    if (inst > 3) return;

    int s_poll;
    for (s_poll = 0; s_poll < 1000000; s_poll++) {
        if (!(*qreg(inst, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN))
            return;
    }
    qspi_timeout[inst] = true;
}

static void qspi_wait_tx(uint inst)
{
    if (inst > 3) return;

    int s_poll;
    for (s_poll = 0; s_poll < 1000000; s_poll++) {
        if (!(*qreg(inst, UDMA_TX_CFG_OFFSET) & UDMA_CFG_EN))
            return;
    }
    qspi_timeout[inst] = true;
}

static void qspi_wait_rx(uint inst)
{
    if (inst > 3) return;

    int s_poll;
    for (s_poll = 0; s_poll < 1000000; s_poll++) {
        if (!(*qreg(inst, UDMA_RX_CFG_OFFSET) & UDMA_CFG_EN))
            return;
    }
    qspi_timeout[inst] = true;
}

/** @brief Initialize a QSPI master instance, configure pins and clock divider.
 *  @param[in] instance SPI master index (0-3).
 *  @param[in] clkdiv Clock divider value.
 *  @req REQ-DABAO-QSPI-0001 */
void qspi_init(uint instance, uint8_t clkdiv)
{
    SEVS_ASSERT(instance <= 3);

    if (instance > 3) return;

    qspi_timeout[instance] = false;

    /* Open UDMA clock gate */
    REG32(UDMA_CTRL_BASE) |= spim_cg[instance];

    /* Pin muxing for SPIM1 (Dabao QSPI port) */
    if (instance == 1)
    {
        /* All SPIM1 pins are AF1 on port C */
        gpio_set_function(GPIO_PORT_C,  7, GPIO_FUNC_AF1);  /* D0 */
        gpio_set_function(GPIO_PORT_C,  8, GPIO_FUNC_AF1);  /* D1 */
        gpio_set_function(GPIO_PORT_C,  9, GPIO_FUNC_AF1);  /* D2 */
        gpio_set_function(GPIO_PORT_C, 10, GPIO_FUNC_AF1);  /* D3 */
        gpio_set_function(GPIO_PORT_C, 11, GPIO_FUNC_AF1);  /* CLK */
        gpio_set_function(GPIO_PORT_C, 12, GPIO_FUNC_AF1);  /* CS0 */
        gpio_set_function(GPIO_PORT_C, 13, GPIO_FUNC_AF1);  /* CS1 */

        /* Data lines: D0 and CLK are outputs, D1-D3 are bidirectional */
        gpio_set_dir(GPIO_PORT_C, 11, true);   /* CLK output */
        gpio_set_dir(GPIO_PORT_C,  7, true);   /* D0 output */
        gpio_set_dir(GPIO_PORT_C,  8, false);  /* D1 input (MISO in 1-line mode) */
        gpio_set_dir(GPIO_PORT_C,  9, false);  /* D2 input */
        gpio_set_dir(GPIO_PORT_C, 10, false);  /* D3 input */
        gpio_set_dir(GPIO_PORT_C, 12, true);   /* CS0 output */
        gpio_set_dir(GPIO_PORT_C, 13, true);   /* CS1 output */

        /* Pull-ups on data inputs */
        gpio_pull_up(GPIO_PORT_C,  8);
        gpio_pull_up(GPIO_PORT_C,  9);
        gpio_pull_up(GPIO_PORT_C, 10);
    }

    /* Send initial CFG command to set clock divider */
    qspi_cmd_buf[0] = QSPI_CMD_CFG(clkdiv, 0, 0);

    qspi_wait_cmd(instance);
    *qreg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)qspi_cmd_buf;
    *qreg(instance, UDMA_CMD_SIZE_OFFSET)  = 1 * 4;
    *qreg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
    memory_fence();
    qspi_wait_cmd(instance);
}

/** @brief Send a sequence of raw UDMA SPI commands.
 *  @param[in] instance SPI master index (0-3).
 *  @param[in] cmds Array of 32-bit UDMA command words.
 *  @param[in] ncmds Number of commands (1-16).
 *  @req REQ-DABAO-QSPI-0002 */
void qspi_exec_cmds(uint instance, const uint32_t *cmds, uint32_t ncmds)
{
    SEVS_REQUIRE_NOT_NULL(cmds);
    if (instance > 3 || ncmds == 0 || ncmds > 16) return;

    qspi_wait_cmd(instance);

    for (uint32_t i = 0; i < ncmds; i++)
        qspi_cmd_buf[i] = cmds[i];

    *qreg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)qspi_cmd_buf;
    *qreg(instance, UDMA_CMD_SIZE_OFFSET)  = ncmds * 4;
    *qreg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
    memory_fence();
}

/** @brief Assert a chip select line.
 *  @param[in] instance SPI master index (0-3).
 *  @param[in] cs Chip select number (0-1).
 *  @req REQ-DABAO-QSPI-0003 */
void qspi_select(uint instance, uint8_t cs)
{
    SEVS_ASSERT(instance <= 3);

    uint32_t cmds[1] = {
        QSPI_CMD_SOT(cs)
    };
    qspi_exec_cmds(instance, cmds, 1);
    qspi_wait_cmd(instance);
}

/** @brief De-assert the chip select line.
 *  @param[in] instance SPI master index (0-3).
 *  @req REQ-DABAO-QSPI-0004 */
void qspi_deselect(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    uint32_t cmds[1] = {
        QSPI_CMD_EOT(0, 0)
    };
    qspi_exec_cmds(instance, cmds, 1);
    qspi_wait_cmd(instance);
}

/** @brief Send an 8-bit command byte over single-line SPI.
 *  @param[in] instance SPI master index (0-3).
 *  @param[in] cmd Command byte.
 *  @req REQ-DABAO-QSPI-0005 */
void qspi_send_cmd(uint instance, uint8_t cmd)
{
    SEVS_ASSERT(instance <= 3);

    uint32_t cmds[1] = {
        QSPI_CMD_SEND_CMD(false, 8, (uint16_t)cmd)
    };
    qspi_exec_cmds(instance, cmds, 1);
    qspi_wait_cmd(instance);
}

/** @brief Send an 8-bit command byte over quad SPI lines.
 *  @param[in] instance SPI master index (0-3).
 *  @param[in] cmd Command byte.
 *  @req REQ-DABAO-QSPI-0006 */
void qspi_send_cmd_quad(uint instance, uint8_t cmd)
{
    SEVS_ASSERT(instance <= 3);

    uint32_t cmds[1] = {
        QSPI_CMD_SEND_CMD(true, 8, (uint16_t)cmd)
    };
    qspi_exec_cmds(instance, cmds, 1);
    qspi_wait_cmd(instance);
}

/** @brief Clock out dummy cycles without transferring data.
 *  @param[in] instance SPI master index (0-3).
 *  @param[in] cycles Number of dummy clock cycles (1-31).
 *  @req REQ-DABAO-QSPI-0007 */
void qspi_dummy(uint instance, uint8_t cycles)
{
    SEVS_ASSERT(instance <= 3);

    if (cycles == 0 || cycles > 31) return;

    uint32_t cmds[1] = {
        QSPI_CMD_DUMMY(cycles)
    };
    qspi_exec_cmds(instance, cmds, 1);
    qspi_wait_cmd(instance);
}

/** @brief Transmit data over QSPI, blocking until complete.
 *  @param[in] instance SPI master index (0-3).
 *  @param[in] data Transmit buffer.
 *  @param[in] len Byte count.
 *  @param[in] quad true for quad-width transfer.
 *  @req REQ-DABAO-QSPI-0008 */
void qspi_write_blocking(uint instance, const uint8_t *data,
                          uint32_t len, bool quad)
{
    SEVS_REQUIRE_NOT_NULL(data);
    if (instance > 3 || len == 0) return;

    int s_guard;
    for (s_guard = 0; s_guard < 100000 && (len > 0); s_guard++)
    {
        uint32_t chunk = (len > 256) ? 256 : len;

        qspi_wait_tx(instance);
        qspi_wait_cmd(instance);

        for (uint32_t i = 0; i < chunk; i++)
            qspi_tx_buf[i] = data[i];

        /* Enqueue TX data */
        *qreg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)qspi_tx_buf;
        *qreg(instance, UDMA_TX_SIZE_OFFSET)  = chunk;
        *qreg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;

        /* Enqueue TX_DATA command */
        qspi_cmd_buf[0] = QSPI_CMD_TX_DATA(chunk, 8, quad, 0);
        *qreg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)qspi_cmd_buf;
        *qreg(instance, UDMA_CMD_SIZE_OFFSET)  = 1 * 4;
        *qreg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
        memory_fence();

        qspi_wait_cmd(instance);
        qspi_wait_tx(instance);

        data += chunk;
        len  -= chunk;
    }

    if (len > 0)
        qspi_timeout[instance] = true;   /* guard exhausted, transfer incomplete */
}

/** @brief Receive data over QSPI, blocking until complete.
 *  @param[in] instance SPI master index (0-3).
 *  @param[out] data Receive buffer.
 *  @param[in] len Byte count.
 *  @param[in] quad true for quad-width transfer.
 *  @req REQ-DABAO-QSPI-0009 */
void qspi_read_blocking(uint instance, uint8_t *data,
                         uint32_t len, bool quad)
{
    SEVS_REQUIRE_NOT_NULL(data);
    if (instance > 3 || len == 0) return;

    int s_guard;
    for (s_guard = 0; s_guard < 100000 && (len > 0); s_guard++)
    {
        uint32_t chunk = (len > 256) ? 256 : len;

        qspi_wait_rx(instance);
        qspi_wait_cmd(instance);

        /* Clear RX buffer */
        for (uint32_t i = 0; i < chunk; i++)
            qspi_rx_buf[i] = 0;

        /* Enqueue RX buffer */
        *qreg(instance, UDMA_RX_SADDR_OFFSET) = (uint32_t)(uintptr_t)qspi_rx_buf;
        *qreg(instance, UDMA_RX_SIZE_OFFSET)  = chunk;
        *qreg(instance, UDMA_RX_CFG_OFFSET)   = UDMA_CFG_EN;

        /* Enqueue RX_DATA command */
        qspi_cmd_buf[0] = QSPI_CMD_RX_DATA(chunk, 8, quad, 0);
        *qreg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)qspi_cmd_buf;
        *qreg(instance, UDMA_CMD_SIZE_OFFSET)  = 1 * 4;
        *qreg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
        memory_fence();

        qspi_wait_cmd(instance);
        qspi_wait_rx(instance);

        /* Copy RX data out of IFRAM */
        for (uint32_t i = 0; i < chunk; i++)
            data[i] = qspi_rx_buf[i];

        data += chunk;
        len  -= chunk;
    }

    if (len > 0)
        qspi_timeout[instance] = true;   /* guard exhausted, transfer incomplete */
}

/** @brief Check whether a QSPI transfer is in progress.
 *  @param[in] instance SPI master index (0-3).
 *  @return true if any DMA channel is still active.
 *  @req REQ-DABAO-QSPI-0010 */
bool qspi_busy(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    if (instance > 3) return false;
    if (*qreg(instance, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN) return true;
    if (*qreg(instance, UDMA_TX_CFG_OFFSET)  & UDMA_CFG_EN) return true;
    if (*qreg(instance, UDMA_RX_CFG_OFFSET)  & UDMA_CFG_EN) return true;
    return false;
}

/** @brief Block until all QSPI DMA channels are idle.
 *  @param[in] instance SPI master index (0-3).
 *  @req REQ-DABAO-QSPI-0011 */
void qspi_wait(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    if (instance > 3) return;

    int s_poll;
    for (s_poll = 0; s_poll < 1000000; s_poll++) {
        if (!qspi_busy(instance))
            return;
    }
    qspi_timeout[instance] = true;
}

/** @brief Report whether any bounded wait on this instance has expired.
 *
 *  Every wait loop in this driver is bounded so a hung device cannot lock
 *  the CPU. When a loop exhausts, the transfer did not complete and this
 *  returns true. The flag is sticky: it stays set until cleared by
 *  qspi_clear_timeout() or by a fresh qspi_init().
 *
 *  @param[in] instance SPI master index (0-3).
 *  @return true if a bounded wait expired since the last clear.
 *  @req REQ-DABAO-QSPI-0012 */
bool qspi_timed_out(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    if (instance > 3) return false;
    return qspi_timeout[instance];
}

/** @brief Clear the timeout flag for an instance.
 *  @param[in] instance SPI master index (0-3).
 *  @req REQ-DABAO-QSPI-0013 */
void qspi_clear_timeout(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    if (instance > 3) return;
    qspi_timeout[instance] = false;
}
