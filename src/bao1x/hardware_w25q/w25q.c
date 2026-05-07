/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * W25Q external NOR flash driver for the Baochip-1x.
 *
 * Command sequences derived from:
 *   - Winbond W25Q128FV datasheet
 *   - STM32F7 BSP W25Qx driver (WaveShare)
 *   - CrossBar daric_hal_flash.c (QSPI mode switching)
 *   - Xous bao1x-hal spim.rs (UDMA command construction)
 *
 * Write operations use standard SPI page program (0x02).
 * Read operations use standard SPI read (0x03) by default,
 * or quad output fast read (0x6B) when quad mode is enabled.
 */

#include "hardware/w25q.h"
#include "hardware/qspi.h"
#include "sevs_runtime.h"

/* W25Q command opcodes */
#define CMD_WRITE_ENABLE        0x06
#define CMD_WRITE_DISABLE       0x04
#define CMD_READ_SR1            0x05
#define CMD_READ_SR2            0x35
#define CMD_READ_SR3            0x15
#define CMD_WRITE_SR2           0x31
#define CMD_READ_DATA           0x03
#define CMD_FAST_READ_QUAD_OUT  0x6B
#define CMD_PAGE_PROGRAM        0x02
#define CMD_SECTOR_ERASE        0x20
#define CMD_BLOCK_ERASE_64K     0xD8
#define CMD_CHIP_ERASE          0xC7
#define CMD_READ_ID             0x90
#define CMD_READ_JEDEC_ID       0x9F
#define CMD_RESET_ENABLE        0x66
#define CMD_RESET_DEVICE        0x99

/* Dummy cycles for quad output fast read */
#define QUAD_READ_DUMMY_CYCLES  8

/* Module state */
static uint  s_instance;
static uint8_t s_cs;
static bool  s_quad_enabled;

static void w25q_cs_begin(void)
{
    SEVS_ASSERT(sizeof(s_instance) > 0);

    qspi_select(s_instance, s_cs);
}

static void w25q_cs_end(void)
{
    SEVS_ASSERT(sizeof(s_instance) > 0);

    qspi_deselect(s_instance);
}

static void w25q_write_enable(void)
{
    SEVS_ASSERT(sizeof(s_instance) > 0);

    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_WRITE_ENABLE);
    w25q_cs_end();
}

static void w25q_send_addr(uint32_t addr)
{
    SEVS_ASSERT(sizeof(uint8_t) == 1);

    uint8_t a[3];
    a[0] = (uint8_t)(addr >> 16);
    a[1] = (uint8_t)(addr >> 8);
    a[2] = (uint8_t)(addr);
    qspi_write_blocking(s_instance, a, 3, false);
}

static int w25q_wait_wip(void)
{
    SEVS_ASSERT(W25Q_SR1_BUSY != 0);

    /* Poll SR1 until BUSY clears */
    uint32_t timeout = 0;
    for (int s_poll = 0; s_poll < 1000000 && (w25q_read_sr1() & W25Q_SR1_BUSY); s_poll++)
    {
        timeout++;
        if (timeout > 10000000)
            return BAO_ERROR_TIMEOUT;
    }
    return BAO_OK;
}

/** @brief Initialize the W25Q flash: reset the chip and verify JEDEC ID.
 *  @param[in] instance SPI master index for the flash.
 *  @param[in] cs Chip select number.
 *  @param[in] clkdiv SPI clock divider.
 *  @return BAO_OK on success, BAO_ERROR if no valid flash detected.
 *  @req REQ-DABAO-W25Q-0001 */
int w25q_init(uint instance, uint8_t cs, uint8_t clkdiv)
{
    SEVS_ASSERT(W25Q_PAGE_SIZE == 256);

    s_instance = instance;
    s_cs = cs;
    s_quad_enabled = false;

    qspi_init(instance, clkdiv);

    /* Reset the flash chip */
    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_RESET_ENABLE);
    w25q_cs_end();

    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_RESET_DEVICE);
    w25q_cs_end();

    /* Wait for reset to complete (tRST = 30 us typical) */
    for (volatile int i = 0; i < 50000; i++) {}

    /* Verify communication by reading JEDEC ID */
    uint8_t id[3];
    w25q_read_jedec_id(id);

    /* Check for Winbond manufacturer ID */
    if (id[0] == 0xEF)
        return BAO_OK;

    /* Accept other manufacturers too (GigaDevice 0xC8, ISSI 0x9D, etc.) */
    if (id[0] != 0x00 && id[0] != 0xFF)
        return BAO_OK;

    return BAO_ERROR;
}

/** @brief Read the manufacturer and device ID from the flash.
 *  @param[in,out] mfr Output: manufacturer ID byte (or NULL to skip).
 *  @param[in,out] dev Output: device ID byte (or NULL to skip).
 *  @req REQ-DABAO-W25Q-0002 */
