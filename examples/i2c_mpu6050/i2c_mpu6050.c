/*
 * i2c_mpu6050.c - Read accelerometer data from an MPU6050 over I2C
 *
 * Reads the WHO_AM_I register, then continuously reads the 3-axis
 * accelerometer and temperature. Prints raw values over UART.
 *
 * Wiring:
 *   PB11 (header pin 27) --> SCL (with 4.7K pull-up to 3V3)
 *   PB12 (header pin 26) --> SDA (with 4.7K pull-up to 3V3)
 *   3V3                  --> VCC
 *   GND                  --> GND, AD0
 */

#include "bao.h"
#include "hardware/i2c.h"

#define MPU6050_ADDR        0x68
#define MPU6050_WHO_AM_I    0x75
#define MPU6050_PWR_MGMT_1  0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B

static void mpu_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = { reg, val };
    i2c_write_blocking(0, MPU6050_ADDR, data, 2);
}

static uint8_t mpu_read_reg(uint8_t reg)
{
    uint8_t val;
    i2c_write_read_blocking(0, MPU6050_ADDR, &reg, 1, &val, 1);
    return val;
}

static int16_t read_16(const uint8_t *buf)
{
    return (int16_t)((buf[0] << 8) | buf[1]);
}

int main(void)
{
    bao_init();

    mini_printf("\r\nMPU6050 I2C Demo\r\n");

    /* Init I2C0 at 100 kHz */
    i2c_init(0, 100000);

    /* Check WHO_AM_I */
    uint8_t who = mpu_read_reg(MPU6050_WHO_AM_I);
    mini_printf("WHO_AM_I = 0x%02x", who);
    if (who == 0x68)
        mini_printf(" [OK]\r\n");
    else
        mini_printf(" [unexpected]\r\n");

    /* Wake up the MPU6050 (clear sleep bit) */
    mpu_write_reg(MPU6050_PWR_MGMT_1, 0x00);
    delay_ms(100);

    while (1) {
        uint8_t buf[14];
        uint8_t reg = MPU6050_ACCEL_XOUT_H;
        i2c_write_read_blocking(0, MPU6050_ADDR, &reg, 1, buf, 14);

        int16_t ax = read_16(&buf[0]);
        int16_t ay = read_16(&buf[2]);
        int16_t az = read_16(&buf[4]);
        int16_t temp_raw = read_16(&buf[6]);
        int32_t temp_mc = (int32_t)temp_raw * 100 / 340 + 3653;

        mini_printf("AX=%d AY=%d AZ=%d T=%d.%02u C\r\n",
                    ax, ay, az, temp_mc / 100, temp_mc % 100);

        delay_ms(50);
    }
}
