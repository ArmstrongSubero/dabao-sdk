#!/bin/bash
# Build tools/emu/fw/test.elf against this SDK tree (GitHub layout).
# usage: tools/emu/build_fw.sh [path to riscv-none-elf- prefix]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CROSS="${1:-${CROSS:-riscv-none-elf-}}"
ARCH="-march=rv32imac_zicsr_zifencei -mabi=ilp32"
CFLAGS="-Os -Wall -Wextra -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns -g -ffunction-sections -fdata-sections"
INC="-I$ROOT/src/common/bao_base/include -I$ROOT/src/common/bao_stdlib/include -I$ROOT/src/sevs -I$ROOT/src/boards/include"
for d in $ROOT/src/bao1x/hardware_*/include; do INC="$INC -I$d"; done
OUT="$HERE/fw/build"
rm -rf "$OUT"; mkdir -p "$OUT"
OBJS=""
for f in $ROOT/src/bao1x/hardware_*/*.c $ROOT/src/common/bao_stdlib/*.c $ROOT/src/sevs/sevs_assert_target.c; do
    o="$OUT/$(basename ${f%.c}).o"
    ${CROSS}gcc $ARCH $CFLAGS $INC -c -o "$o" "$f"
    OBJS="$OBJS $o"
done
${CROSS}gcc $ARCH -g -c -o "$OUT/crt0.o" "$ROOT/src/runtime/crt0.S"
${CROSS}gcc $ARCH $CFLAGS $INC -c -o "$OUT/test_main.o" "$HERE/fw/test_main.c"
${CROSS}gcc $ARCH -g -c -o "$OUT/test_asm.o" "$HERE/fw/test_asm.S"
${CROSS}gcc $ARCH -T "$ROOT/bao1x.ld" -nostdlib -nostartfiles -Wl,--gc-sections \
    -o "$HERE/fw/test.elf" "$OUT/crt0.o" "$OUT/test_main.o" "$OUT/test_asm.o" $OBJS -lgcc
echo "built $HERE/fw/test.elf"
