/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C master driver for the Baochip-1x UDMA I2C.
 */

#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/udma.h"
#include "sevs_runtime.h"

static uint8_t  i2c_tx_buf[I2C_MAX_XFER]  __attribute__((section(".dma_buffers")));
static uint8_t  i2c_rx_buf[I2C_MAX_XFER]  __attribute__((section(".dma_buffers")));
static uint32_t i2c_cmd_buf[16] __attribute__((section(".dma_buffers")));

static const uintptr_t i2c_base[] = {
    UDMA_I2C0_BASE,
    UDMA_I2C1_BASE,
    UDMA_I2C2_BASE,
    UDMA_I2C3_BASE,
};

static const uint32_t i2c_cg[] = {
    UDMA_CG_I2C0,
    UDMA_CG_I2C1,
    UDMA_CG_I2C2,
    UDMA_CG_I2C3,
};

/* I2C-specific register offsets (beyond standard UDMA) */
#define I2C_STATUS_OFFSET  0x30
#define I2C_SETUP_OFFSET   0x34

/*
 * NOTE: i2c_divider and the three DMA buffers above are shared across
 * all four instances. Only one I2C instance may be in use at a time.
 * The Dabao board exposes I2C0 only, so this is not a limitation there.
 */
static uint32_t i2c_divider;

static inline volatile uint32_t *i2c_reg(uint inst, uint offset)
{
    return (volatile uint32_t *)(i2c_base[inst] + offset);
}

#define I2C_POLL_LIMIT 1000000

/*
 * NACK detection.
 *
 * A NACK does not stall the DMA channels. The RD_NACK command still
 * clocks out eight bits, reads 0xFF off the pulled-up bus, and every
 * channel goes idle normally, so a timeout cannot detect it. The
 * status lives in IRQ array 12 instead.
 *
 * The Daric datasheet gives I2C0.nack as IRQn 184 and I2C0.err as 188.
 * Measured on real hardware: a failed read sets bit 8 of this array's
 * EV_PENDING, and every transaction sets bit 12 (end of transfer).
 * Bit 8 is therefore I2C0's NACK flag, and instances 1 to 3 are
 * assumed to follow at bits 9, 10 and 11. Only instance 0 has been
 * verified on hardware.
 */
#define I2C_IRQ_ARRAY_BASE   0xE0008000UL
#define I2C_IRQ_EV_PENDING   (*(volatile uint32_t *)(I2C_IRQ_ARRAY_BASE + 0x10))
#define I2C_IRQ_EV_ENABLE    (*(volatile uint32_t *)(I2C_IRQ_ARRAY_BASE + 0x14))
#define I2C_NACK_BIT(inst)   (1u << (8 + (inst)))

/* Clear this instance's NACK flag before starting a transaction. */
static inline void i2c_nack_clear(uint inst)
{
    I2C_IRQ_EV_PENDING = I2C_NACK_BIT(inst);
}

/* True if the slave failed to acknowledge during the last transaction. */
static inline bool i2c_nack_seen(uint inst)
{
    return (I2C_IRQ_EV_PENDING & I2C_NACK_BIT(inst)) != 0;
}

/*
 * Wait for a DMA channel to go idle. Returns true on success, false if
 * the channel was still busy after I2C_POLL_LIMIT iterations. A stuck
 * channel usually means the bus is held low or the slave NACKed.
 */
static bool i2c_wait_idle(uint inst, uint offset)
{
    for (int s_poll = 0; s_poll < I2C_POLL_LIMIT; s_poll++) {
        if ((*i2c_reg(inst, offset) & UDMA_CFG_EN) == 0)
            return true;
    }
    return false;
}

/** @brief Initialize an I2C master instance at the specified clock speed.
 *  @param[in] instance I2C peripheral index (0-3).
 *  @param[in] speed_hz Desired SCL frequency in Hz.
 *  @req REQ-DABAO-I2C-0001 */
