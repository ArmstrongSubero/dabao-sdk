/*
 * i2c_async.c - Non-blocking I2C reads from MPU6050
 *
 * Demonstrates async I2C: the CPU kicks off a 14-byte sensor
 * read and does other work (toggles LED, counts cycles) while
 * the I2C DMA runs in the background.
 *
 * Wiring:
 *   PB11 (header pin 27) --> SCL (with 4.7K pull-up to 3V3)
 *   PB12 (header pin 26) --> SDA (with 4.7K pull-up to 3V3)
 *   PB1  --> LED --> 330R --> GND
 *   3V3  --> VCC
 *   GND  --> GND, AD0
 */

#include "bao.h"
#include "hardware/i2c.h"

#define MPU6050_ADDR         0x68
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B

static int16_t read_16(const uint8_t *buf)
{
    return (int16_t)((buf[0] << 8) | buf[1]);
}

int main(void)
{
    bao_init();

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    mini_printf("\r\n========================================\r\n");
    mini_printf("  Async I2C Demo (MPU6050)\r\n");
    mini_printf("========================================\r\n\r\n");

    i2c_init(0, 100000);

    /* Check WHO_AM_I (blocking, one-time) */
    uint8_t reg = MPU6050_WHO_AM_I;
    uint8_t who;
    i2c_write_read_blocking(0, MPU6050_ADDR, &reg, 1, &who, 1);
    mini_printf("WHO_AM_I = 0x%02x %s\r\n", who, who == 0x68 ? "[OK]" : "[unexpected]");

    /* Wake up MPU6050 (blocking, one-time) */
    uint8_t wake[2] = { MPU6050_PWR_MGMT_1, 0x00 };
    i2c_write_blocking(0, MPU6050_ADDR, wake, 2);
    delay_ms(100);

    /* Test 1: Blocking read for comparison */
    mini_printf("\r\n[Test 1] Blocking I2C read (14 bytes)...\r\n");
    uint8_t buf[14];
    reg = MPU6050_ACCEL_XOUT_H;

    uint64_t t0 = millis();
    i2c_write_read_blocking(0, MPU6050_ADDR, &reg, 1, buf, 14);
    uint64_t t1 = millis();

    int16_t ax = read_16(&buf[0]);
    int16_t ay = read_16(&buf[2]);
    int16_t az = read_16(&buf[4]);
    mini_printf("  AX=%d AY=%d AZ=%d\r\n", ax, ay, az);
    mini_printf("  Time: %u ms\r\n", (uint32_t)(t1 - t0));

    /* Test 2: Async read with LED work */
    mini_printf("\r\n[Test 2] Async I2C read + LED work...\r\n");
    reg = MPU6050_ACCEL_XOUT_H;
    uint32_t toggles = 0;

    i2c_write_read_async(0, MPU6050_ADDR, &reg, 1, 14);

    while (!i2c_async_done(0)) {
        gpio_toggle(GPIO_PORT_B, 1);
        toggles++;
    }

    i2c_async_read(0, buf, 14);
    ax = read_16(&buf[0]);
    ay = read_16(&buf[2]);
    az = read_16(&buf[4]);
    mini_printf("  AX=%d AY=%d AZ=%d\r\n", ax, ay, az);
    mini_printf("  LED toggled %u times during transfer\r\n", toggles);

    /* Test 3: Continuous async reads */
    mini_printf("\r\n[Test 3] 10 async reads...\r\n");
    for (int i = 0; i < 10; i++) {
        reg = MPU6050_ACCEL_XOUT_H;
        i2c_write_read_async(0, MPU6050_ADDR, &reg, 1, 14);
        i2c_async_wait(0);
        i2c_async_read(0, buf, 14);

        ax = read_16(&buf[0]);
        ay = read_16(&buf[2]);
        az = read_16(&buf[4]);
        int16_t temp_raw = read_16(&buf[6]);
        int32_t temp_mc = (int32_t)temp_raw * 100 / 340 + 3653;
        mini_printf("  [%d] AX=%d AY=%d AZ=%d T=%d.%02u C\r\n",
                    i, ax, ay, az, temp_mc / 100, temp_mc % 100);
        delay_ms(200);
    }

    mini_printf("\r\n[Done] Async I2C demo complete.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
