#!/bin/bash
# bao.sh - Unified build and flash tool for the Dabao SDK
#
# Usage:
#   ./bao.sh build <example>                         Build only
#   ./bao.sh flash <port> <example>                  Flash (normal, bootwait on)
#   ./bao.sh flash <port> <example> --persistent     Flash + auto-run on reset
#   ./bao.sh flash <port> <example> --icsp <dtr>     Zero-button via DTR reset
#   ./bao.sh flash <port> <example> --icsp-single    Single-port ICSP
#   ./bao.sh run <port> <example>                    Build + flash in one shot
#   ./bao.sh run <port> <example> --persistent       Build + flash persistent
#   ./bao.sh run <port> <example> --icsp <dtr>       Build + flash ICSP
#   ./bao.sh run <port> <example> --icsp-single      Build + flash single-port ICSP
#   ./bao.sh list                                    List available examples
#
# Examples:
#   ./bao.sh build blink
#   ./bao.sh flash /dev/ttyACM0 blink
#   ./bao.sh flash /dev/ttyACM0 blink --persistent
#   ./bao.sh flash /dev/ttyACM0 blink --icsp /dev/ttyUSB0
#   ./bao.sh flash /dev/ttyUSB0 blink --icsp-single
#   ./bao.sh run /dev/ttyACM0 hello_uart

set -e

# ---- Toolchain detection ----
if [ -x "xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc" ]; then
    CROSS="xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-"
else
    CROSS="riscv-none-elf-"
fi

ARCH="-march=rv32imac_zicsr_zifencei -mabi=ilp32"
CFLAGS="-Os -Wall -Wextra -ffreestanding -nostdlib -g"

INC="-Isrc/common/bao_base/include"
INC="$INC -Isrc/common/bao_stdlib/include"
INC="$INC -Isrc/bao1x/hardware_regs/include"
INC="$INC -Isrc/bao1x/hardware_gpio/include"
INC="$INC -Isrc/bao1x/hardware_uart/include"
INC="$INC -Isrc/bao1x/hardware_pwm/include"
INC="$INC -Isrc/bao1x/hardware_spi/include"
INC="$INC -Isrc/bao1x/hardware_i2c/include"
INC="$INC -Isrc/bao1x/hardware_adc/include"
INC="$INC -Isrc/bao1x/hardware_trng/include"
INC="$INC -Isrc/bao1x/hardware_wdt/include"
INC="$INC -Isrc/bao1x/hardware_rtc/include"
INC="$INC -Isrc/bao1x/hardware_bio/include"
INC="$INC -Isrc/bao1x/hardware_timer/include"
INC="$INC -Isrc/bao1x/hardware_bio_dma/include"
INC="$INC -Isrc/bao1x/hardware_irq/include"
INC="$INC -Isrc/bao1x/hardware_rram/include"
INC="$INC -Isrc/bao1x/hardware_aes/include"
INC="$INC -Isrc/bao1x/hardware_sha/include"
INC="$INC -Isrc/bao1x/hardware_qspi/include"
INC="$INC -Isrc/bao1x/hardware_w25q/include"
INC="$INC -Isrc/bao1x/hardware_reset/include"
INC="$INC -Isrc/boards/include"
INC="$INC -Isrc/sevs"
INC="$INC -Ithird_party/fatfs"

# ---- SDK source list ----
SDK_SRCS="
    src/bao1x/hardware_gpio/gpio.c
    src/bao1x/hardware_uart/uart.c
    src/bao1x/hardware_pwm/pwm.c
    src/bao1x/hardware_spi/spi.c
    src/bao1x/hardware_i2c/i2c.c
    src/bao1x/hardware_adc/adc.c
    src/bao1x/hardware_trng/trng.c
    src/bao1x/hardware_wdt/wdt.c
    src/bao1x/hardware_rtc/rtc.c
    src/bao1x/hardware_bio/bio.c
    src/bao1x/hardware_bio_dma/bio_dma.c
    src/bao1x/hardware_irq/irq.c
    src/bao1x/hardware_rram/rram.c
    src/bao1x/hardware_aes/aes.c
    src/bao1x/hardware_sha/sha.c
    src/bao1x/hardware_qspi/qspi.c
    src/bao1x/hardware_w25q/w25q.c
    src/common/bao_stdlib/stdio.c
    src/common/bao_stdlib/delay.c
    src/common/bao_stdlib/stdlib.c
    src/sevs/sevs_assert_target.c
