/*
 * diskio.c: FatFs disk I/O layer for Baochip-1x / Dabao Board
 *
 * Ported from PIC16F1718 version by Armstrong Subero.
 * Uses UDMA SPI2 full-duplex transfers instead of byte-at-a-time MSSP.
 *
 * SPI2 (SPIM2) at 0x50107000, clock gate bit 6
 * Pins: PC0=CLK, PC1=MOSI, PC2=MISO, PC3=CS (all AF2, hardware CS via SOT/EOT)
 * Init at 400 kHz, data at 12.4 MHz (clkdiv=3)
 *
 * Every transfer ends with EOT keep_cs set, so CS stays low from the first
 * command onwards and a command, its token wait and its data block can be
 * spread over several transfers. The card is the only device on SPI2.
 * This is the arrangement that was verified on hardware.
 *
 * Every wait is bounded. A missing or wedged card makes a call fail with
 * RES_ERROR or RES_NOTRDY rather than hanging the program. Token and busy
 * waits are timed with millis(): the SD specification allows a card up to
 * 100 ms to produce a read data token and up to 250 ms of busy after a
 * write, and slow cards use most of it.
 */

#include <stdint.h>
#include <stdbool.h>
#include "diskio.h"
#include "bao/stdlib.h"

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
#define SPI_EOT_KEEP_CS (1 << 1)

/* ---- Limits ---- */

#define SD_BUF_SIZE         2600u
#define SD_POLL_LIMIT       1000000
#define SD_READ_TOKEN_MS    100u
#define SD_WRITE_BUSY_MS    500u
#define SD_INIT_MS          1000u
#define SD_POLL_CHUNK       64u

/* ---- DMA buffers in IFRAM ---- */

static uint8_t  sd_tx_buf[SD_BUF_SIZE] __attribute__((section(".dma_buffers"))) __attribute__((aligned(4)));
static uint8_t  sd_rx_buf[SD_BUF_SIZE] __attribute__((section(".dma_buffers"))) __attribute__((aligned(4)));
static uint32_t sd_cmd_buf[8]          __attribute__((section(".dma_buffers"))) __attribute__((aligned(4)));

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

static bool spi_wait(void)
{
    for (int s_poll = 0; s_poll < SD_POLL_LIMIT; s_poll++) {
        if ((SPI2_CMD_CFG & UDMA_CFG_EN) == 0 &&
            (SPI2_TX_CFG & UDMA_CFG_EN) == 0 &&
            (SPI2_RX_CFG & UDMA_CFG_EN) == 0)
            return true;
    }
    return false;
}

/* Full-duplex SPI: send len bytes from sd_tx_buf, receive into sd_rx_buf.
   Returns false if the previous or this transfer never completed. */
static bool spi_full_duplex(uint32_t len)
{
    uint32_t ci = 0;

    if (len == 0 || len > SD_BUF_SIZE) return false;
    if (!spi_wait()) return false;

    sd_cmd_buf[ci++] = SPI_CMD_CFG | (sd_clkdiv & 0xFF);
    sd_cmd_buf[ci++] = SPI_CMD_SOT | (0 << 0);
    sd_cmd_buf[ci++] = SPI_CMD_FULL | ((8 - 1) << 16) | ((len - 1) & 0xFFFF);
    sd_cmd_buf[ci++] = SPI_CMD_EOT | SPI_EOT_KEEP_CS;
    __asm__ volatile ("fence" ::: "memory");

    SPI2_TX_SADDR = (uint32_t)(uintptr_t)sd_tx_buf;
    SPI2_TX_SIZE = len;
    SPI2_TX_CFG = UDMA_CFG_EN;

    SPI2_RX_SADDR = (uint32_t)(uintptr_t)sd_rx_buf;
    SPI2_RX_SIZE = len;
    SPI2_RX_CFG = UDMA_CFG_EN;

    SPI2_CMD_SADDR = (uint32_t)(uintptr_t)sd_cmd_buf;
    SPI2_CMD_SIZE = ci * 4;
    SPI2_CMD_CFG = UDMA_CFG_EN | (2 << 1);

    if (!spi_wait()) return false;
    __asm__ volatile ("fence" ::: "memory");
    return true;
}

