/*
 * diskio.c - FatFs disk I/O layer for Baochip-1x / Dabao Board
 *
 * Ported from PIC16F1718 version by Armstrong Subero.
 * Uses UDMA SPI2 full-duplex transfers instead of byte-at-a-time MSSP.
 *
 * SPI2 (SPIM2) at 0x50107000, clock gate bit 6
 * Pins: PC0=CLK, PC1=MOSI, PC2=MISO, PC3=CS (all AF2, hardware CS via SOT/EOT)
 * Init at 400 kHz, data at 12.4 MHz (clkdiv=3)
 */

#include <stdint.h>
#include "diskio.h"

/* ---- Hardware registers (must match main.c) ---- */

#define IOX_BASE        0x5012F000UL
#define IOX_CRAFSEL4    (*(volatile uint32_t *)(IOX_BASE + 0x010))
#define IOX_GPIOOE_PC   (*(volatile uint32_t *)(IOX_BASE + 0x150))
#define IOX_GPIOPU_PC   (*(volatile uint32_t *)(IOX_BASE + 0x168))

#define UDMA_CTRL_CG    (*(volatile uint32_t *)(0x50100000UL))
#define UDMA_CFG_EN     (1 << 4)

#define SPI2_BASE       0x50107000UL
#define SPI2_RX_SADDR   (*(volatile uint32_t *)(SPI2_BASE + 0x00))
#define SPI2_RX_SIZE    (*(volatile uint32_t *)(SPI2_BASE + 0x04))
#define SPI2_RX_CFG     (*(volatile uint32_t *)(SPI2_BASE + 0x08))
#define SPI2_TX_SADDR   (*(volatile uint32_t *)(SPI2_BASE + 0x10))
#define SPI2_TX_SIZE    (*(volatile uint32_t *)(SPI2_BASE + 0x14))
#define SPI2_TX_CFG     (*(volatile uint32_t *)(SPI2_BASE + 0x18))
#define SPI2_CMD_SADDR  (*(volatile uint32_t *)(SPI2_BASE + 0x20))
#define SPI2_CMD_SIZE   (*(volatile uint32_t *)(SPI2_BASE + 0x24))
#define SPI2_CMD_CFG    (*(volatile uint32_t *)(SPI2_BASE + 0x28))

/* SPI command IDs from RTL */
#define SPI_CMD_CFG     (0x0 << 28)
#define SPI_CMD_SOT     (0x1 << 28)
#define SPI_CMD_TX_DATA (0x6 << 28)
#define SPI_CMD_EOT     (0x9 << 28)
#define SPI_CMD_FULL    (0xC << 28)

/* ---- DMA buffers in IFRAM ---- */

static uint8_t  sd_tx_buf[2600] __attribute__((section(".dma_buffers"))) __attribute__((aligned(4)));
static uint8_t  sd_rx_buf[2600] __attribute__((section(".dma_buffers"))) __attribute__((aligned(4)));
static uint32_t sd_cmd_buf[8]   __attribute__((section(".dma_buffers"))) __attribute__((aligned(4)));

/* ---- State ---- */

static uint32_t sd_clkdiv = 122;  /* start at 400 kHz */
static uint8_t  CardType = 0;
static DSTATUS  Stat = STA_NOINIT;

/* ---- CRC7 ---- */

static uint8_t sd_crc7(const uint8_t *data, uint32_t len)
{
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t d = data[i];
        for (int b = 0; b < 8; b++) {
            crc <<= 1;
            if ((d & 0x80) ^ (crc & 0x80))
                crc ^= 0x09;
            d <<= 1;
        }
    }
    return (crc << 1) | 1;
}

/* ---- Low-level SPI transfer ---- */

static void spi_wait(void)
{
    while (SPI2_CMD_CFG & UDMA_CFG_EN) {}
    while (SPI2_TX_CFG & UDMA_CFG_EN) {}
    while (SPI2_RX_CFG & UDMA_CFG_EN) {}
}

