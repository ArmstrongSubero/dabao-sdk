#!/usr/bin/env python3
"""
run_pke.py: run the pke_bigint and pke_ecc examples in the Baochip-1x
model with the SCE model attached, and check every test passes with no
watcher or model violations.

Copyright (c) 2026 Armstrong Subero
SPDX-License-Identifier: Apache-2.0

Usage:
  python3 run_pke.py <build folder with pke_bigint.elf and pke_ecc.elf>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from baochip_emu import BaochipRig
from unicorn.riscv_const import UC_RISCV_REG_PC


def run(elf, budget=60000000):
    rig = BaochipRig()
    rig.max_insns = budget
    rig.load_elf(elf)
    rig.run_until(None)
    v = [x for x in rig.violations if 'without reaching' not in x]
    if len(v) != len(rig.violations):
        pc = rig.mu.reg_read(UC_RISCV_REG_PC)
        here = struct.unpack('<I', rig.mu.mem_read(pc, 4))[0]
        before = struct.unpack('<I', rig.mu.mem_read(pc - 4, 4))[0]
        if 0x10500073 not in (here, before):
            v.append("stopped away from wfi at 0x%08X" % pc)
    return rig, v


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else '.'
    ok = True
    for name in ('pke_bigint', 'pke_ecc'):
        rig, v = run(os.path.join(folder, name + '.elf'))
        text = bytes(rig.uart_out).decode(errors='replace')
        print("==== %s (%d instructions, %d SCE RAM clears)" % (name, rig.insns, rig.sce.ram_clears))
        print(text.strip())
        for x in v + rig.sce.violations:
            print("  VIOLATION: " + x)
        for w in rig.warnings:
            print("  warn: " + w)
        passed = 'all tests passed' in text and not v and not rig.sce.violations
        print("  RESULT: %s" % ("PASS" if passed else "FAIL"))
        ok &= passed
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
