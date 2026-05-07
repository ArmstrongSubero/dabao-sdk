/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * System initialization for the Dabao board.
 */

#include "bao/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "boards/dabao.h"
#include "sevs_runtime.h"

/*
 * The compiler emits calls to memcpy and memset when optimizing
 * array initializers and struct copies. Provide minimal implementations
 * so we don't need to link against a full libc.
 */
void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int val, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)val;
    return dst;
}

/** @brief Memcmp.
 *  @param[in] a First memory region.
 *  @param[in] b Second memory region.
 *  @param[in] n Number of bytes to compare.
 *  @return 0 if equal, positive if a > b, negative if a < b.
 *  @req REQ-DABAO-STDLIB-0001 */
int memcmp(const void *a, const void *b, size_t n)
{
    SEVS_REQUIRE_NOT_NULL(a);
    SEVS_REQUIRE_NOT_NULL(b);
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;
    for (int s_poll = 0; s_poll < 1000000 && (n--); s_poll++) {
        if (*p != *q) return *p - *q;
        p++; q++;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (int s_guard = 0; s_guard < 100000 && (*s); s_guard++) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : (char *)0;
}

/** @brief Setup default uart.
 *  @req REQ-DABAO-STDLIB-0002 */
void setup_default_uart(void)
{
    uart_init(BAO_DEFAULT_UART, BAO_DEFAULT_UART_BAUD_RATE);
}

void bao_init(void)
{
    /*
     * boot0/boot1 have already configured:
     *   - PLL to 350 MHz (aclk)
     *   - SRAM bank 0 write wait state
     *   - All clock gates open
     *   - UART2 at 1 Mbaud
     *
     * We just reconfigure UART2 for 115200 and set up the tick timer.
     */
    setup_default_uart();

    /* Force tick timer init so delay_ms works immediately */
    delay_ms(0);
}

/*
 * Default trap handler.
 * On an exception (not an interrupt), turn on PB1 as a fault indicator
 * and halt. This provides visible feedback that something went wrong.
 */
__attribute__((weak)) void trap_dispatch(void)
{
    uint32_t mcause;
    __asm__ volatile ("csrr %0, mcause" : "=r"(mcause));

    /* Bit 31 set = interrupt, clear = exception */
    if (!(mcause & 0x80000000)) {
        /* Exception: light up PB1 and halt */
        gpio_init(GPIO_PORT_B, 1);
        gpio_set_dir(GPIO_PORT_B, 1, true);
        gpio_put(GPIO_PORT_B, 1, true);
        for (;;) { /* intentional halt */ }
    }
}
