# Dabao SDK

The Dabao SDK provides the headers, libraries and build system necessary to write programs for the Baochip-1x RISC-V SoC in C. It targets the Dabao evaluation board, a Pico-form-factor dev board for the Baochip-1x.

The SDK provides a Pico-style API and programming environment that is familiar to embedded C developers. A single program runs on the device at a time and starts with a conventional `main()` method. C-level libraries are provided for accessing all of the Baochip-1x's hardware including GPIO, UART, SPI, I2C, PWM, ADC, timers, the BIO coprocessor (analogous to PIO), hardware cryptography (AES, SHA-256, TRNG), and on-chip ReRAM non-volatile storage.

All peripheral drivers use the UDMA DMA engine for data transfers, with both blocking and non-blocking (async) APIs available for UART, SPI, and I2C. The BIO coprocessor provides a quad-core 700 MHz programmable I/O subsystem that can be used for custom protocols, high-speed bit-banging, or memory-to-memory DMA.

The design goal for the SDK is to be simple but powerful. Register definition headers are provided so you can access raw hardware registers directly when needed.

## Quick Start

**Windows:**

```
bao_flash build blink
bao_flash flash COM9 blink
```

**Linux/macOS:**

```
chmod +x bao_flash.sh
./bao_flash.sh build blink
./bao_flash.sh flash /dev/ttyACM0 blink
```

## Requirements

- **Python 3:** Required for UF2 signing and serial flashing
- **pyserial:** `pip install pyserial`
- **USB-serial adapter:** PB14 (TX), PB13 (RX), 115200 baud for serial output

### Toolchain Setup

Download xpack riscv-none-elf-gcc 15.2.0 from:
https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases

Extract it into the SDK root so the folder structure looks like:

```
dabao_sdk/
  xpack-riscv-none-elf-gcc-15.2.0-1/
    bin/
      riscv-none-elf-gcc
      ...
  bao_flash.bat
  ...
```

The build script finds it automatically. Alternatively, add the toolchain's `bin/` folder to your system PATH.

## Building

```
bao_flash build <example>
```

Output goes to `build/<example>.uf2`.

## Flashing

Press RESET on the board to enter the boot1 REPL, then:

```
bao_flash flash <port> <example>
```

Bootwait is enabled after flashing. Pressing RESET always returns to the boot1 REPL so you can flash again without touching any buttons.

To make firmware persist across power cycles and reset:

```
bao_flash flash <port> <example> --persistent
```

The firmware will run automatically on every power cycle and reset. To return to the boot1 REPL for reflashing:

1. Hold PROG
2. Press and release RESET (keep holding PROG)
3. Wait 1 second
4. Release PROG

The board will enter the boot1 REPL and you can flash normally again.

## Build + Flash

```
bao_flash run <port> <example>
bao_flash run <port> <example> --persistent
```

## Viewing Serial Output

Connect a USB-serial adapter to PB14 (TX) and PB13 (RX). Open a serial terminal at 115200 baud, 8N1.

## List Examples

```
bao_flash list
```

## ICSP Mode (Zero-Button Reflash)

If you have a CP2104 USB-serial adapter with DTR wired to the RUN pin (pin 30), you can reflash without pressing any buttons:

```
bao_flash flash <cdc_port> <example> --icsp <dtr_port>
```

The adapter toggles DTR to reset the board into boot1, then the firmware is flashed over the USB CDC port. Example:

```
bao_flash flash COM9 blink --icsp COM4
```

## Drivers

| Driver  | Header               | Description                                                       |
| ------- | -------------------- | ----------------------------------------------------------------- |
| GPIO    | `hardware/gpio.h`    | Digital I/O with pull-ups, Schmitt trigger, bank ops              |
| UART    | `hardware/uart.h`    | UDMA UART with blocking and async DMA (4 instances)               |
| SPI     | `hardware/spi.h`     | UDMA SPI master with blocking and async full-duplex (4 instances) |
| I2C     | `hardware/i2c.h`     | UDMA I2C master with blocking and async (4 instances)             |
| PWM     | `hardware/pwm.h`     | 4 slices, 4 channels each, frequency and duty control             |
| ADC     | `hardware/adc.h`     | 10-bit ADC with bandgap reference and temp sensor                 |
| Timer   | `hardware/timer.h`   | Timer0 (periodic/one-shot), TickTimer, ATimer, D11CTIME           |
| IRQ     | `hardware/irq.h`     | Per-array interrupt dispatch with callback registration           |
| BIO     | `hardware/bio.h`     | Quad-core PicoRV32 I/O coprocessor at 700 MHz                     |
| BIO DMA | `hardware/bio_dma.h` | Memory-to-memory DMA via BIO Core 3                               |
| RRAM    | `hardware/rram.h`    | 2.5 MB on-chip non-volatile storage with unaligned writes         |
| AES     | `hardware/aes.h`     | Hardware AES-128/192/256 encryption (SCE)                         |
| SHA     | `hardware/sha.h`     | Hardware SHA-256 hashing (SCE)                                    |
| TRNG    | `hardware/trng.h`    | Hardware true random number generator (SCE)                       |
| WDT     | `hardware/wdt.h`     | Watchdog timer with lock/unlock                                   |
| RTC     | `hardware/rtc.h`     | Real-time clock on always-on domain                               |
| QSPI    | `hardware/qspi.h`    | General-purpose quad-SPI via UDMA SPIM (SPIM1 on Dabao)           |
| W25Q    | `hardware/w25q.h`    | W25Q-series external NOR flash (read, write, erase, quad mode)    |

