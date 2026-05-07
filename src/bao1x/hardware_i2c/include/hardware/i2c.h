/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C master API for the Baochip-1x UDMA I2C.
 *
 * The chip has 4 I2C instances (0-3).
 * On the Dabao board, I2C0 is broken out:
 *   PB11 = I2C0_SCL (AF1)
 *   PB12 = I2C0_SDA (AF1)
 *
 * The UDMA I2C uses a command-based protocol similar to SPI:
 * a command buffer with START, WRITE, READ, STOP sequences
 * is fed through DMA.
 */

#ifndef _HARDWARE_I2C_H
#define _HARDWARE_I2C_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize an I2C instance at the specified clock speed.
 * For instance 0 (Dabao default), configures PB11/PB12 as AF1.
 */
void i2c_init(uint instance, uint32_t speed_hz);

/*
 * Write len bytes to a device.
 * Returns 0 on success.
 */
int i2c_write_blocking(uint instance, uint8_t addr,
                       const uint8_t *data, uint32_t len);

/*
 * Read len bytes from a device.
 * Returns 0 on success.
 */
int i2c_read_blocking(uint instance, uint8_t addr,
                      uint8_t *data, uint32_t len);

/*
 * Write then read (common register access pattern).
 * Sends src, then reads dst without releasing the bus.
 */
int i2c_write_read_blocking(uint instance, uint8_t addr,
                            const uint8_t *src, uint32_t src_len,
                            uint8_t *dst, uint32_t dst_len);

/*
 * Start an asynchronous write-then-read I2C transfer.
 * Builds the command sequence, kicks UDMA, returns immediately.
 * Use i2c_async_done() to check completion, i2c_async_read() to get data.
 * Only one async transfer per instance at a time.
 */
void i2c_write_read_async(uint instance, uint8_t addr,
                          const uint8_t *src, uint32_t src_len,
                          uint32_t rx_len);

/* Returns true when the last async transfer has completed. */
bool i2c_async_done(uint instance);

/* Block until the last async transfer completes. */
void i2c_async_wait(uint instance);

/* Copy received data from internal RX buffer after async transfer. */
void i2c_async_read(uint instance, uint8_t *dst, uint32_t len);

/* I2C command opcodes (for building custom sequences) */
#define I2C_CMD_SHIFT       28
#define I2C_CMD_CFG_ID      0x0E
#define I2C_CMD_START_ID    0x00
#define I2C_CMD_STOP_ID     0x02
#define I2C_CMD_RD_ACK_ID   0x04
#define I2C_CMD_RD_NACK_ID  0x06
#define I2C_CMD_WR_ID       0x08
#define I2C_CMD_WRB_ID      0x07
#define I2C_CMD_RPT_ID      0x0C
#define I2C_CMD_EOT_ID      0x09

#define I2C_CMD_CFG(div)    ((I2C_CMD_CFG_ID << I2C_CMD_SHIFT) | ((div) & 0xFFFF))
#define I2C_CMD_START       (I2C_CMD_START_ID << I2C_CMD_SHIFT)
#define I2C_CMD_STOP        (I2C_CMD_STOP_ID << I2C_CMD_SHIFT)
#define I2C_CMD_RDACK       (I2C_CMD_RD_ACK_ID << I2C_CMD_SHIFT)
#define I2C_CMD_RDNACK      (I2C_CMD_RD_NACK_ID << I2C_CMD_SHIFT)
#define I2C_CMD_WR          (I2C_CMD_WR_ID << I2C_CMD_SHIFT)
#define I2C_CMD_WRB(b)      ((I2C_CMD_WRB_ID << I2C_CMD_SHIFT) | ((b) & 0xFF))
#define I2C_CMD_REPEAT(n)   ((I2C_CMD_RPT_ID << I2C_CMD_SHIFT) | ((n) & 0xFFFF))

#ifdef __cplusplus
}
#endif

#endif
