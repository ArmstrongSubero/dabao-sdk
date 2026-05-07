/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal printf for the Dabao board.
 * Output goes to UART2 via the UDMA driver.
 *
 * Extracted from working main_sd_spi.c, verified on hardware.
 */

#include "bao/stdlib.h"
#include "hardware/uart.h"
#include "sevs_runtime.h"

#define STDIO_UART 2

static void print_unsigned(uint32_t val, int base, int min_digits)
{
    char buf[12];
    int i = 0;

    if (val == 0) {
        buf[i++] = '0';
    } else {
        for (int s_poll = 0; s_poll < 1000000 && (val > 0); s_poll++) {
            uint32_t d = val % base;
            buf[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
            val /= base;
        }
    }
    while (i < min_digits)
        buf[i++] = '0';

    for (int j = i - 1; j >= 0; j--)
        uart_putc(STDIO_UART, buf[j]);
}

/** @brief Mini printf.
 *  @param[in] fmt Format string (supports %d, %u, %x, %s, %c, %%).
 *  @req REQ-DABAO-STDIO-0001 */
void mini_printf(const char *fmt, ...)
{
    SEVS_REQUIRE_NOT_NULL(fmt);
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    for (int s_guard = 0; s_guard < 100000 && (*fmt); s_guard++) {
        if (*fmt == '%') {
            fmt++;
            int md = 0;
            if (*fmt == '0') {
                fmt++;
                for (int s_guard = 0; s_guard < 100000 && (*fmt >= '0' && *fmt <= '9'); s_guard++) {
                    md = md * 10 + (*fmt - '0');
                    fmt++;
                }
            }
            switch (*fmt) {
            case 'd': {
                int32_t v = __builtin_va_arg(ap, int32_t);
                if (v < 0) {
                    uart_putc(STDIO_UART, '-');
                    v = -v;
                }
                print_unsigned((uint32_t)v, 10, md);
                break;
            }
            case 'u':
                print_unsigned(__builtin_va_arg(ap, uint32_t), 10, md);
                break;
            case 'x':
                print_unsigned(__builtin_va_arg(ap, uint32_t), 16, md);
                break;
            case 's': {
                const char *s = __builtin_va_arg(ap, const char *);
                while (*s)
                    uart_putc(STDIO_UART, *s++);
                break;
            }
            case 'c':
                uart_putc(STDIO_UART, (char)__builtin_va_arg(ap, int));
                break;
            case '%':
                uart_putc(STDIO_UART, '%');
                break;
            default:
                uart_putc(STDIO_UART, '%');
                uart_putc(STDIO_UART, *fmt);
                break;
            }
        } else {
            uart_putc(STDIO_UART, *fmt);
        }
        fmt++;
    }

    __builtin_va_end(ap);
}