## Examples

| Example            | Peripheral | Description                                                 |
| ------------------ | ---------- | ----------------------------------------------------------- |
| `blink`            | GPIO       | Toggle LED on PB1 at 1 Hz                                   |
| `button`           | GPIO       | Read push button on PB3 with pull-up                        |
| `hello_uart`       | UART       | Print messages over UART2                                   |
| `hello_uart_async` | UART       | Non-blocking UART transmit with LED work during DMA         |
| `pwm`              | PWM        | LED breathing on PB1 via PWM slice 1                        |
| `spi_mcp41010`     | SPI        | Sweep a digital potentiometer over SPI2                     |
| `spi_async`        | SPI        | Non-blocking full-duplex SPI with loopback verification     |
| `i2c_mpu6050`      | I2C        | Read accelerometer data over I2C0                           |
| `i2c_async`        | I2C        | Non-blocking I2C reads from MPU6050                         |
| `adc_read`         | ADC        | Read analog voltage on PC9 and temperature                  |
| `timer_irq`        | Timer, IRQ | LED blink driven by Timer0 interrupt                        |
| `ticktimer`        | Timer      | Free-running 64-bit tick counter                            |
| `atimer`           | Timer      | Always-on timer in low-power domain                         |
| `heartbeat`        | Timer      | LED heartbeat using D11CTIME                                |
| `irq`              | IRQ        | Dual interrupt demo (Timer0 + UART2 TX done)                |
| `bio_blink`        | BIO        | 100 kHz square wave from BIO coprocessor                    |
| `bio_dma`          | BIO DMA    | Memory-to-memory copy at 700 MHz via BIO Core 3             |
| `rram`             | RRAM       | Non-volatile storage: aligned, unaligned, cross-page writes |
| `aes`              | AES        | NIST test vector encrypt/decrypt round-trip                 |
| `sha`              | SHA        | SHA-256 hash verification against known vectors             |
| `trng`             | TRNG       | Generate hardware random numbers                            |
| `wdt`              | WDT        | Watchdog timer with feed and reset                          |
| `rtc`              | RTC        | Real-time clock counting seconds                            |
| `fatfs`            | SPI, FatFS | SD card mount, directory listing, file read/write           |
| `pmu`              | PMU        | Power management register dump                              |
| `w25q_flash`       | QSPI, W25Q | External NOR flash: ID, erase, write, read, quad mode       |

## Writing Your Own Program

Create a directory under `examples/` with a single `.c` file:

```c
#include "bao.h"

int main(void)
{
    bao_init();
    mini_printf("Hello!\r\n");

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    while (1) {
        gpio_toggle(GPIO_PORT_B, 1);
        delay_ms(500);
    }
}
```

Then: `bao_flash build your_program`

## Clock Tree

| Clock  | Frequency | Source                                    |
| ------ | --------- | ----------------------------------------- |
| FCLK   | 700 MHz   | PLL                                       |
| ACLK   | 350 MHz   | FCLK/2                                    |
| PCLK   | 43.75 MHz | ACLK/8                                    |
| perclk | 99 MHz    | PLL (integer divider from 48 MHz crystal) |
| BIO    | 700 MHz   | FCLK                                      |

## Pin Map (Dabao Header)

| Pin | Chip | SPI/QSPI       | I2C/UART/PWM |
| --- | ---- | -------------- | ------------ |
| 1   | PC13 | QSPI1_CS1      |              |
| 2   | PC12 | QSPI1_CS0      |              |
| 4   | PC11 | QSPI1_CLK      |              |
| 5   | PC10 | QSPI1_D3       |              |
| 6   | PC9  | QSPI1_D2       |              |
| 7   | PC8  | QSPI1_D1       |              |
| 8   | PC7  | QSPI1_D0       |              |
| 10  | PC3  | SPI2_CS0       | PWM2.3       |
| 11  | PC2  | SPI2_D1 (MISO) | PWM2.2       |
| 12  | PC1  | SPI2_D0 (MOSI) | PWM2.1       |
| 14  | PC0  | SPI2_CLK       | PWM2.0       |
| 15  | PB14 |                | UART2 TX     |
| 16  | PB13 |                | UART2 RX     |
| 26  | PB12 | SPI2_CS1       | I2C0 SDA     |
| 27  | PB11 | SPI2_CS0       | I2C0 SCL     |
| 29  | PB1  |                | PWM1.1       |
| 31  | PB2  |                | PWM1.2       |
| 32  | PB3  |                | PWM1.3       |

## Known Hardware Quirks

- **MDMA (PL230) is not writable** from bare metal. Use BIO DMA instead.
- **SDIO has 3 RTL bugs** (response timeout, clock gating, output enable). Use SPI mode for SD cards.
- **SPI full-duplex command ID is 0xC**, not 0x8 (which is RPT).
- **BIO access filters** must be disabled before BIO cores can access SRAM/peripherals.
- **perclk is 99 MHz**, not 100 MHz. PLL fractional divider is broken, integer-only from 48 MHz crystal.
- **RRAM security vectors** occupy the top 152 KB (above 0x603DA000). Do not write there.

## License

Apache-2.0
