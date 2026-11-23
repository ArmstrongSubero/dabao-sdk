# Dabao SDK

The Dabao SDK provides the headers, libraries and build system necessary to write programs for the Baochip-1x RISC-V SoC in C. It targets the Dabao evaluation board, a Pico-form-factor dev board for the Baochip-1x.

The SDK provides a Pico-style API and programming environment that is familiar to embedded C developers. A single program runs on the device at a time and starts with a conventional `main()` method. C-level libraries are provided for accessing all of the Baochip-1x's hardware including GPIO, UART, SPI, I2C, PWM, ADC, timers, the BIO coprocessor (analogous to PIO), hardware cryptography (AES, SHA-256, SHA-512, HMAC, TRNG, the PKE and ALU big number engines, ECDSA and Ed25519 signatures), power down, USB serial and on-chip ReRAM non-volatile storage.

All peripheral drivers use the UDMA DMA engine for data transfers, with both blocking and non-blocking (async) APIs available for UART, SPI, and I2C. The BIO coprocessor provides a quad-core 700 MHz programmable I/O subsystem that can be used for custom protocols, high-speed bit-banging, or memory-to-memory DMA.

The design goal for the SDK is to be simple but powerful. Register definition headers are provided so you can access raw hardware registers directly when needed.

## Quick Start

**Windows:**

```bash
bao_flash build blink
bao_flash flash COM9 blink
```

**Linux/macOS:**

```bash
chmod +x bao_flash.sh
./bao_flash.sh build blink
./bao_flash.sh flash /dev/ttyACM0 blink
```

**uvx Windows/Linux/macOS**

