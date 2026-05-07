# Assertion Handling

The SDK validates parameters at runtime. Pass an out-of-range port number, a null pointer, or an invalid instance index and the chip halts immediately. This is intentional. It catches bugs at the point of the mistake instead of letting corrupted state propagate through your system.

## What triggers an assertion

```c
gpio_init(7, 0);            /* port 7 does not exist */
aes_encrypt(NULL, out, 16); /* null pointer */
spi_init(5);                /* only instances 0-3 exist */
wdt_start(0);               /* timeout must be > 0 */
```

## What you see

The UART goes silent. Blinking LEDs freeze. The chip is stuck in a spin loop inside `sevs_assert_fail()`. There is no automatic reset.

By default there is no message telling you which assertion failed.

## Enabling assertion messages

Override `sevs_target_uart_write_str()` in your program. This is a weak symbol defined in the SDK. Your version replaces it at link time.

```c
#include "hardware/uart.h"

void sevs_target_uart_write_str(const char *s)
{
    while (*s) {
        uart_putc(2, *s++);
    }
}
```

Put this anywhere in your .c file, outside of main. No header include needed beyond `uart.h`. With this in place, a failed assertion prints before halting:

```
ASSERT FAIL: gpio.c:42: port < GPIO_NUM_PORTS
```

## Disabling interrupts on failure

Same pattern. Override `sevs_target_disable_irq()`:

```c
void sevs_target_disable_irq(void)
{
    __asm__ volatile ("csrci mstatus, 0x8");
}
```

This prevents an interrupt from firing while the assertion message is being printed.

## Both hooks together

A complete setup for the Dabao board:

```c
#include "bao.h"

void sevs_target_disable_irq(void)
{
    __asm__ volatile ("csrci mstatus, 0x8");
}

void sevs_target_uart_write_str(const char *s)
{
    while (*s) {
        uart_putc(2, *s++);
    }
}

int main(void)
{
    bao_init();

    /* Your code here. If an assertion fails,
       you will see the file and line in Termite. */
    gpio_init(7, 0);  /* this will print an error and halt */
}
```

## Changing halt behavior

Override `sevs_target_halt()` if you want a watchdog reset instead of a spin loop:

```c
#include "hardware/wdt.h"

void sevs_target_halt(void)
{
    wdt_start_raw(1);  /* immediate reset */
    for (;;) { }
}
```
