#!/usr/bin/env python3
"""Usage: python3 sweep_examples.py [glob of ELF files]

Run every built example under the Baochip-1x watchers for a
bounded number of instructions. Budget exhaustion is fine (examples loop
forever); any other stop is a finding."""
import glob, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from baochip_emu import BaochipRig
from unicorn.riscv_const import UC_RISCV_REG_PC

PATTERN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', '*.elf')

ok = True
for elf in sorted(glob.glob(PATTERN)):
    name = os.path.basename(elf)[:-4]
    rig = BaochipRig()
    rig.max_insns = 3000000
    rig.load_elf(elf)
    rig.run_until(None)
    v = [x for x in rig.violations if 'budget exhausted' not in x]
    if len(v) == 1 and 'without reaching' in v[0]:
        pc = rig.mu.reg_read(UC_RISCV_REG_PC)
        here = struct.unpack('<I', rig.mu.mem_read(pc, 4))[0]
        before = struct.unpack('<I', rig.mu.mem_read(pc - 4, 4))[0]
        if 0x10500073 in (here, before):   # wfi: unicorn stops the core here
            v = []
    status = 'ok' if not v else 'FINDING'
    ok &= not v
    uart = bytes(rig.uart_out)[:70]
    print("%-18s %-8s insns=%-8d irq=%d uart=%r" % (name, status, rig.insns, rig.irq_delivered, uart))
    for x in v:
        print("    " + x)
    for x in rig.warnings:
        print("    warn: " + x)
sys.exit(0 if ok else 1)