"

# ---- Functions ----

usage() {
    echo "bao.sh - Dabao SDK build and flash tool"
    echo ""
    echo "Usage:"
    echo "  ./bao.sh build <example>"
    echo "  ./bao.sh flash <port> <example> [--persistent|--icsp <dtr>|--icsp-single]"
    echo "  ./bao.sh run   <port> <example> [--persistent|--icsp <dtr>|--icsp-single]"
    echo "  ./bao.sh verify"
    echo "  ./bao.sh list"
    echo ""
    echo "Flash modes:"
    echo "  (default)       Normal flash, bootwait on. Press RESET to re-enter boot1."
    echo "  --persistent    Auto-run on reset/power cycle. Hold PROG to re-enter boot1."
    echo "  --icsp <dtr>    Zero-button reflash. DTR port resets board, CDC port flashes."
    echo "  --icsp-single   Single-port ICSP. CP2104 DTR+TX/RX, no USB cable needed."
    echo ""
    echo "Examples:"
    echo "  ./bao.sh build blink"
    echo "  ./bao.sh flash /dev/ttyACM0 blink"
    echo "  ./bao.sh run /dev/ttyACM0 hello_uart --persistent"
    echo "  ./bao.sh run /dev/ttyACM0 blink --icsp /dev/ttyUSB0"
    exit 0
}

