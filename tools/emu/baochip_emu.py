#!/usr/bin/env python3
"""
baochip_emu.py: a Baochip-1x memory map model on unicorn.

Copyright (c) 2026 Armstrong Subero
SPDX-License-Identifier: Apache-2.0

Usage:
  python3 baochip_emu.py <firmware.elf>

The model is not the chip. It is the address map, the RRAM write rules,
the custom VexRiscv CSRs, interrupt delivery through mtvec, and watchers
that stop the run the moment code touches anything that can brick a real
board. It exists to run SDK code under those watchers before the code is
ever flashed.

Watched, fatal on any touch:
  0x40000000 to 0x4000FFFF   RRC (RRAM controller)
                             except a 32 bit write of 0xFC00 or 0xFC02 to RRC_CR
                             (security mode bits set, as Xous writes them)
  0x40040000 to 0x4007FFFF   CGU, sysctrl, EVC, WDT, ATIMER, AON, RTC
                             except the reviewed driver accesses listed in
                             BaochipRig, which are reported as warnings
  0x60000000 to 0x6005FFFF   boot0 and boot1 (any store)
  0x603DA000 to 0x603FFFFF   one way counters, access control, key slots
                             (any access)

RRAM model:
  Mapped read and execute only. Stores are caught by the write protect
  hook and applied by hand. In RRC normal mode a store is a staging store
  and lands in memory. In RRC write command mode a store is a command word
  (0x5200 load buffer, 0x9528 write buffer) and must be inside the page
  that was staged; anything else is fatal, because on the real part the
  commit goes to the last address touched.

Custom CSRs:
  0xBC0 MIM and 0xFC0 MIP are VexRiscv specific. unicorn raises an illegal
  instruction for them; the hook emulates the access. The .word 0x500F
  cache flush is accepted as a no op.

Interrupts:
  A store to 0xE00FF000 asks the rig to deliver a machine external
  interrupt (mcause 0x8000000B) a few instructions later, with the written
  value showing in MIP. Delivery runs the firmware's own _trap_entry.

The rig also models the UDMA UART TX path as instant completion and
collects the bytes, so mini_printf output is readable in the log.
"""

import struct
import sys

from unicorn import (Uc, UcError, UC_ARCH_RISCV, UC_MODE_RISCV32,
                     UC_HOOK_INTR, UC_HOOK_CODE, UC_HOOK_MEM_READ,
                     UC_HOOK_MEM_WRITE, UC_HOOK_MEM_WRITE_PROT,
                     UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED,
                     UC_HOOK_MEM_FETCH_UNMAPPED,
                     UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_PROT_ALL,
                     UC_MEM_READ, UC_MEM_WRITE)
from unicorn.riscv_const import (UC_RISCV_REG_PC, UC_RISCV_REG_SP,
                                 UC_RISCV_REG_X0, UC_RISCV_REG_MEPC,
                                 UC_RISCV_REG_MCAUSE, UC_RISCV_REG_MSTATUS,
                                 UC_RISCV_REG_MTVEC, UC_RISCV_REG_MIE,
                                 UC_RISCV_REG_PRIV)
from elftools.elf.elffile import ELFFile


RRAM_BASE = 0x60000000
RRAM_SIZE = 0x00400000
SRAM_BASE = 0x61000000
SRAM_SIZE = 0x00200000
IFRAM_BASE = 0x50000000
IFRAM_SIZE = 0x00040000
PERIPH_AO_BASE = 0x40000000
PERIPH_AO_SIZE = 0x00100000
PERIPH_UDMA_BASE = 0x50100000
PERIPH_UDMA_SIZE = 0x00100000
PERIPH_CPU_BASE = 0xE0000000
PERIPH_CPU_SIZE = 0x00100000

RRC_CR = 0x40000000
RRC_CR_NORMAL = 0xFC00
RRC_CR_WRITE_CMD = 0xFC02
RRC_LOAD_BUFFER = 0x5200
RRC_WRITE_BUFFER = 0x9528

BOOT_END = 0x60060000
SECURITY_BASE = 0x603DA000