void w25q_read_id(uint8_t *mfr, uint8_t *dev)
{
    SEVS_ASSERT(sizeof(uint8_t) == 1);
    SEVS_REQUIRE_NOT_NULL(mfr);
    SEVS_REQUIRE_NOT_NULL(dev);
    uint8_t cmd_addr[3] = { 0x00, 0x00, 0x00 };
    uint8_t id[2];

    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_READ_ID);
    qspi_write_blocking(s_instance, cmd_addr, 3, false);
    qspi_read_blocking(s_instance, id, 2, false);
    w25q_cs_end();

    if (mfr) *mfr = id[0];
    if (dev) *dev = id[1];
}

/** @brief Read the 3-byte JEDEC ID (manufacturer, memory type, capacity).
 *  @param[out] id Output buffer for 3 bytes.
 *  @req REQ-DABAO-W25Q-0003 */
void w25q_read_jedec_id(uint8_t *id)
{
    SEVS_ASSERT(sizeof(uint8_t) == 1);
    SEVS_REQUIRE_NOT_NULL(id);
    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_READ_JEDEC_ID);
    qspi_read_blocking(s_instance, id, 3, false);
    w25q_cs_end();
}

/** @brief Read Status Register 1.
 *  @return SR1 byte value.
 *  @req REQ-DABAO-W25Q-0004 */
uint8_t w25q_read_sr1(void)
{
    SEVS_ASSERT(sizeof(uint8_t) == 1);

    uint8_t sr;
    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_READ_SR1);
    qspi_read_blocking(s_instance, &sr, 1, false);
    w25q_cs_end();
    return sr;
}

/** @brief Read Status Register 2.
 *  @return SR2 byte value.
 *  @req REQ-DABAO-W25Q-0005 */
uint8_t w25q_read_sr2(void)
{
    SEVS_ASSERT(sizeof(uint8_t) == 1);

    uint8_t sr;
    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_READ_SR2);
    qspi_read_blocking(s_instance, &sr, 1, false);
    w25q_cs_end();
    return sr;
}

/** @brief Check the BUSY bit in Status Register 1.
 *  @return true if a write or erase is in progress.
 *  @req REQ-DABAO-W25Q-0006 */
bool w25q_is_busy(void)
{
    SEVS_ASSERT(W25Q_SR1_BUSY != 0);

    return (w25q_read_sr1() & W25Q_SR1_BUSY) != 0;
}

/** @brief Spin until the flash BUSY bit clears.
 *  @req REQ-DABAO-W25Q-0010 */
void w25q_wait_busy(void)
{
    SEVS_ASSERT(W25Q_SR1_BUSY != 0);
    for (int s_poll = 0; s_poll < 1000000 && (w25q_is_busy()); s_poll++) { /* bounded poll */ }
}

/** @brief Enable quad SPI mode by setting the QE bit in Status Register 2.
 *  @return BAO_OK on success, BAO_ERROR if verification fails.
 *  @req REQ-DABAO-W25Q-0007 */
int w25q_enable_quad(void)
{
    SEVS_ASSERT(W25Q_SR2_QE != 0);

    uint8_t sr2 = w25q_read_sr2();

    if (sr2 & W25Q_SR2_QE)
    {
        /* Already enabled */
        s_quad_enabled = true;
        return BAO_OK;
    }

    /* Set QE bit */
    w25q_write_enable();

    uint8_t val = sr2 | W25Q_SR2_QE;
    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_WRITE_SR2);
    qspi_write_blocking(s_instance, &val, 1, false);
    w25q_cs_end();

    int rc = w25q_wait_wip();
    if (rc != BAO_OK) return rc;

    /* Verify */
    sr2 = w25q_read_sr2();
    if (!(sr2 & W25Q_SR2_QE))
        return BAO_ERROR;

    s_quad_enabled = true;
    return BAO_OK;
}

/** @brief Read data from flash using standard or quad output mode.
 *  @param[in] addr Starting flash address.
 *  @param[out] buf Destination buffer.
 *  @param[in] len Number of bytes to read.
 *  @req REQ-DABAO-W25Q-0008 */