void i2c_init(uint instance, uint32_t speed_hz)
{
    SEVS_ASSERT(instance <= 3);
    SEVS_ASSERT(speed_hz > 0);

    if (instance > 3) return;

    uint32_t perclk = uart_get_perclk();
    i2c_divider = (perclk / (5 * speed_hz)) - 1;

    /* Open UDMA clock gate */
    REG32(UDMA_CTRL_BASE) |= i2c_cg[instance];

    /* Pin muxing for I2C0 (Dabao default) */
    if (instance == 0) {
        /* PB11=SCL, PB12=SDA, both AF1 */
        gpio_set_function(GPIO_PORT_B, 11, GPIO_FUNC_AF1);
        gpio_set_function(GPIO_PORT_B, 12, GPIO_FUNC_AF1);
        gpio_pull_up(GPIO_PORT_B, 11);
        gpio_pull_up(GPIO_PORT_B, 12);
    }

    /* Let the IRQ array latch NACK and error events for polling.
     * This does not raise a CPU interrupt unless MIM bit 12 is also
     * enabled via irq_enable(IRQ_I2C_ERR). */
    I2C_IRQ_EV_ENABLE |= 0xF00u;

    /* Reset the I2C peripheral */
    *i2c_reg(instance, I2C_SETUP_OFFSET) = 0x01;
    memory_fence();
    for (volatile int d = 0; d < 100; d++)
        __asm__ volatile ("nop");
    *i2c_reg(instance, I2C_SETUP_OFFSET) = 0x00;
    memory_fence();
}

/** @brief Write data to an I2C slave, blocking until complete.
 *  @param[in] instance I2C peripheral index (0-3).
 *  @param[in] addr 7-bit slave address.
 *  @param[in] data Transmit buffer.
 *  @param[in] len Byte count.
 *  @return 0 on success.
 *  @req REQ-DABAO-I2C-0002 */
int i2c_write_blocking(uint instance, uint8_t addr,
                       const uint8_t *data, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(data);
    if (len > I2C_MAX_XFER) return I2C_ERR_LEN;
    i2c_nack_clear(instance);
    for (uint32_t i = 0; i < len; i++)
        i2c_tx_buf[i] = data[i];

    uint32_t idx = 0;
    i2c_cmd_buf[idx++] = I2C_CMD_CFG(i2c_divider);
    i2c_cmd_buf[idx++] = I2C_CMD_START;
    i2c_cmd_buf[idx++] = I2C_CMD_WRB((addr << 1) & 0xFE);

    if (len > 0) {
        i2c_cmd_buf[idx++] = I2C_CMD_REPEAT(len);
        i2c_cmd_buf[idx++] = I2C_CMD_WR;
    }

    i2c_cmd_buf[idx++] = I2C_CMD_STOP;

    if (!i2c_wait_idle(instance, UDMA_TX_CFG_OFFSET)) return I2C_ERR_TIMEOUT;
    if (!i2c_wait_idle(instance, UDMA_CMD_CFG_OFFSET)) return I2C_ERR_TIMEOUT;

    if (len > 0) {
        *i2c_reg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_tx_buf;
        *i2c_reg(instance, UDMA_TX_SIZE_OFFSET)  = len;
        *i2c_reg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;
    }

    *i2c_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_cmd_buf;
    *i2c_reg(instance, UDMA_CMD_SIZE_OFFSET)  = idx * 4;
    *i2c_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
    memory_fence();

    if (!i2c_wait_idle(instance, UDMA_CMD_CFG_OFFSET)) return I2C_ERR_TIMEOUT;
    if (len > 0)
        if (!i2c_wait_idle(instance, UDMA_TX_CFG_OFFSET)) return I2C_ERR_TIMEOUT;

    if (i2c_nack_seen(instance)) {
        i2c_nack_clear(instance);
        return I2C_ERR_NACK;
    }
    return I2C_OK;
}