/* Full-duplex SPI: send tx_len bytes from sd_tx_buf, receive into sd_rx_buf */
static void spi_full_duplex(uint32_t len)
{
    uint32_t ci = 0;
    sd_cmd_buf[ci++] = SPI_CMD_CFG | (sd_clkdiv & 0xFF);
    sd_cmd_buf[ci++] = SPI_CMD_SOT | (0 << 0);
    sd_cmd_buf[ci++] = SPI_CMD_FULL | ((8 - 1) << 16) | ((len - 1) & 0xFFFF);
    sd_cmd_buf[ci++] = SPI_CMD_EOT | (1 << 1);

    spi_wait();

    SPI2_TX_SADDR = (uint32_t)sd_tx_buf;
    SPI2_TX_SIZE = len;
    SPI2_TX_CFG = UDMA_CFG_EN;

    SPI2_RX_SADDR = (uint32_t)sd_rx_buf;
    SPI2_RX_SIZE = len;
    SPI2_RX_CFG = UDMA_CFG_EN;

    SPI2_CMD_SADDR = (uint32_t)sd_cmd_buf;
    SPI2_CMD_SIZE = ci * 4;
    SPI2_CMD_CFG = UDMA_CFG_EN | (2 << 1);
    __asm__ volatile ("fence" ::: "memory");

    spi_wait();
}

/* TX-only without SOT (for init clocks with CS high) */
static void spi_tx_no_cs(uint32_t len)
{
    uint32_t ci = 0;
    sd_cmd_buf[ci++] = SPI_CMD_CFG | (sd_clkdiv & 0xFF);
    sd_cmd_buf[ci++] = SPI_CMD_TX_DATA | ((8 - 1) << 16) | ((len - 1) & 0xFFFF);

    spi_wait();

    SPI2_TX_SADDR = (uint32_t)sd_tx_buf;
    SPI2_TX_SIZE = len;
    SPI2_TX_CFG = UDMA_CFG_EN;

    SPI2_CMD_SADDR = (uint32_t)sd_cmd_buf;
    SPI2_CMD_SIZE = ci * 4;
    SPI2_CMD_CFG = UDMA_CFG_EN | (2 << 1);
    __asm__ volatile ("fence" ::: "memory");

    spi_wait();
}

/* ---- SD command helpers ---- */

/* Send SD command, return R1 response. CS asserted/deasserted per transaction. */
static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t frame[6];
    uint8_t real_cmd = cmd & 0x7F;

    /* Handle ACMD (bit 7 set means send CMD55 first) */
    if (cmd & 0x80) {
        uint8_t r1 = sd_send_cmd(0x40 | 55, 0);
        if (r1 > 1) return r1;
    }

    frame[0] = 0x40 | real_cmd;
    frame[1] = (arg >> 24) & 0xFF;
    frame[2] = (arg >> 16) & 0xFF;
    frame[3] = (arg >> 8)  & 0xFF;
    frame[4] = arg & 0xFF;
    frame[5] = sd_crc7(frame, 5);

    /* 1 dummy + 6 cmd + 10 response = 17 bytes */
    sd_tx_buf[0] = 0xFF;
    for (int i = 0; i < 6; i++) sd_tx_buf[i+1] = frame[i];
    for (int i = 7; i < 17; i++) sd_tx_buf[i] = 0xFF;

    spi_full_duplex(17);

    /* Find R1 (first non-0xFF byte after command) */
    uint8_t r1 = 0xFF;
    for (int i = 7; i < 17; i++) {
        if (sd_rx_buf[i] != 0xFF) { r1 = sd_rx_buf[i]; break; }
    }
    return r1;
}

/* Send command and read 4-byte trailing response (for CMD8, CMD58) */
static uint8_t sd_send_cmd_r7(uint8_t cmd, uint32_t arg, uint8_t *resp4)
{
    uint8_t frame[6];
    frame[0] = 0x40 | cmd;
    frame[1] = (arg >> 24) & 0xFF;
    frame[2] = (arg >> 16) & 0xFF;
    frame[3] = (arg >> 8)  & 0xFF;
    frame[4] = arg & 0xFF;
    frame[5] = sd_crc7(frame, 5);

    sd_tx_buf[0] = 0xFF;
    for (int i = 0; i < 6; i++) sd_tx_buf[i+1] = frame[i];
    for (int i = 7; i < 27; i++) sd_tx_buf[i] = 0xFF;

    spi_full_duplex(27);

    uint8_t r1 = 0xFF;
    int r1_pos = -1;
    for (int i = 7; i < 27; i++) {
        if (sd_rx_buf[i] != 0xFF) { r1 = sd_rx_buf[i]; r1_pos = i; break; }
    }

    if (r1_pos >= 0 && r1_pos + 4 < 27 && resp4) {
        for (int i = 0; i < 4; i++) resp4[i] = sd_rx_buf[r1_pos + 1 + i];
    }
    return r1;
}