Required [uv](https://docs.astral.sh/uv/).

```bash
uvx --from git+https://github.com/ArmstrongSubero/dabao-sdk dabao-serial-flash <PORT> <uf2_file>
```

## Requirements

- **Python 3:** Required for UF2 signing and serial flashing
- **pyserial:** `pip install pyserial`
- **pure25519:** `pip install pure25519`
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

The build script pulls in FatFS for any example that includes `ff.h` or `diskio.h`, and TinyUSB for `usb_cdc`, so neither needs any extra setup.

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

## Board State Out of the Box

The Dabao ships with `bootwait` enabled, so it comes up in the boot1 REPL
ready to program rather than running application code. You do not need to
hold any buttons for a first flash.

After flashing, `bootwait` stays enabled and pressing RESET returns you to
the boot1 REPL, so you can reflash without touching buttons. Use
`--persistent` if you want the firmware to run automatically on every reset
and power cycle instead.

Both boot1 entry points are supported. Older boot1 builds jump to 0x60060000 and boot1 builds from bunnie's change of 15 August 2026 jump to 0x60060400, and the linker script and startup code place a jump at 0x60060300 and `_start` at 0x60060400 so either one starts the program the same way.


## Drivers

| Driver  | Header               | Description                                                       |
| ------- | -------------------- | ----------------------------------------------------------------- |
| GPIO    | `hardware/gpio.h`    | Digital I/O with pull-ups, Schmitt trigger, bank ops              |
| UART    | `hardware/uart.h`    | UDMA UART with blocking and async DMA (4 instances)               |
| SPI     | `hardware/spi.h`     | UDMA SPI master with blocking and async full-duplex (4 instances) |
| I2C     | `hardware/i2c.h`     | UDMA I2C master with blocking and async, up to 4096 bytes per transfer (4 instances) |
| PWM     | `hardware/pwm.h`     | 4 slices, 4 channels each, frequency and duty control (0 to 100%) |
| ADC     | `hardware/adc.h`     | 10-bit ADC with bandgap reference and temp sensor                 |
| Timer   | `hardware/timer.h`   | Timer0 (periodic/one-shot), TickTimer, ATimer, D11CTIME           |
| IRQ     | `hardware/irq.h`     | Per-array interrupt dispatch with callback registration           |
| BIO     | `hardware/bio.h`     | Quad-core PicoRV32 I/O coprocessor at 700 MHz                     |
| BIO DMA | `hardware/bio_dma.h` | Memory-to-memory DMA via BIO Core 3                               |
| RRAM    | `hardware/rram.h`    | 2.5 MB on-chip non-volatile storage with unaligned writes         |
| AES     | `hardware/aes.h`     | Hardware AES-128/192/256 in ECB, CBC, CTR, OFB and CFB (SCE)      |
| SHA     | `hardware/sha.h`     | Hardware SHA-256 and SHA-512, streaming, HMAC-SHA256/512 (SCE)    |
| TRNG    | `hardware/trng.h`    | Hardware true random number generator (SCE)                       |
| PKE     | `hardware/pke.h`     | Public key engine: modular arithmetic, RSA and curve operations (SCE) |
| ALU     | `hardware/alu.h`     | Big number divide, add, subtract and modular arithmetic (SCE)     |
| Sign    | `hardware/sign.h`    | ECDSA on P-256, secp256k1 and P-384 (RFC 6979) and Ed25519 (RFC 8032) |
| WDT     | `hardware/wdt.h`     | Watchdog timer with lock/unlock                                   |
| RTC     | `hardware/rtc.h`     | Real-time clock on always-on domain, with alarm and wake          |
| Power   | `hardware/power.h`   | Power down until a wake source (RTC alarm) boots the chip again   |
| QSPI    | `hardware/qspi.h`    | General-purpose quad-SPI via UDMA SPIM (SPIM1 on Dabao)           |
| W25Q    | `hardware/w25q.h`    | W25Q-series external NOR flash (read, write, erase, quad mode)    |

The signature module runs on top of the PKE, ALU and SHA drivers. Every PKE call clears the SCE RAM, which also clears the AES key, so call `aes_set_key()` again after any PKE or signature call.

## Examples

| Example            | Peripheral | Description                                                 |
| ------------------ | ---------- | ----------------------------------------------------------- |
| `blink`            | GPIO       | Toggle LED on PB1 at 1 Hz                                   |
| `button`           | GPIO       | Read push button on PB3 with pull-up                        |
| `hello_uart`       | UART       | Print messages over UART2                                   |
| `hello_uart_async` | UART       | Non-blocking UART transmit with LED work during DMA         |
| `usb_cdc`          | USB        | The Dabao's USB port as a serial port that echoes what is typed |
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
| `alu_bigint`       | ALU        | 1024 bit modular arithmetic, inverse, gcd and divide against known answers |
| `pke_bigint`       | PKE, ALU   | The same big integer checks run on the PKE and ALU          |
| `pke_ecc`          | PKE        | P-256, secp256k1, P-384, X25519 and Ed25519 point operations |
| `pke_clock`        | PKE        | Prints the PLL and frequency sensors, checks the PKE at two clock speeds |
| `pke_diag`         | PKE        | Runs single PKE operations and prints the SCE status registers |
| `signed_update`    | Sign, RRAM | Checks an Ed25519 signature on an image before storing it in one of two RRAM slots |
| `wdt`              | WDT        | Watchdog timer with feed and reset                          |
| `rtc`              | RTC        | Real-time clock counting seconds                            |
| `fatfs`            | SPI, FatFS | SD card mount, directory listing, file read/write           |
| `pmu`              | PMU        | Power management register dump                              |
| `w25q_flash`       | QSPI, W25Q | External NOR flash: ID, erase, write, read, quad mode       |

### MMU sandbox examples

Each of these folders holds a `bao_mmu` file, so they always build with the sandbox described below.

| Example     | Description                                                              |
| ----------- | ------------------------------------------------------------------------ |
| `mmu_svc`   | SHA, TRNG and Timer0 interrupts used from supervisor mode through the SDK |
| `mmu_gap`   | A store into the unmapped SRAM gap, which ends in the fault report        |
| `mmu_stack` | A runaway recursion that reaches the guard page under the main stack      |

### Emulator tests (baoemu)

These programs were written against baoemu, the Baochip-1x emulator, and check the SDK and the BIO against the RTL and published test vectors, printing PASS or FAIL for each check.

| Example      | Description                                                          |
| ------------ | -------------------------------------------------------------------- |
| `aes_modes`  | Every AES mode and key size against the NIST SP 800-38A vectors       |
| `sha_stream` | Streaming SHA-256 and SHA-512 against the FIPS 180-2 vectors          |
| `hmac`       | HMAC-SHA256 and HMAC-SHA512 against RFC 4231 test cases 1 to 7        |
| `ecdsa`      | ECDSA on P-256, P-384 and secp256k1 against the RFC 6979 vectors      |
| `ed25519`    | Ed25519 against the RFC 8032 section 7.1 vectors                      |
| `bio_start`  | A started core runs nothing before pc 0 of its new program            |
| `bio_isa`    | 32 bit and compressed instructions checked against host arithmetic    |
| `bio_fifo`   | FIFO depth, empty and full behaviour, level events and x30 waits      |
| `bio_div`    | Quantum divider periods, fractional dividers and lock step starts     |
| `bio_gpio`   | GPIO mask, event registers, clock count and trap                      |
| `bio_irq`    | BIO interrupts to the CPU and DMA request routing                     |
| `bio_ext`    | Core 1 clocked by core 0 on BIO pin 2                                 |
| `bio_snap`   | Snap to quantum on input and output                                   |
| `bio_filter` | DMA filter windows and the gutter for refused accesses                |

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

## MMU Sandbox (BAO_MMU, opt in)

`BAO_MMU=1 ./bao_flash.sh build your_program`, or a file named `bao_mmu` in the example's folder, builds the program to run in supervisor mode under an identity mapped Sv32 page table. Without it nothing changes: the build commands, and the binaries they make, are what they always were.

On Windows, `set BAO_MMU=1` before `bao_flash build` or `bao_flash run` does the same, and `set BAO_MMU=` goes back to normal builds.

In a sandbox build a stray store from your code cannot reach the boot chain. The page table maps only the program image (read and execute), runtime storage (read only), your stacks and data, the IFRAM DMA buffers and the peripheral pages the SDK drivers use. boot0, boot1, the one way counters, the security region, the IFR and every register block from 0x40000000 to 0x4007FFFF (RRC, SCE, CGU, WDT, ATIMER, EVC, AON, RTC) are unmapped, so touching them ends in a page fault. 

The main stack has an unmapped guard page below it, so a runaway recursion ends in a fault as well. A fault prints the cause, the faulting address and the pc on the console and halts:

```
MMU: store page fault at 0x61000fd0, pc 0x60065912, supervisor mode
MMU: below the main stack, stack overflow
MMU: halted
```

The SHA, AES, TRNG, PKE, ALU, RRAM write, WDT and RTC drivers, bio_init's clock setting and bao_software_reset still work from your code: they run in machine mode behind an ecall, unchanged, with every pointer you pass them checked against your page table and lengths capped at 2 MB. rram_write and friends reach runtime storage only (0x60160000 up); a write into the program region faults. 

Interrupts and the irq, timer and event calls work as before, delivered to supervisor mode.

What a sandbox build cannot do are read or write the machine mode CSRs (mstatus, mie, mcycle, the 0xBC0 and 0xFC0 arrays), reach the mailbox, the MDMA or any register block the SDK drivers do not use, use the PKE helper calls that take SCE register pointers (pke_run, pke_seg_*, pke_wait_fr, pke_sce_prepare, pke_bn_*).

*NOTE ON USB CDC: the sandbox build does not compile TinyUSB, so `usb_cdc` builds as a normal program only. Use `sstatus`, `sie`, the cycle CSR and the SDK calls instead. Every sandbox image carries all the machine mode drivers (about 23 KB more than a plain build), and SRAM loses its bottom 132 KB to machine mode data and stack, the page tables, the stacks and their guard pages. `src/mmu/bao_mmu.c` has the details.


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
- **The RTC has no 32 kHz crystal** on the Dabao. Its internal RC oscillator measures about 24 kHz and varies per chip, so the 1 Hz divider has to be calibrated against the TickTimer.
- **PKE calls clear the SCE RAM**, AES key included. Set the AES key again after any PKE or signature call.

## License

Apache-2.0


## Going deeper

The SDK gets you running. If you want the register level detail behind it,
including the undocumented behaviour, I wrote [The Dabao Book](https://payhip.com/b/EwSZv).
Thank you for your support!