/* TX-only without SOT (for init clocks with CS high) */
static bool spi_tx_no_cs(uint32_t len)
{
    uint32_t ci = 0;

    if (len == 0 || len > SD_BUF_SIZE) return false;
    if (!spi_wait()) return false;

    sd_cmd_buf[ci++] = SPI_CMD_CFG | (sd_clkdiv & 0xFF);
    sd_cmd_buf[ci++] = SPI_CMD_TX_DATA | ((8 - 1) << 16) | ((len - 1) & 0xFFFF);
    __asm__ volatile ("fence" ::: "memory");

    SPI2_TX_SADDR = (uint32_t)(uintptr_t)sd_tx_buf;
    SPI2_TX_SIZE = len;
    SPI2_TX_CFG = UDMA_CFG_EN;

    SPI2_CMD_SADDR = (uint32_t)(uintptr_t)sd_cmd_buf;
    SPI2_CMD_SIZE = ci * 4;
    SPI2_CMD_CFG = UDMA_CFG_EN | (2 << 1);

    return spi_wait();
}

/* Clock out len bytes of 0xFF and receive the reply. */
static bool spi_clock_ff(uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) sd_tx_buf[i] = 0xFF;
    return spi_full_duplex(len);
}

/* ---- SD command helpers ---- */

/* Send SD command, return R1 response or 0xFF on no reply or SPI failure. */
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

    if (!spi_full_duplex(17)) return 0xFF;

    /* Find R1 (first non-0xFF byte after command) */
    uint8_t r1 = 0xFF;
    for (int i = 7; i < 17; i++) {
        if (sd_rx_buf[i] != 0xFF) { r1 = sd_rx_buf[i]; break; }
    }
    return r1;
}

