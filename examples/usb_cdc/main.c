/*
 * main.c - USB serial port (CDC) demo
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Dabao's USB port becomes a serial port on the PC. Whatever is typed
 * into it is sent back, and the UART console reports what the USB side is
 * doing.
 *
 * USB runs on TinyUSB with the Corigine UDC driver written by the
 * MicroPython contributors (third_party/tinyusb). The controller is
 * polled from the main loop, so no interrupt is used.
 *
 * While this program runs the USB port is no longer boot1's. Press RESET
 * to get the boot1 port back for flashing.
 *
 * Wiring:
 *   USB    --> PC
 *   PB14   --> USB-serial RX (console)
 */

#include "bao.h"
#include "tusb.h"
#include "dcd_corigine_udc.h"

/* pid.codes test pair, for bench testing only. It is not unique and must
 * not ship in a product. Use your own VID and PID there. */
#define USB_VID   0x1209
#define USB_PID   0x0001
#define USB_BCD   0x0200

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

static const uint8_t desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

static const uint8_t desc_hs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 512),
};

static const tusb_desc_device_qualifier_t desc_device_qualifier = {
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0x00
};

static const char *const string_desc[] = {
    "Rovari",
    "Dabao USB Serial",
    "DABAO0001",
    "Dabao CDC",
};

static uint8_t  desc_other_speed[CONFIG_TOTAL_LEN];
static uint16_t desc_string[32 + 1];

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&desc_device;
}

const uint8_t *tud_descriptor_device_qualifier_cb(void)
{
    return (const uint8_t *)&desc_device_qualifier;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration
                                                : desc_fs_configuration;
}

const uint8_t *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void)index;
    const uint8_t *src = (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_fs_configuration
                                                              : desc_hs_configuration;
    for (uint32_t i = 0; i < CONFIG_TOTAL_LEN; i++)
        desc_other_speed[i] = src[i];
    desc_other_speed[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    return desc_other_speed;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint32_t count = 0;

    if (index == 0) {
        desc_string[1] = 0x0409;
        count = 1;
    } else {
        if (index > sizeof(string_desc) / sizeof(string_desc[0]))
            return NULL;
        const char *str = string_desc[index - 1];
        while (str[count] != 0 && count < 32) {
            desc_string[1 + count] = (uint16_t)str[count];
            count++;
        }
    }
    desc_string[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return desc_string;
}

uint32_t tusb_time_millis_api(void)
{
    return (uint32_t)millis();
}

void tud_mount_cb(void)
{
    mini_printf("USB: configured by the host, %s speed\r\n",
                tud_speed_get() == TUSB_SPEED_HIGH ? "high" : "full");
}

void tud_umount_cb(void)
{
    mini_printf("USB: unconfigured\r\n");
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    mini_printf("USB: line state DTR %u RTS %u\r\n", dtr, rts);
}

static void echo_task(void)
{
    uint8_t buf[64];

    if (!tud_cdc_available())
        return;

    uint32_t n = tud_cdc_read(buf, sizeof(buf));
    tud_cdc_write(buf, n);
    tud_cdc_write_flush();

    mini_printf("USB: echoed %u bytes\r\n", n);
}

int main(void)
{
    bao_init();

    mini_printf("\r\n========================================\r\n");
    mini_printf("  USB Serial Demo\r\n");
    mini_printf("========================================\r\n");

    tusb_rhport_init_t dev_init = {
        .role  = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    if (!tusb_init(0, &dev_init)) {
        mini_printf("USB: controller did not start\r\n");
        while (1) {}
    }
    mini_printf("USB: controller started, waiting for the host\r\n");

    while (1) {
        dcd_corigine_poll_pending_events(0);
        tud_task();
        echo_task();
    }
}