/* ---- SPI2 hardware init ---- */

static void spi2_hw_init(uint32_t clkdiv_val)
{
    sd_clkdiv = clkdiv_val;

    uint32_t af = IOX_CRAFSEL4;
    af &= ~(0x03 << (0*2)); af |= (0x02 << (0*2));  /* PC0 = AF2 CLK */
    af &= ~(0x03 << (1*2)); af |= (0x02 << (1*2));  /* PC1 = AF2 MOSI */
    af &= ~(0x03 << (2*2)); af |= (0x02 << (2*2));  /* PC2 = AF2 MISO */
    af &= ~(0x03 << (3*2)); af |= (0x02 << (3*2));  /* PC3 = AF2 CS */
    IOX_CRAFSEL4 = af;

    IOX_GPIOOE_PC |= (1 << 0) | (1 << 1) | (1 << 3);
    IOX_GPIOOE_PC &= ~(1 << 2);
    IOX_GPIOPU_PC |= (1 << 2);

    UDMA_CTRL_CG |= (1 << 6);
    __asm__ volatile ("fence" ::: "memory");
}

/* ---- Delay helper ---- */

static void dly_100us(void)
{
    for (volatile int d = 0; d < 5000; d++) __asm__ volatile ("nop");
}

/* ================================================================== */
/*                     FatFs Required API                               */
/* ================================================================== */

DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return Stat;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;

    uint8_t n, ty, ocr[4];
    uint16_t tmr;

    /* Init SPI at 400 kHz */
    spi2_hw_init(122);

    /* 80 clocks with CS high (no SOT) */
    for (int i = 0; i < 10; i++) sd_tx_buf[i] = 0xFF;
    spi_tx_no_cs(10);

    ty = 0;

    if (sd_send_cmd(0, 0) == 1) {
        /* Card in idle state */

        if (sd_send_cmd_r7(8, 0x1AA, ocr) == 1) {
            /* SD v2 */
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                /* ACMD41 with HCS */
                for (tmr = 10000; tmr; tmr--) {
                    uint8_t r = sd_send_cmd(0x80 | 41, 1UL << 30);
                    if (r == 0) break;
                    dly_100us();
                }

                if (tmr) {
                    /* Read OCR for CCS bit */
                    if (sd_send_cmd_r7(58, 0, ocr) == 0) {
                        ty = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
                    }
                }
            }
        } else {
            /* SD v1 or MMC */
            uint8_t cmd;
            if (sd_send_cmd(0x80 | 41, 0) <= 1) {
                ty = CT_SD1;
                cmd = 0x80 | 41;  /* ACMD41 */
            } else {
                ty = CT_MMC;
                cmd = 0x40 | 1;   /* CMD1 */
            }

            for (tmr = 10000; tmr && sd_send_cmd(cmd, 0); tmr--)
                dly_100us();

            if (!tmr || sd_send_cmd(0x40 | 16, 512) != 0)
                ty = 0;
        }
    }

    CardType = ty;

    /* Switch to 12.4 MHz for data transfers */
    sd_clkdiv = 3;

    if (ty) {
        Stat &= (DSTATUS)~STA_NOINIT;
    } else {
        Stat = STA_NOINIT;
    }

    return Stat;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    (void)pdrv;

    if (!count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & CT_BLOCK)) sector *= 512;

    while (count--) {
        /* Build CMD17 + data read as single transaction */
        uint8_t frame[6];
        frame[0] = 0x40 | 17;
        frame[1] = (sector >> 24) & 0xFF;
        frame[2] = (sector >> 16) & 0xFF;
        frame[3] = (sector >> 8) & 0xFF;
        frame[4] = sector & 0xFF;
        frame[5] = sd_crc7(frame, 5);

        /* 1 dummy + 6 cmd + ~2050 receive (token wait + 512 + 2 CRC) */
        uint32_t total = 2560;
        sd_tx_buf[0] = 0xFF;
        for (int i = 0; i < 6; i++) sd_tx_buf[i+1] = frame[i];
        for (uint32_t i = 7; i < total; i++) sd_tx_buf[i] = 0xFF;

        spi_full_duplex(total);

        /* Find R1 */
        uint8_t r1 = 0xFF;
        int r1_pos = -1;
        for (int i = 7; i < 20; i++) {
            if (sd_rx_buf[i] != 0xFF) { r1 = sd_rx_buf[i]; r1_pos = i; break; }
        }
        if (r1 != 0x00) return RES_ERROR;

        /* Find 0xFE data token */
        int token_pos = -1;
        for (int i = r1_pos + 1; i < (int)total; i++) {
            if (sd_rx_buf[i] == 0xFE) { token_pos = i; break; }
        }
        if (token_pos < 0 || (token_pos + 513) >= (int)total) return RES_ERROR;

        /* Copy 512 bytes of sector data */
        for (int i = 0; i < 512; i++) {
            buff[i] = sd_rx_buf[token_pos + 1 + i];
        }

        sector++;
        buff += 512;
    }

    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    (void)pdrv;

    if (!count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & CT_BLOCK)) sector *= 512;

    while (count--) {
        /* Build CMD24 + data as single transaction:
         * 1 dummy + 6 cmd + 10 response + 1 gap + 1 token + 512 data + 2 CRC + 64 busy = 597
         * Keep it under buffer size */
        uint8_t frame[6];
        frame[0] = 0x40 | 24;
        frame[1] = (sector >> 24) & 0xFF;
        frame[2] = (sector >> 16) & 0xFF;
        frame[3] = (sector >> 8) & 0xFF;
        frame[4] = sector & 0xFF;
        frame[5] = sd_crc7(frame, 5);

        uint32_t pos = 0;
        /* Dummy + CMD24 */
        sd_tx_buf[pos++] = 0xFF;
        for (int i = 0; i < 6; i++) sd_tx_buf[pos++] = frame[i];
        /* Response wait (10 bytes of 0xFF) */
        for (int i = 0; i < 10; i++) sd_tx_buf[pos++] = 0xFF;
        /* Gap byte before data */
        sd_tx_buf[pos++] = 0xFF;
        /* Data token */
        sd_tx_buf[pos++] = 0xFE;
        /* 512 bytes of data */
        for (int i = 0; i < 512; i++) sd_tx_buf[pos++] = buff[i];
        /* CRC dummy */
        sd_tx_buf[pos++] = 0xFF;
        sd_tx_buf[pos++] = 0xFF;
        /* Space for data response + initial busy wait */
        for (int i = 0; i < 64; i++) sd_tx_buf[pos++] = 0xFF;

        spi_full_duplex(pos);

        /* Find R1 response to CMD24 */
        uint8_t r1 = 0xFF;
        for (int i = 7; i < 17; i++) {
            if (sd_rx_buf[i] != 0xFF) { r1 = sd_rx_buf[i]; break; }
        }
        if (r1 != 0x00) return RES_ERROR;

        /* Find data response token after the 512 data bytes
         * It's at approximately position 7+10+1+1+512+2 = 533 */
        uint8_t resp = 0xFF;
        for (uint32_t i = 530; i < pos; i++) {
            if (sd_rx_buf[i] != 0xFF) { resp = sd_rx_buf[i]; break; }
        }
        if ((resp & 0x1F) != 0x05) return RES_ERROR;

        /* Wait for card to finish programming (busy = 0x00 on MISO) */
        int busy_done = 0;
        for (int attempt = 0; attempt < 50 && !busy_done; attempt++) {
            for (int j = 0; j < 256; j++) sd_tx_buf[j] = 0xFF;
            spi_full_duplex(256);
            for (int j = 0; j < 256; j++) {
                if (sd_rx_buf[j] != 0x00) { busy_done = 1; break; }
            }
        }
        if (!busy_done) return RES_ERROR;

        sector++;
        buff += 512;
    }

    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    (void)pdrv;

    if (Stat & STA_NOINIT) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;
            return RES_OK;

        case GET_SECTOR_COUNT:
            return RES_PARERR;

        default:
            return RES_PARERR;
    }
}