TICKTIMER_TIME1 = 0xE001B004
TICKTIMER_TIME0 = 0xE001B008
TICKTIMER_EV_PENDING = 0xE001B018
TIMER0_EV_PENDING = 0xE001C018

RIG_INJECT_IRQ = 0xE00FF000
RIG_CONSOLE = 0xE00FF004

UDMA_UART_BASES = (0x50101000, 0x50102000, 0x50103000, 0x50104000)
UDMA_TX_SADDR = 0x10
UDMA_TX_SIZE = 0x14
UDMA_TX_CFG = 0x18
UDMA_CFG_EN = 1 << 4

CSR_MIM = 0xBC0
CSR_MIP = 0xFC0

MSTATUS_MIE = 0x8
MSTATUS_MPIE = 0x80
MSTATUS_MPP = 0x1800

INSNS_PER_MS = 350000


class RigStop(Exception):
    pass


class RigFatal(Exception):
    pass


class BaochipRig:
    def __init__(self):
        self.mu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        mu = self.mu
        mu.mem_map(PERIPH_AO_BASE, PERIPH_AO_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        mu.mem_map(IFRAM_BASE, IFRAM_SIZE, UC_PROT_ALL)
        mu.mem_map(PERIPH_UDMA_BASE, PERIPH_UDMA_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        mu.mem_map(RRAM_BASE, RRAM_SIZE, UC_PROT_READ | UC_PROT_EXEC)
        mu.mem_map(SRAM_BASE, SRAM_SIZE, UC_PROT_ALL)
        mu.mem_map(PERIPH_CPU_BASE, PERIPH_CPU_SIZE, UC_PROT_READ | UC_PROT_WRITE)

        self.rrc_mode = RRC_CR_NORMAL
        self.staged_page = None
        self.rrc_cr_writes = []
        self.csr = {CSR_MIM: 0, CSR_MIP: 0}
        self.insns = 0
        self.inject_at = None
        self.inject_mip = 0
        self.irq_delivered = 0
        self.uart_out = bytearray()
        self.console_marker = 0xC0FFEE
        self.violations = []
        self.warnings = []
        self.warned = set()
        self.stop_pc = None
        self.max_insns = 50000000
        self.symbols = {}
        self.insns_per_ms = INSNS_PER_MS
        self.post_actions = []
        from sce_model import SceModel
        self.sce = SceModel(self)

        mu.hook_add(UC_HOOK_INTR, self._hook_intr)
        mu.hook_add(UC_HOOK_CODE, self._hook_code)
        mu.hook_add(UC_HOOK_MEM_WRITE_PROT, self._hook_rram_write,
                    begin=RRAM_BASE, end=RRAM_BASE + RRAM_SIZE - 1)
        mu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self._hook_ao,
                    begin=PERIPH_AO_BASE, end=PERIPH_AO_BASE + PERIPH_AO_SIZE - 1)
        mu.hook_add(UC_HOOK_MEM_WRITE, self._hook_sce,
                    begin=0x40020000, end=0x4002FFFF)
        mu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self._hook_security,
                    begin=SECURITY_BASE, end=RRAM_BASE + RRAM_SIZE - 1)
        mu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self._hook_cpu_periph,
                    begin=PERIPH_CPU_BASE, end=PERIPH_CPU_BASE + PERIPH_CPU_SIZE - 1)
        mu.hook_add(UC_HOOK_MEM_WRITE, self._hook_udma,
                    begin=PERIPH_UDMA_BASE, end=PERIPH_UDMA_BASE + PERIPH_UDMA_SIZE - 1)
        mu.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED |
                    UC_HOOK_MEM_FETCH_UNMAPPED, self._hook_unmapped)

    # loading

    def load_elf(self, path):
        with open(path, 'rb') as f:
            elf = ELFFile(f)
            self.entry = elf.header['e_entry']
            for seg in elf.iter_segments():
                if seg['p_type'] != 'PT_LOAD' or seg['p_filesz'] == 0:
                    continue
                data = seg.data()[:seg['p_filesz']]
                self.mu.mem_write(seg['p_paddr'], data)
            symtab = elf.get_section_by_name('.symtab')
            if symtab is not None:
                for sym in symtab.iter_symbols():
                    if sym.name:
                        self.symbols[sym.name] = sym['st_value']
        # SRAM starts as garbage on a real part; fill with a pattern so a
        # missing .bss clear shows up.
        self.mu.mem_write(SRAM_BASE, b'\xCD' * 0x10000)

    def sym(self, name):
        return self.symbols[name]

    # helpers

    def _pc(self):
        return self.mu.reg_read(UC_RISCV_REG_PC)

    def _fatal(self, msg):
        self.violations.append(msg)
        self.mu.emu_stop()
        raise RigFatal(msg)

    def _where(self):
        pc = self._pc()
        best = None
        for name, addr in self.symbols.items():
            if addr <= pc and (best is None or addr > best[1]) and not name.startswith('.'):
                best = (name, addr)
        if best:
            return "pc=0x%08X (%s+0x%X)" % (pc, best[0], pc - best[1])
        return "pc=0x%08X" % pc

    # hooks

    def _hook_unmapped(self, uc, access, addr, size, value, user):
        self._fatal("access to unmapped address 0x%08X at %s" % (addr, self._where()))
        return False

    # Driver accesses inside the boot chain register region that have been
    # reviewed and run on hardware. They are reported as warnings so a sweep
    # still shows them, while anything not on this list is fatal.
    REVIEWED_CGU_WRITES = {
        (0x40040014, 0x070001FF),   # bio_init fast mode divider
        (0x40040014, 0x0700017F),   # bio_init slow mode divider
        (0x4004002C, 0x00000032),   # bio_init CGU set
        (0x40040080, 0x000055AA),   # bao_software_reset
    }
    REVIEWED_BLOCKS = (
        (0x40041000, 0x40042000, "WDT (watchdog, lock protected)"),
        (0x40043000, 0x40044000, "ATIMER"),
        (0x40060000, 0x40061000, "AON (RTC divider)"),
        (0x40061000, 0x40062000, "RTC"),
        (0x40063000, 0x40064000, "ATIMER_AO"),
    )

    def _warn(self, key, msg):
        if key not in self.warned:
            self.warned.add(key)
            self.warnings.append(msg)

    def _hook_ao(self, uc, access, addr, size, value, user):
        kind = "write" if access == UC_MEM_WRITE else "read"
        if PERIPH_AO_BASE <= addr < 0x40010000:
            if access == UC_MEM_WRITE and addr == RRC_CR and size == 4 \
                    and value in (RRC_CR_NORMAL, RRC_CR_WRITE_CMD):
                self.rrc_cr_writes.append(value)
                self.rrc_mode = value
                if value == RRC_CR_NORMAL:
                    self.staged_page = None
                return
            self._fatal("RRC touched: %s 0x%08X at %s" % (kind.upper(), addr, self._where()))
        if 0x40040000 <= addr < 0x40041000:
            if access == UC_MEM_WRITE and (addr, value) in self.REVIEWED_CGU_WRITES:
                self._warn(("cgu", addr, value),
                           "reviewed CGU write 0x%08X <= 0x%08X at %s" % (addr, value, self._where()))
                return
            self._fatal("CGU/sysctrl touched: %s 0x%08X at %s" % (kind, addr, self._where()))
        for lo, hi, name in self.REVIEWED_BLOCKS:
            if lo <= addr < hi:
                self._warn(("blk", lo), "boot chain register region: %s %s at 0x%08X from %s" % (
                    name, kind, addr, self._where()))
                return
        if 0x40040000 <= addr < 0x40080000:
            self._fatal("boot chain register region touched: %s 0x%08X at %s" % (
                kind, addr, self._where()))

    def post(self, fn):
        """Run fn after the current store has landed (next instruction)."""
        self.post_actions.append(fn)

    def _hook_sce(self, uc, access, addr, size, value, user):
        self.sce.on_write(addr, value, size)

    def _hook_security(self, uc, access, addr, size, value, user):
        self._fatal("security region touched: %s 0x%08X at %s" % (
            "write" if access == UC_MEM_WRITE else "read", addr, self._where()))

    def _hook_rram_write(self, uc, access, addr, size, value, user):
        if addr < BOOT_END:
            self._fatal("store into boot0/boot1: 0x%08X <= 0x%X at %s" % (addr, value, self._where()))
        if addr >= SECURITY_BASE:
            self._fatal("store into security region: 0x%08X at %s" % (addr, self._where()))
        if self.rrc_mode == RRC_CR_NORMAL:
            page = addr & ~31
            if self.staged_page is not None and page != self.staged_page:
                self._fatal("staging store crosses pages: 0x%08X after page 0x%08X at %s" % (
                    addr, self.staged_page, self._where()))
            self.staged_page = page
            uc.mem_write(addr, value.to_bytes(size, 'little'))
            return True
        # write command mode: the store is a command word
        if size != 4 or value not in (RRC_LOAD_BUFFER, RRC_WRITE_BUFFER):
            self._fatal("unexpected store in RRC write mode: 0x%08X <= 0x%X at %s" % (
                addr, value, self._where()))
        if self.staged_page is None or (addr & ~31) != self.staged_page:
            self._fatal("RRC commit aimed at 0x%08X but staged page is %s at %s" % (
                addr, hex(self.staged_page) if self.staged_page is not None else "none",
                self._where()))
        return True

    def _hook_cpu_periph(self, uc, access, addr, size, value, user):
        if access == UC_MEM_READ:
            if addr == TICKTIMER_TIME0:
                ms = self.insns // self.insns_per_ms
                uc.mem_write(TICKTIMER_TIME0, struct.pack('<I', ms & 0xFFFFFFFF))
            elif addr == TICKTIMER_TIME1:
                ms = self.insns // self.insns_per_ms
                uc.mem_write(TICKTIMER_TIME1, struct.pack('<I', ms >> 32))
            elif addr == RIG_CONSOLE:
                uc.mem_write(RIG_CONSOLE, struct.pack('<I', self.console_marker))
            return
        if addr == RIG_INJECT_IRQ:
            self.inject_mip = value
            self.inject_at = self.insns + 6
        elif addr == TIMER0_EV_PENDING or addr == TICKTIMER_EV_PENDING:
            # write one to clear
            pass

    def _hook_udma(self, uc, access, addr, size, value, user):
        for base in UDMA_UART_BASES:
            if addr == base + UDMA_TX_CFG and (value & UDMA_CFG_EN):
                saddr = struct.unpack('<I', uc.mem_read(base + UDMA_TX_SADDR, 4))[0]
                n = struct.unpack('<I', uc.mem_read(base + UDMA_TX_SIZE, 4))[0]
                if n and IFRAM_BASE <= saddr < IFRAM_BASE + IFRAM_SIZE:
                    self.uart_out += bytes(uc.mem_read(saddr, min(n, 4096)))
                elif n:
                    self._fatal("UART DMA source 0x%08X is not in IFRAM at %s" % (saddr, self._where()))
                # transfer completes at once: EN drops. The write itself
                # lands after this hook, so clear it on the next instruction.
                self._clear_cfg = base + UDMA_TX_CFG

    _clear_cfg = None

    def _hook_code(self, uc, addr, size, user):
        self.insns += 1
        if self.post_actions:
            acts, self.post_actions = self.post_actions, []
            for a in acts:
                a()
        if self._clear_cfg is not None:
            uc.mem_write(self._clear_cfg, struct.pack('<I', 0))
            self._clear_cfg = None
        if self.stop_pc is not None and addr == self.stop_pc:
            uc.emu_stop()
            raise RigStop()
        if self.insns > self.max_insns:
            self._fatal("instruction budget exhausted at %s" % self._where())
        if self.inject_at is not None and self.insns >= self.inject_at:
            self.inject_at = None
            mstatus = uc.reg_read(UC_RISCV_REG_MSTATUS)
            if mstatus & MSTATUS_MIE:
                self.csr[CSR_MIP] = self.inject_mip
                self._deliver_trap(uc, 0x8000000B, addr)
                self.irq_delivered += 1
            else:
                self._fatal("interrupt requested with MIE clear at %s" % self._where())

    def _deliver_trap(self, uc, mcause, epc):
        mstatus = uc.reg_read(UC_RISCV_REG_MSTATUS)
        new = mstatus & ~(MSTATUS_MIE | MSTATUS_MPIE | MSTATUS_MPP)
        if mstatus & MSTATUS_MIE:
            new |= MSTATUS_MPIE
        new |= MSTATUS_MPP
        uc.reg_write(UC_RISCV_REG_MSTATUS, new)
        uc.reg_write(UC_RISCV_REG_MEPC, epc)
        uc.reg_write(UC_RISCV_REG_MCAUSE, mcause)
        mtvec = uc.reg_read(UC_RISCV_REG_MTVEC) & ~3
        if mtvec == 0:
            self._fatal("trap with mtvec unset at %s" % self._where())
        uc.reg_write(UC_RISCV_REG_PC, mtvec)

    def _hook_intr(self, uc, intno, user):
        pc = uc.reg_read(UC_RISCV_REG_PC)
        fault_pc = pc - 4
        insn = struct.unpack('<I', uc.mem_read(fault_pc, 4))[0]
        if intno == 2:
            if insn == 0x0000500F:
                return
            if (insn & 0x7F) == 0x73 and ((insn >> 12) & 7) != 0:
                self._emulate_csr(uc, insn)
                return
            self._fatal("illegal instruction 0x%08X at pc=0x%08X" % (insn, fault_pc))
        if intno in (8, 9, 11):
            self._fatal("ecall at pc=0x%08X" % fault_pc)
        if intno == 3:
            self._fatal("ebreak at pc=0x%08X" % fault_pc)
        self._fatal("exception %d at pc=0x%08X" % (intno, fault_pc))

    def _emulate_csr(self, uc, insn):
        csr = insn >> 20
        rd = (insn >> 7) & 0x1F
        rs1 = (insn >> 15) & 0x1F
        funct3 = (insn >> 12) & 7
        if csr not in self.csr:
            self._fatal("unknown CSR 0x%03X at %s" % (csr, self._where()))
        old = self.csr[csr]
        src = rs1 if funct3 >= 5 else uc.reg_read(UC_RISCV_REG_X0 + rs1)
        op = funct3 & 3
        new = old
        if op == 1:
            new = src
        elif op == 2:
            if rs1 != 0:
                new = old | src
        elif op == 3:
            if rs1 != 0:
                new = old & ~src
        if csr == CSR_MIP:
            new = old   # read only
        self.csr[csr] = new & 0xFFFFFFFF
        if rd != 0:
            uc.reg_write(UC_RISCV_REG_X0 + rd, old)

    # run

    def run(self, stop_symbol):
        return self.run_until(self.sym(stop_symbol))

    def run_until(self, stop_pc):
        self.stop_pc = stop_pc
        self.mu.reg_write(UC_RISCV_REG_PRIV, 3)
        self.mu.reg_write(UC_RISCV_REG_MSTATUS, MSTATUS_MIE)  # boot1 hands over with MIE set
        self.mu.reg_write(UC_RISCV_REG_MIE, 0x800)
        try:
            self.mu.emu_start(self.entry, 0)
        except RigStop:
            return True
        except RigFatal:
            return False
        except UcError as e:
            if self.violations:
                return False
            self.violations.append("unicorn error: %s at %s" % (e, self._where()))
            return False
        self.violations.append("emulation ended without reaching %s" % (
            "0x%08X" % stop_pc if stop_pc is not None else "a stop"))
        return False

    def read_u32(self, addr):
        return struct.unpack('<I', self.mu.mem_read(addr, 4))[0]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    rig = BaochipRig()
    rig.load_elf(sys.argv[1])
    ok = rig.run('test_done')
    print("instructions: %d" % rig.insns)
    print("uart: %r" % bytes(rig.uart_out))
    if not ok:
        for v in rig.violations:
            print("FATAL: " + v)
        sys.exit(1)
    print("reached test_done")


if __name__ == '__main__':
    main()