/* Send command and read 4-byte trailing response (for CMD8, CMD58).
   resp4 is only written when a complete response was received. */
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

    if (!spi_full_duplex(27)) return 0xFF;

    uint8_t r1 = 0xFF;
    int r1_pos = -1;
    for (int i = 7; i < 27; i++) {
        if (sd_rx_buf[i] != 0xFF) { r1 = sd_rx_buf[i]; r1_pos = i; break; }
    }

    if (r1_pos < 0 || r1_pos + 4 >= 27) return 0xFF;
    if (resp4) {
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

    uint8_t ty, ocr[4] = { 0, 0, 0, 0 };
    uint64_t t0;

    Stat = STA_NOINIT;
    CardType = 0;

    /* Init SPI at 400 kHz */
    spi2_hw_init(122);

    /* 80 clocks with CS high (no SOT) */
    for (int i = 0; i < 10; i++) sd_tx_buf[i] = 0xFF;
    if (!spi_tx_no_cs(10)) return Stat;

    ty = 0;

    if (sd_send_cmd(0, 0) == 1) {
        /* Card in idle state */

        if (sd_send_cmd_r7(8, 0x1AA, ocr) == 1) {
            /* SD v2 */
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                /* ACMD41 with HCS, up to one second */
                t0 = millis();
                for (;;) {
                    uint8_t r = sd_send_cmd(0x80 | 41, 1UL << 30);
                    if (r == 0) {
                        /* Read OCR for CCS bit */
                        if (sd_send_cmd_r7(58, 0, ocr) == 0) {
                            ty = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
                        }
                        break;
                    }
                    if (r == 0xFF || millis() - t0 > SD_INIT_MS) break;
                    delay_us(100);
                }
            }
        } else {
            /* SD v1 or MMC */
            uint8_t cmd;
            uint8_t r = sd_send_cmd(0x80 | 41, 0);
            if (r <= 1) {
                ty = CT_SD1;
                cmd = 0x80 | 41;  /* ACMD41 */
            } else {
                ty = CT_MMC;
                cmd = 0x40 | 1;   /* CMD1 */
            }

            t0 = millis();
            for (;;) {
                r = sd_send_cmd(cmd, 0);
                if (r == 0) break;
                if (r == 0xFF || millis() - t0 > SD_INIT_MS) { ty = 0; break; }
                delay_us(100);
            }

            if (ty && sd_send_cmd(0x40 | 16, 512) != 0)
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

    if (!count || !buff) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & CT_BLOCK)) sector *= 512;

    while (count--) {
        uint8_t frame[6];
        frame[0] = 0x40 | 17;
        frame[1] = (sector >> 24) & 0xFF;
        frame[2] = (sector >> 16) & 0xFF;
        frame[3] = (sector >> 8) & 0xFF;
        frame[4] = sector & 0xFF;
        frame[5] = sd_crc7(frame, 5);

        /* 1 dummy + 6 cmd + 10 response. CS stays low afterwards. */
        sd_tx_buf[0] = 0xFF;
        for (int i = 0; i < 6; i++) sd_tx_buf[i+1] = frame[i];
        for (int i = 7; i < 17; i++) sd_tx_buf[i] = 0xFF;
        if (!spi_full_duplex(17)) return RES_ERROR;

        uint8_t r1 = 0xFF;
        uint32_t scan = 17;
        for (uint32_t i = 7; i < 17; i++) {
            if (sd_rx_buf[i] != 0xFF) { r1 = sd_rx_buf[i]; scan = i + 1; break; }
        }
        if (r1 != 0x00) return RES_ERROR;

        /* Look for the 0xFE data token, first in what is left of the command
         * transfer (a fast card answers within a byte or two), then in small
         * chunks for up to 100 ms. The bytes after the token in whichever
         * transfer holds it are the first bytes of the sector. */
        uint32_t got = 0;
        uint32_t len = 17;
        int found = 0;
        uint64_t t0 = millis();
        for (;;) {
            for (uint32_t i = scan; i < len; i++) {
                if (sd_rx_buf[i] == 0xFE) {
                    for (uint32_t j = i + 1; j < len && got < 512; j++)
                        buff[got++] = sd_rx_buf[j];
                    found = 1;
                    break;
                }
                if (sd_rx_buf[i] != 0xFF) return RES_ERROR;   /* error token */
            }
            if (found) break;
            if (millis() - t0 > SD_READ_TOKEN_MS) return RES_ERROR;
            if (!spi_clock_ff(SD_POLL_CHUNK)) return RES_ERROR;
            scan = 0;
            len = SD_POLL_CHUNK;
        }

        /* Rest of the sector plus the 2 CRC bytes */
        uint32_t rest = (512 - got) + 2;
        if (!spi_clock_ff(rest)) return RES_ERROR;
        for (uint32_t i = 0; got < 512; i++)
            buff[got++] = sd_rx_buf[i];

        sector++;
        buff += 512;
    }

    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    (void)pdrv;

    if (!count || !buff) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & CT_BLOCK)) sector *= 512;

    while (count--) {
        /* Build CMD24 + data as single transaction:
         * 1 dummy + 6 cmd + 10 response + 1 gap + 1 token + 512 data + 2 CRC + 64 busy = 597 */
        uint8_t frame[6];
        frame[0] = 0x40 | 24;
        frame[1] = (sector >> 24) & 0xFF;
        frame[2] = (sector >> 16) & 0xFF;
        frame[3] = (sector >> 8) & 0xFF;
        frame[4] = sector & 0xFF;
        frame[5] = sd_crc7(frame, 5);

        uint32_t pos = 0;
        sd_tx_buf[pos++] = 0xFF;
        for (int i = 0; i < 6; i++) sd_tx_buf[pos++] = frame[i];
        for (int i = 0; i < 10; i++) sd_tx_buf[pos++] = 0xFF;
        sd_tx_buf[pos++] = 0xFF;
        sd_tx_buf[pos++] = 0xFE;
        for (int i = 0; i < 512; i++) sd_tx_buf[pos++] = buff[i];
        sd_tx_buf[pos++] = 0xFF;
        sd_tx_buf[pos++] = 0xFF;
        for (int i = 0; i < 64; i++) sd_tx_buf[pos++] = 0xFF;

        if (!spi_full_duplex(pos)) return RES_ERROR;

        uint8_t r1 = 0xFF;
        for (int i = 7; i < 17; i++) {
            if (sd_rx_buf[i] != 0xFF) { r1 = sd_rx_buf[i]; break; }
        }
        if (r1 != 0x00) return RES_ERROR;

        /* Data response token follows the 512 data bytes, at about 533 */
        uint8_t resp = 0xFF;
        for (uint32_t i = 530; i < pos; i++) {
            if (sd_rx_buf[i] != 0xFF) { resp = sd_rx_buf[i]; break; }
        }
        if ((resp & 0x1F) != 0x05) return RES_ERROR;

        /* Wait for the card to finish programming (MISO low while busy),
         * up to 500 ms. */
        int busy_done = 0;
        uint64_t t0 = millis();
        while (!busy_done) {
            if (!spi_clock_ff(SD_POLL_CHUNK)) return RES_ERROR;
            for (uint32_t j = 0; j < SD_POLL_CHUNK; j++) {
                if (sd_rx_buf[j] != 0x00) { busy_done = 1; break; }
            }
            if (!busy_done && millis() - t0 > SD_WRITE_BUSY_MS) return RES_ERROR;
        }

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

        case MMC_GET_TYPE:
            *(BYTE*)buff = CardType;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}