list_examples() {
    echo "Available examples:"
    for d in examples/*/; do
        echo "  $(basename "$d")"
    done
    exit 0
}

do_build() {
    local EXAMPLE="$1"
    local EXAMPLE_DIR="examples/$EXAMPLE"

    if [ ! -d "$EXAMPLE_DIR" ]; then
        echo "Error: example \"$EXAMPLE\" not found in examples/"
        exit 1
    fi

    echo ""
    echo "============================================"
    echo "  Building: $EXAMPLE"
    echo "============================================"
    echo ""

    mkdir -p build

    echo "[1/4] Assembling startup ..."
    ${CROSS}gcc $ARCH -g -c -o build/crt0.o src/runtime/crt0.S

    echo "[2/4] Compiling SDK ..."
    for f in $SDK_SRCS; do
        name=$(basename "$f" .c)
        ${CROSS}gcc $ARCH $CFLAGS $INC -c -o "build/${name}.o" "$f"
    done

    echo "[3/4] Compiling $EXAMPLE ..."
    MAIN_SRC=$(ls "$EXAMPLE_DIR"/*.c 2>/dev/null | head -1)
    if [ -z "$MAIN_SRC" ]; then
        echo "Error: no .c file found in $EXAMPLE_DIR"
        exit 1
    fi
    ${CROSS}gcc $ARCH $CFLAGS $INC -c -o build/main.o "$MAIN_SRC"

    # Third-party libraries
    EXTRA_OBJS=""
    if [ "$EXAMPLE" = "fatfs" ]; then
        echo "       Compiling FatFS ..."
        ${CROSS}gcc $ARCH $CFLAGS $INC -Ithird_party/fatfs -c -o build/ff.o third_party/fatfs/ff.c
        ${CROSS}gcc $ARCH $CFLAGS $INC -Ithird_party/fatfs -c -o build/diskio.o third_party/fatfs/diskio.c
        EXTRA_OBJS="build/ff.o build/diskio.o"
    fi

    echo "[4/4] Linking $EXAMPLE.uf2 ..."
    ${CROSS}gcc $ARCH -T bao1x.ld -nostdlib -nostartfiles -Wl,--gc-sections \
        -o "build/${EXAMPLE}.elf" \
        build/crt0.o build/gpio.o build/uart.o build/pwm.o build/spi.o \
        build/i2c.o build/adc.o build/trng.o build/wdt.o build/rtc.o \
        build/bio.o build/bio_dma.o build/irq.o build/rram.o build/aes.o build/sha.o \
        build/qspi.o build/w25q.o \
        build/stdio.o build/delay.o build/stdlib.o build/sevs_assert_target.o \
        build/main.o \
        $EXTRA_OBJS -lgcc

    ${CROSS}objcopy -O binary "build/${EXAMPLE}.elf" "build/${EXAMPLE}.bin"

    python3 tools/sign_and_uf2.py "build/${EXAMPLE}.bin" "build/${EXAMPLE}.uf2" 0x60060000 0xa7d76373

    echo ""
    ${CROSS}size "build/${EXAMPLE}.elf"
    echo ""
    echo "============================================"
    echo "  BUILD SUCCESS: build/${EXAMPLE}.uf2"
    echo "============================================"
    echo ""
}

do_flash() {
    local PORT="$1"
    local EXAMPLE="$2"
    shift 2

    local UF2="build/${EXAMPLE}.uf2"
    if [ ! -f "$UF2" ]; then
        echo "Error: $UF2 not found. Run \"./bao.sh build $EXAMPLE\" first."
        exit 1
    fi

    local FLASH_ARGS=""

    while [ $# -gt 0 ]; do
        case "$1" in
            --persistent)
                FLASH_ARGS="--persistent"
                echo "Flashing $UF2 via $PORT (persistent mode) ..."
                python3 tools/serial_flash.py $FLASH_ARGS "$PORT" "$UF2"
                return
                ;;
            --icsp)
                if [ -z "$2" ]; then
                    echo "Error: --icsp requires a DTR port argument"
                    exit 1
                fi
                local DTR_PORT="$2"
                shift
                echo "Flashing $UF2 via $DTR_PORT + $PORT (ICSP mode) ..."
                python3 tools/serial_flash.py --icsp "$DTR_PORT" "$PORT" "$UF2"
                return
                ;;
            --icsp-single)
                echo "Flashing $UF2 via $PORT (single-port ICSP) ..."
                python3 tools/serial_flash.py --icsp-single "$PORT" "$UF2"
                return
                ;;
            *)
                echo "Unknown flash option: $1"
                exit 1
                ;;
        esac
        shift
    done

    # Default: normal flash
    echo "Flashing $UF2 via $PORT ..."
    python3 tools/serial_flash.py "$PORT" "$UF2"
}

do_verify() {
    python3 tools/sevs/verify.py
}

# ---- Main ----

if [ $# -eq 0 ]; then
    usage
fi

CMD="$1"
shift

case "$CMD" in
    build)
        if [ -z "$1" ]; then
            echo "Usage: ./bao.sh build <example>"
            exit 1
        fi
        do_build "$1"
        ;;
    flash)
        if [ -z "$2" ]; then
            echo "Usage: ./bao.sh flash <port> <example> [--persistent|--icsp <dtr>|--icsp-single]"
            exit 1
        fi
        do_flash "$@"
        ;;
    run)
        if [ -z "$2" ]; then
            echo "Usage: ./bao.sh run <port> <example> [--persistent|--icsp <dtr>|--icsp-single]"
            exit 1
        fi
        PORT="$1"
        EXAMPLE="$2"
        shift 2
        do_build "$EXAMPLE"
        do_flash "$PORT" "$EXAMPLE" "$@"
        ;;
    list)
        list_examples
        ;;
    verify)
        do_verify
        ;;
    *)
        echo "Unknown command: $CMD"
        echo ""
        usage
        ;;
esac