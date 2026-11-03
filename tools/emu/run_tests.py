#!/usr/bin/env python3
"""
run_tests.py: drive fw/test.elf through baochip_emu.py and check results.

Copyright (c) 2026 Armstrong Subero
SPDX-License-Identifier: Apache-2.0

Usage:
  python3 run_tests.py [fw/test.elf]

Case 0 runs the guard and trap checks to test_done and compares results[].
Cases 1..5 and 9 must stop in sevs_target_halt with a crash record for the
expected source line. Cases 6..8 are rig self tests: the firmware behaves
like a careless application and the model must refuse the access.
"""

import struct
import sys

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from baochip_emu import BaochipRig

ELF = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), 'fw', 'test.elf')
RIG_CASE_ADDR = 0x50030000
SEVS_CRASH_MAGIC = None


def make_rig(case):
    rig = BaochipRig()
    rig.load_elf(ELF)
    rig.mu.mem_write(RIG_CASE_ADDR, struct.pack('<I', case))
    return rig


def crash_record(rig):
    base = rig.sym('_sevs_crash_start')
    raw = rig.mu.mem_read(base, 192)
    magic, kind, line = struct.unpack('<III', raw[:12])
    expr = raw[12:76].split(b'\0')[0].decode(errors='replace')
    fil = raw[76:140].split(b'\0')[0].decode(errors='replace')
    return magic, kind, line, expr, fil


def run_case0():
    rig = make_rig(0)
    ok = rig.run('test_done')
    print("case 0: %d instructions, uart=%r" % (rig.insns, bytes(rig.uart_out)))
    if not ok:
        for v in rig.violations:
            print("  FATAL " + v)
        return False
    base = rig.sym('results')
    r = [rig.read_u32(base + 4 * i) for i in range(40)]
    main_top = rig.sym('_main_stack_top')
    trap_top = rig.sym('_trap_stack_top')
    NEG1 = 0xFFFFFFFF
    checks = [
        ("init_data copied from RRAM", r[0] == 0x12345678),
        (".bss cleared", r[1] == 0),
        ("rram_write boot0 refused", r[2] == NEG1),
        ("rram_write boot1 refused", r[3] == NEG1),
        ("rram_write straddling boot1 end refused", r[4] == NEG1),
        ("rram_write security region refused", r[5] == NEG1),
        ("rram_write straddling into security refused", r[6] == NEG1),
        ("rram_write wrapping length refused", r[7] == NEG1),
        ("rram_write_page boot0 refused", r[8] == NEG1),
        ("rram_write_page unaligned straddle refused", r[9] == NEG1),
        ("rram_erase_page boot1 refused", r[10] == NEG1),
        ("rram_write user storage ok", r[11] == 0),
        ("rram_write_page last valid page ok", r[12] == 0),
        ("rram_write first program page ok", r[13] == 0),
        ("bio_allow_range wrap refused", r[14] == NEG1),
        ("bio_allow_range zero length refused", r[16] == NEG1),
        ("bio_allow_range SRAM ok", r[18] == 0),
        ("bio_allow_range program+storage ok", r[19] == 0),
        ("bio_dma_start misaligned dst refused", r[20] == NEG1),
        ("bio_dma_start misaligned src refused", r[21] == NEG1),
        ("bio_dma_start wrapping src refused", r[22] == NEG1),
        ("MIE set after irq_init", r[23] == 8),
        ("timer handler ran once", r[24] == 1),
        ("handler sp on trap stack", main_top < r[25] <= trap_top),
        ("MIE restored after interrupt", r[26] == 8),
        ("all registers restored across trap (bits)", r[27] == 0),
        ("handler ran again inside register test", r[28] == 2),
        ("MIE restored after recoverable SEVS_CHECK", r[31] == 8),
        ("rig console marker read", r[32] == 0xC0FFEE),
        ("end marker", r[33] == 0xC0DEC0DE),
        ("RRC_CR only written 0xFC00/0xFC02", all(v in (0xFC00, 0xFC02) for v in rig.rrc_cr_writes) and len(rig.rrc_cr_writes) > 0),
        ("one interrupt delivered per request", rig.irq_delivered == 2),
    ]
    allok = True
    for name, passed in checks:
        print("  %s %s" % ("PASS" if passed else "FAIL", name))
        allok &= passed
    if r[27] != 0:
        print("  register mismatch bits: 0x%08X" % r[27])
    print("  results: " + " ".join("%d=%08X" % (i, v) for i, v in enumerate(r[:34])))
    # verify the staged data landed in the RRAM model
    data = bytes(rig.mu.mem_read(0x60300000, 40))
    expect = bytes((0xA0 + i) & 0xFF for i in range(40))
    passed = data == expect
    print("  %s rram_write data visible in RRAM model" % ("PASS" if passed else "FAIL"))
    allok &= passed
    print("  rrc_cr writes: %d, uart bytes: %d" % (len(rig.rrc_cr_writes), len(rig.uart_out)))
    return allok


def run_halt_case(case, expect_file, expect_func):
    rig = make_rig(case)
    halt = rig.sym('sevs_target_halt')
    ok = rig.run_until(halt)
    if not ok:
        print("case %d: FAIL did not halt: %s" % (case, rig.violations))
        return False
    magic, kind, line, expr, fil = crash_record(rig)
    results1 = rig.read_u32(rig.sym('results') + 4)
    # The record's file field is 64 bytes and keeps the start of a long
    # path, so match on the UART line, which carries the whole path.
    uart = bytes(rig.uart_out).decode(errors='replace')
    good = magic == 0x53455653 and results1 != 0xBAD0BAD0 \
        and expect_file in uart and expect_func in uart
    print("case %d: %s halted in %s:%d (%s) kind=%d uart=%r" % (
        case, "PASS" if good else "FAIL", fil.split('/')[-1], line, expr, kind, bytes(rig.uart_out)))
    return good


def run_self_test(case, expect_text):
    rig = make_rig(case)
    ok = rig.run('test_done')
    caught = (not ok) and any(expect_text in v for v in rig.violations)
    print("case %d: %s rig %s: %s" % (case, "PASS" if caught else "FAIL",
                                       "caught" if caught else "missed", rig.violations))
    return caught


def main():
    allok = run_case0()
    allok &= run_halt_case(1, 'bio.c', 'bio_allow_range')
    allok &= run_halt_case(2, 'bio.c', 'bio_allow_range')
    allok &= run_halt_case(3, 'bio.c', 'bio_allow_range')
    allok &= run_halt_case(4, 'bio_dma.c', 'bio_dma_start')
    allok &= run_halt_case(5, 'bio.c', 'bio_load_code')
    allok &= run_halt_case(9, 'bio.c', 'bio_allow_range')
    allok &= run_self_test(6, 'boot0/boot1')
    allok &= run_self_test(7, 'RRC touched')
    allok &= run_self_test(8, 'security region')
    print("ALL PASS" if allok else "FAILURES")
    sys.exit(0 if allok else 1)


if __name__ == '__main__':
    main()
