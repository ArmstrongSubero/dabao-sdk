/*
 * fatfs.c - FatFS filesystem on SPI SD card
 *
 * Mounts a FAT32 SD card, lists the root directory, writes a test file,
 * and reads it back. Uses FatFS through the diskio.c SPI driver.
 *
 * Wiring (SPI SD breakout to Dabao header):
 *   SD CLK  -> PC0 (header pin 14)
 *   SD MOSI -> PC1 (header pin 12)
 *   SD MISO -> PC2 (header pin 11)
 *   SD CS   -> PC3 (header pin 10)
 *   SD VCC  -> 3V3
 *   SD GND  -> GND
 *   PB1     -> LED -> 330R -> GND
 *   PB14    -> USB-serial RX
 */

#include "bao.h"
#include "ff.h"
#include "diskio.h"

static FATFS fs;
static FIL fil;
static DIR dir;
static FILINFO fno;

static uint32_t my_strlen(const char *s)
{
    uint32_t n = 0;
    while (*s++) n++;
    return n;
}

int main(void)
{
    FRESULT fr;

    bao_init();

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    mini_printf("\r\n========================================\r\n");
    mini_printf("  FatFS SD Card Test\r\n");
    mini_printf("========================================\r\n\r\n");

    /* Initialize disk (SPI init + SD card init happens inside diskio.c) */
    mini_printf("[1] disk_initialize...\r\n");
    DSTATUS ds = disk_initialize(0);
    if (ds & STA_NOINIT) {
        mini_printf("  FAIL: disk init returned 0x%02x\r\n", ds);
        while (1) { gpio_toggle(GPIO_PORT_B, 1); delay_ms(200); }
    }
    mini_printf("  OK\r\n\r\n");

    /* Mount filesystem */
    mini_printf("[2] f_mount...\r\n");
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        mini_printf("  FAIL: f_mount returned %d\r\n", (int)fr);
        while (1) { gpio_toggle(GPIO_PORT_B, 1); delay_ms(200); }
    }
    mini_printf("  Mounted OK (FAT type=%d)\r\n\r\n", fs.fs_type);

    /* List root directory */
    mini_printf("[3] Root directory:\r\n");
    fr = f_opendir(&dir, "/");
    if (fr == FR_OK) {
        int count = 0;
        for (;;) {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || fno.fname[0] == 0) break;
            if (fno.fattrib & AM_DIR)
                mini_printf("  [DIR]  %s\r\n", fno.fname);
            else
                mini_printf("  %s  (%u bytes)\r\n", fno.fname, (uint32_t)fno.fsize);
            count++;
            if (count >= 20) {
                mini_printf("  ... (truncated)\r\n");
                break;
            }
        }
        f_closedir(&dir);
        if (count == 0) mini_printf("  (empty)\r\n");
    }
    mini_printf("\r\n");

    /* Write a test file */
    mini_printf("[4] Writing DABAO.TXT...\r\n");
    fr = f_open(&fil, "DABAO.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        const char *msg = "Hello from Dabao!\r\nBaochip-1x bare metal C + FatFS.\r\n";
        UINT bw;
        fr = f_write(&fil, msg, (UINT)my_strlen(msg), &bw);
        if (fr == FR_OK)
            mini_printf("  Wrote %u bytes\r\n", bw);
        else
            mini_printf("  f_write failed: %d\r\n", (int)fr);
        f_close(&fil);
    } else {
        mini_printf("  f_open failed: %d\r\n", (int)fr);
    }
    mini_printf("\r\n");

    /* Read it back */
    mini_printf("[5] Reading DABAO.TXT...\r\n");
    fr = f_open(&fil, "DABAO.TXT", FA_READ);
    if (fr == FR_OK) {
        char rbuf[128];
        UINT br;
        fr = f_read(&fil, rbuf, sizeof(rbuf) - 1, &br);
        if (fr == FR_OK) {
            rbuf[br] = '\0';
            mini_printf("  Read %u bytes:\r\n", br);
            mini_printf("  \"%s\"\r\n", rbuf);
        }
        f_close(&fil);
    }
    mini_printf("\r\n");

    mini_printf("[Done] FatFS test complete.\r\n");

    while (1) {
        gpio_toggle(GPIO_PORT_B, 1);
        delay_ms(500);
    }
}