/** @brief Read data from an I2C slave, blocking until complete.
 *  @param[in] instance I2C peripheral index (0-3).
 *  @param[in] addr 7-bit slave address.
 *  @param[out] data Receive buffer.
 *  @param[in] len Byte count.
 *  @return 0 on success.
 *  @req REQ-DABAO-I2C-0003 */
int i2c_read_blocking(uint instance, uint8_t addr,
                      uint8_t *data, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(data);
    if (len > I2C_MAX_XFER) return I2C_ERR_LEN;
    i2c_nack_clear(instance);
    for (uint32_t i = 0; i < len; i++)
        i2c_rx_buf[i] = 0;

    uint32_t idx = 0;
    i2c_cmd_buf[idx++] = I2C_CMD_CFG(i2c_divider);
    i2c_cmd_buf[idx++] = I2C_CMD_START;
    i2c_cmd_buf[idx++] = I2C_CMD_WRB((addr << 1) | 0x01);

    if (len > 1) {
        i2c_cmd_buf[idx++] = I2C_CMD_REPEAT(len - 1);
        i2c_cmd_buf[idx++] = I2C_CMD_RDACK;
    }
    i2c_cmd_buf[idx++] = I2C_CMD_RDNACK;
    i2c_cmd_buf[idx++] = I2C_CMD_STOP;

    if (!i2c_wait_idle(instance, UDMA_RX_CFG_OFFSET)) return I2C_ERR_TIMEOUT;
    if (!i2c_wait_idle(instance, UDMA_CMD_CFG_OFFSET)) return I2C_ERR_TIMEOUT;

    *i2c_reg(instance, UDMA_RX_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_rx_buf;
    *i2c_reg(instance, UDMA_RX_SIZE_OFFSET)  = len;
    *i2c_reg(instance, UDMA_RX_CFG_OFFSET)   = UDMA_CFG_EN;

    *i2c_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_cmd_buf;
    *i2c_reg(instance, UDMA_CMD_SIZE_OFFSET)  = idx * 4;
    *i2c_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
    memory_fence();

    if (!i2c_wait_idle(instance, UDMA_CMD_CFG_OFFSET)) return I2C_ERR_TIMEOUT;
    if (!i2c_wait_idle(instance, UDMA_RX_CFG_OFFSET)) return I2C_ERR_TIMEOUT;

    for (uint32_t i = 0; i < len; i++)
        data[i] = i2c_rx_buf[i];

    if (i2c_nack_seen(instance)) {
        i2c_nack_clear(instance);
        return I2C_ERR_NACK;
    }
    return I2C_OK;
}

/** @brief Write then read from an I2C slave using repeated start.
 *  @param[in] instance I2C peripheral index (0-3).
 *  @param[in] addr 7-bit slave address.
 *  @param[in] src Transmit buffer (register address etc.).
 *  @param[in] src_len Transmit byte count.
 *  @param[out] dst Receive buffer.
 *  @param[out] dst_len Receive byte count.
 *  @return 0 on success.
 *  @req REQ-DABAO-I2C-0004 */
int i2c_write_read_blocking(uint instance, uint8_t addr,
                            const uint8_t *src, uint32_t src_len,
                            uint8_t *dst, uint32_t dst_len)
{
    SEVS_REQUIRE_NOT_NULL(src);
    SEVS_REQUIRE_NOT_NULL(dst);
    if (src_len > I2C_MAX_XFER || dst_len > I2C_MAX_XFER) return I2C_ERR_LEN;
    i2c_nack_clear(instance);
    for (uint32_t i = 0; i < src_len; i++)
        i2c_tx_buf[i] = src[i];
    for (uint32_t i = 0; i < dst_len; i++)
        i2c_rx_buf[i] = 0;

    uint32_t idx = 0;
    i2c_cmd_buf[idx++] = I2C_CMD_CFG(i2c_divider);

    /* Write phase */
    i2c_cmd_buf[idx++] = I2C_CMD_START;
    i2c_cmd_buf[idx++] = I2C_CMD_WRB((addr << 1) & 0xFE);
    if (src_len > 0) {
        i2c_cmd_buf[idx++] = I2C_CMD_REPEAT(src_len);
        i2c_cmd_buf[idx++] = I2C_CMD_WR;
    }

    /* Repeated start + read phase */
    i2c_cmd_buf[idx++] = I2C_CMD_START;
    i2c_cmd_buf[idx++] = I2C_CMD_WRB((addr << 1) | 0x01);
    if (dst_len > 1) {
        i2c_cmd_buf[idx++] = I2C_CMD_REPEAT(dst_len - 1);
        i2c_cmd_buf[idx++] = I2C_CMD_RDACK;
    }
    i2c_cmd_buf[idx++] = I2C_CMD_RDNACK;
    i2c_cmd_buf[idx++] = I2C_CMD_STOP;

    if (!i2c_wait_idle(instance, UDMA_TX_CFG_OFFSET)) return I2C_ERR_TIMEOUT;
    if (!i2c_wait_idle(instance, UDMA_RX_CFG_OFFSET)) return I2C_ERR_TIMEOUT;
    if (!i2c_wait_idle(instance, UDMA_CMD_CFG_OFFSET)) return I2C_ERR_TIMEOUT;

    /* Enqueue RX */
    *i2c_reg(instance, UDMA_RX_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_rx_buf;
    *i2c_reg(instance, UDMA_RX_SIZE_OFFSET)  = dst_len;
    *i2c_reg(instance, UDMA_RX_CFG_OFFSET)   = UDMA_CFG_EN;

    /* Enqueue TX */
    if (src_len > 0) {
        *i2c_reg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_tx_buf;
        *i2c_reg(instance, UDMA_TX_SIZE_OFFSET)  = src_len;
        *i2c_reg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;
    }

    /* Enqueue commands */
    *i2c_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_cmd_buf;
    *i2c_reg(instance, UDMA_CMD_SIZE_OFFSET)  = idx * 4;
    *i2c_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
    memory_fence();

    if (!i2c_wait_idle(instance, UDMA_CMD_CFG_OFFSET)) return I2C_ERR_TIMEOUT;
    if (src_len > 0)
        if (!i2c_wait_idle(instance, UDMA_TX_CFG_OFFSET)) return I2C_ERR_TIMEOUT;
    if (!i2c_wait_idle(instance, UDMA_RX_CFG_OFFSET)) return I2C_ERR_TIMEOUT;

    for (uint32_t i = 0; i < dst_len; i++)
        dst[i] = i2c_rx_buf[i];

    if (i2c_nack_seen(instance)) {
        i2c_nack_clear(instance);
        return I2C_ERR_NACK;
    }
    return I2C_OK;
}

/** @brief Start a non-blocking I2C write-then-read transaction.
 *  @param[in] instance I2C peripheral index (0-3).
 *  @param[in] addr 7-bit slave address.
 *  @param[in] src Transmit buffer.
 *  @param[in] src_len Transmit byte count.
 *  @param[in] rx_len Expected receive byte count.
 *  @req REQ-DABAO-I2C-0005 */
void i2c_write_read_async(uint instance, uint8_t addr,
                          const uint8_t *src, uint32_t src_len,
                          uint32_t rx_len)
{
    SEVS_REQUIRE_NOT_NULL(src);
    if (instance > 3) return;
    if (src_len > I2C_MAX_XFER) src_len = I2C_MAX_XFER;
    if (rx_len  > I2C_MAX_XFER) rx_len  = I2C_MAX_XFER;

    for (uint32_t i = 0; i < src_len; i++)
        i2c_tx_buf[i] = src[i];
    for (uint32_t i = 0; i < rx_len; i++)
        i2c_rx_buf[i] = 0;

    uint32_t idx = 0;
    i2c_cmd_buf[idx++] = I2C_CMD_CFG(i2c_divider);

    /* Write phase */
    i2c_cmd_buf[idx++] = I2C_CMD_START;
    i2c_cmd_buf[idx++] = I2C_CMD_WRB((addr << 1) & 0xFE);
    if (src_len > 0) {
        i2c_cmd_buf[idx++] = I2C_CMD_REPEAT(src_len);
        i2c_cmd_buf[idx++] = I2C_CMD_WR;
    }

    /* Read phase (if requested) */
    if (rx_len > 0) {
        i2c_cmd_buf[idx++] = I2C_CMD_START;
        i2c_cmd_buf[idx++] = I2C_CMD_WRB((addr << 1) | 0x01);
        if (rx_len > 1) {
            i2c_cmd_buf[idx++] = I2C_CMD_REPEAT(rx_len - 1);
            i2c_cmd_buf[idx++] = I2C_CMD_RDACK;
        }
        i2c_cmd_buf[idx++] = I2C_CMD_RDNACK;
    }

    i2c_cmd_buf[idx++] = I2C_CMD_STOP;

    /* Wait for channels to be free */
    (void)i2c_wait_idle(instance, UDMA_TX_CFG_OFFSET);
    (void)i2c_wait_idle(instance, UDMA_RX_CFG_OFFSET);
    (void)i2c_wait_idle(instance, UDMA_CMD_CFG_OFFSET);

    /* Enqueue RX */
    if (rx_len > 0) {
        *i2c_reg(instance, UDMA_RX_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_rx_buf;
        *i2c_reg(instance, UDMA_RX_SIZE_OFFSET)  = rx_len;
        *i2c_reg(instance, UDMA_RX_CFG_OFFSET)   = UDMA_CFG_EN;
    }

    /* Enqueue TX */
    if (src_len > 0) {
        *i2c_reg(instance, UDMA_TX_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_tx_buf;
        *i2c_reg(instance, UDMA_TX_SIZE_OFFSET)  = src_len;
        *i2c_reg(instance, UDMA_TX_CFG_OFFSET)   = UDMA_CFG_EN;
    }

    /* Enqueue commands - kicks off the transfer */
    *i2c_reg(instance, UDMA_CMD_SADDR_OFFSET) = (uint32_t)(uintptr_t)i2c_cmd_buf;
    *i2c_reg(instance, UDMA_CMD_SIZE_OFFSET)  = idx * 4;
    *i2c_reg(instance, UDMA_CMD_CFG_OFFSET)   = UDMA_CFG_EN | UDMA_CFG_SIZE_32;
    memory_fence();
}

/** @brief Check whether an async I2C transaction has completed.
 *  @param[in] instance I2C peripheral index (0-3).
 *  @return true if all DMA channels are idle.
 *  @req REQ-DABAO-I2C-0006 */
bool i2c_async_done(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    if (*i2c_reg(instance, UDMA_CMD_CFG_OFFSET) & UDMA_CFG_EN) return false;
    if (*i2c_reg(instance, UDMA_TX_CFG_OFFSET) & UDMA_CFG_EN) return false;
    if (*i2c_reg(instance, UDMA_RX_CFG_OFFSET) & UDMA_CFG_EN) return false;
    return true;
}

/** @brief Block until an async I2C transaction completes.
 *  @param[in] instance I2C peripheral index (0-3).
 *  @req REQ-DABAO-I2C-0007 */
void i2c_async_wait(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    for (int s_poll = 0; s_poll < I2C_POLL_LIMIT && !i2c_async_done(instance); s_poll++) { /* bounded poll */ }
}

/** @brief Copy received data from the I2C DMA buffer after an async transfer.
 *  @param[in] instance I2C peripheral index (unused, buffer is shared).
 *  @param[out] dst Destination buffer.
 *  @param[in] len Number of bytes to copy (max 64).
 *  @req REQ-DABAO-I2C-0008 */
void i2c_async_read(uint instance, uint8_t *dst, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(dst);
    (void)instance;
    if (len > I2C_MAX_XFER) len = I2C_MAX_XFER;
    for (uint32_t i = 0; i < len; i++)
        dst[i] = i2c_rx_buf[i];
}