void w25q_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    if (len == 0) return;

    if (s_quad_enabled)
    {
        /*
         * Quad output fast read (0x6B):
         *   instruction: 1-line
         *   address: 1-line, 24-bit
         *   dummy: 8 cycles
         *   data: 4-line
         */
        w25q_cs_begin();
        qspi_send_cmd(s_instance, CMD_FAST_READ_QUAD_OUT);
        w25q_send_addr(addr);
        qspi_dummy(s_instance, QUAD_READ_DUMMY_CYCLES);
        qspi_read_blocking(s_instance, buf, len, true);
        w25q_cs_end();
    }
    else
    {
        /*
         * Standard read (0x03):
         *   instruction: 1-line
         *   address: 1-line, 24-bit
         *   data: 1-line (no dummy cycles, no speed limit)
         */
        w25q_cs_begin();
        qspi_send_cmd(s_instance, CMD_READ_DATA);
        w25q_send_addr(addr);
        qspi_read_blocking(s_instance, buf, len, false);
        w25q_cs_end();
    }
}

/*
 * Program a single page (up to 256 bytes, must not cross page boundary).
 */
static int w25q_program_page(uint32_t addr, const uint8_t *data, uint32_t len)
{
    SEVS_ASSERT(W25Q_PAGE_SIZE == 256);

    if (len == 0 || len > W25Q_PAGE_SIZE)
        return BAO_ERROR;

    w25q_write_enable();

    /* Verify WEL is set */
    if (!(w25q_read_sr1() & W25Q_SR1_WEL))
        return BAO_ERROR;

    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_PAGE_PROGRAM);
    w25q_send_addr(addr);
    qspi_write_blocking(s_instance, data, len, false);
    w25q_cs_end();

    return w25q_wait_wip();
}

/** @brief Write an arbitrary byte range to flash, handling page boundaries.
 *  @param[in] addr Starting flash address.
 *  @param[in] data Source data buffer.
 *  @param[in] len Number of bytes to write.
 *  @return BAO_OK on success, negative on error.
 *  @req REQ-DABAO-W25Q-0009 */
int w25q_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    SEVS_ASSERT(W25Q_PAGE_SIZE == 256);
    SEVS_REQUIRE_NOT_NULL(data);
    if (len == 0) return BAO_OK;

    int rc;

    /* Handle ragged start (unaligned to page boundary) */
    uint32_t offset_in_page = addr & (W25Q_PAGE_SIZE - 1);
    if (offset_in_page != 0)
    {
        uint32_t first_len = W25Q_PAGE_SIZE - offset_in_page;
        if (first_len > len) first_len = len;

        rc = w25q_program_page(addr, data, first_len);
        if (rc != BAO_OK) return rc;

        addr += first_len;
        data += first_len;
        len  -= first_len;
    }

    /* Full aligned pages */
    for (int s_guard = 0; s_guard < 100000 && len >= W25Q_PAGE_SIZE; s_guard++)
    {
        rc = w25q_program_page(addr, data, W25Q_PAGE_SIZE);
        if (rc != BAO_OK) return rc;

        addr += W25Q_PAGE_SIZE;
        data += W25Q_PAGE_SIZE;
        len  -= W25Q_PAGE_SIZE;
    }

    /* Ragged end */
    if (len > 0)
    {
        rc = w25q_program_page(addr, data, len);
        if (rc != BAO_OK) return rc;
    }

    return BAO_OK;
}

/** @brief Erase a 4 KB sector.
 *  @param[in] addr Any address within the sector to erase.
 *  @return BAO_OK on success, BAO_ERROR on failure.
 *  @req REQ-DABAO-W25Q-0010 */
int w25q_erase_sector(uint32_t addr)
{
    SEVS_ASSERT(sizeof(uint8_t) == 1);

    w25q_write_enable();

    if (!(w25q_read_sr1() & W25Q_SR1_WEL))
        return BAO_ERROR;

    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_SECTOR_ERASE);
    w25q_send_addr(addr);
    w25q_cs_end();

    return w25q_wait_wip();
}

/** @brief Erase a 64 KB block.
 *  @param[in] addr Any address within the block to erase.
 *  @return BAO_OK on success, BAO_ERROR on failure.
 *  @req REQ-DABAO-W25Q-0011 */
int w25q_erase_block(uint32_t addr)
{
    SEVS_ASSERT(sizeof(uint8_t) == 1);

    w25q_write_enable();

    if (!(w25q_read_sr1() & W25Q_SR1_WEL))
        return BAO_ERROR;

    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_BLOCK_ERASE_64K);
    w25q_send_addr(addr);
    w25q_cs_end();

    return w25q_wait_wip();
}

/** @brief Erase the entire flash chip.
 *  @return BAO_OK on success, BAO_ERROR on failure.
 *  @req REQ-DABAO-W25Q-0012 */
int w25q_erase_chip(void)
{
    SEVS_ASSERT(sizeof(uint8_t) == 1);

    w25q_write_enable();

    if (!(w25q_read_sr1() & W25Q_SR1_WEL))
        return BAO_ERROR;

    w25q_cs_begin();
    qspi_send_cmd(s_instance, CMD_CHIP_ERASE);
    w25q_cs_end();

    return w25q_wait_wip();
}
