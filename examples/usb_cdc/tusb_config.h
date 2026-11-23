/*
 * tusb_config.h - TinyUSB configuration for the usb_cdc example
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#include "hardware/gpio.h"

#define CFG_TUSB_MCU             OPT_MCU_CORIGINE_BAO1X
#define CFG_TUSB_OS              OPT_OS_NONE
#define CFG_TUSB_DEBUG           0

#define CFG_TUD_ENABLED          1
#define CFG_TUH_ENABLED          0
#define CFG_TUD_MAX_SPEED        OPT_MODE_HIGH_SPEED
#define CFG_TUD_ENDPOINT0_SIZE   64

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN       __attribute__((aligned(4)))

#define CFG_TUD_CDC              1
#define CFG_TUD_MSC              0
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           0

#define CFG_TUD_CDC_RX_BUFSIZE   512
#define CFG_TUD_CDC_TX_BUFSIZE   512
#define CFG_TUD_CDC_EP_BUFSIZE   512

/* PC13 shorts D+ and D- to ground on the Dabao. It is also the PROG pin. */
#define TUD_CORIGINE_SE0_ASSERT() \
    do { gpio_set_dir(GPIO_PORT_C, 13, true); gpio_put(GPIO_PORT_C, 13, false); } while (0)
#define TUD_CORIGINE_SE0_DEASSERT() \
    do { gpio_put(GPIO_PORT_C, 13, true); gpio_set_dir(GPIO_PORT_C, 13, false); } while (0)

/* The USB controller's DMA reaches IFRAM only */
#define TUD_CORIGINE_DMA_ATTR    __attribute__((section(".dma_buffers"), aligned(64)))

#endif
