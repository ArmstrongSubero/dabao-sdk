#!/usr/bin/env python3
"""
sce_model.py: a functional model of the Baochip-1x SCE PKE, ALU and
internal DMA channel, for baochip_emu.py.

Copyright (c) 2026 Armstrong Subero
SPDX-License-Identifier: Apache-2.0

The semantics come from reading the CrossBar Daric SDK drivers (pke_rsa.c,
pke_curve.c, alu.c, sdma.c): what each function code reads, from which
segment and offset, and where it leaves its result. The model computes
the mathematical answer itself, so a driver that passes here moves the
right operands to the right places and waits correctly. It says nothing
about whether the silicon behaves the way the vendor code assumes.

Internal representations (the hardware's Montgomery and projective forms)
are modelled as plain affine values padded to the same sizes, which is
enough to check that intermediate results are copied where the next step
expects them.

Watched: any CPU write into SCE RAM outside the PKE segments
(0x40020800 to 0x40021BFF) while an operation is in flight is reported,
because it would land on the hash, AES or RNG buffers.
"""

import math
import struct

SCE_BASE = 0x40020000
SCE_RAM_END = 0x40028000
CTRL = 0x40028000
SDMA = 0x40029000
PKE = 0x4002C000
ALU = 0x4002F000

SEG_BASE = {9: SCE_BASE + 0x0C00, 11: SCE_BASE + 0x1400, 12: SCE_BASE + 0x1800}
PCON = SCE_BASE + 0x0800
PKB = SCE_BASE + 0x0800
PIB = SCE_BASE + 0x0C00
POB = SCE_BASE + 0x1400
PKE_LO = SCE_BASE + 0x0800
PKE_HI = SCE_BASE + 0x1C00

CLK_SDMA, CLK_AES, CLK_PKE, CLK_HASH, CLK_ALU = 1, 2, 4, 8, 16

P25519 = 2**255 - 19
ED_D = (-121665 * pow(121666, -1, P25519)) % P25519


def words_to_int(ws):
    return sum(w << (32 * i) for i, w in enumerate(ws))


def int_to_words(v, n):
    return [(v >> (32 * i)) & 0xFFFFFFFF for i in range(n)]


class SceModel:
    def __init__(self, rig):
        self.rig = rig
        self.mu = rig.mu
        self.dead = False          # when True no operation ever completes
        self.ops = []              # function codes run, for the tests
        self.violations = []
        self.n = None
        self.curve = None          # ('w', p, a) | ('ed', p, d) | ('x', p, a24)
        self.inv_p = None
        self.ram_clears = 0
        self.ctrl_resets = 0

    # memory helpers
    def rd(self, addr, n=1):
        return list(struct.unpack('<%dI' % n, bytes(self.mu.mem_read(addr, 4 * n))))

    def wr(self, addr, ws):
        if ws:
            self.mu.mem_write(addr, struct.pack('<%dI' % len(ws), *[w & 0xFFFFFFFF for w in ws]))

    def reg(self, addr):
        return self.rd(addr)[0]

    def setreg(self, addr, v):
        self.wr(addr, [v])

    # rig hooks
    def on_write(self, addr, value, size):
        if SCE_BASE <= addr < SCE_RAM_END and not (PKE_LO <= addr < PKE_HI):
            self.violations.append("CPU write to SCE RAM outside the PKE segments: 0x%08X" % addr)
            return
        if addr == CTRL + 0x1C:
            self.rig.post(lambda: self.resetram(value))
        elif addr == SDMA + 0x00 and value == 0x5A:
            self.rig.post(self.ich)
        elif addr == PKE + 0x04 and value == 0x5A:
            self.rig.post(self.pke_op)
        elif addr == ALU + 0x04 and value == 0x5A:
            self.rig.post(self.alu_op)
        elif addr in (PKE + 0x0C, ALU + 0x0C, CTRL + 0x14):
            old = self.reg(addr)
            self.rig.post(lambda a=addr, o=old, v=value: self.setreg(a, o & ~v))
        elif addr == PKE + 0x08:
            old = self.reg(addr)
            self.rig.post(lambda o=old, v=value: self.setreg(PKE + 0x08, o & ~(v & 0x100)))

    # SCE control
    def resetram(self, value):
        if value == 0xA5:
            self.ram_clears += 1
            self.mu.mem_write(SCE_BASE, b'\0' * 0x2800)
            self.mu.mem_write(SCE_BASE + 0x4000, b'\0' * 0x2000)
        elif value == 0x5A:
            self.ctrl_resets += 1
            for off in (0x00, 0x04, 0x30):
                self.setreg(CTRL + off, 0)
        self.setreg(CTRL + 0x10, 0)

    def ich(self):
        if self.dead:
            return
        segid = self.reg(SDMA + 0x54)
        seg_w, seg_r = segid & 0xFF, (segid >> 8) & 0xFF
        off_r, off_w, n = self.reg(SDMA + 0x58), self.reg(SDMA + 0x5C), self.reg(SDMA + 0x60)
        if seg_r not in SEG_BASE or seg_w not in SEG_BASE:
            self.violations.append("ICH with unknown segment %d or %d" % (seg_r, seg_w))
            return
        src, dst = SEG_BASE[seg_r] + 4 * off_r, SEG_BASE[seg_w] + 4 * off_w
        for a in (src, src + 4 * n - 1, dst, dst + 4 * n - 1):
            if not (PKE_LO <= a < PKE_HI):
                self.violations.append("ICH reaches outside the PKE segments: 0x%08X" % a)
                return
        self.wr(dst, self.rd(src, n))
        self.setreg(CTRL + 0x14, self.reg(CTRL + 0x14) | (1 << 5))

    # PKE
    def pke_done(self):
        self.setreg(PKE + 0x0C, self.reg(PKE + 0x0C) | 1)

    def pke_op(self):
        if self.dead:
            return
        if not (self.reg(CTRL + 0x04) & CLK_PKE):
            self.violations.append("PKE started with its clock off")
            return
        fn = self.reg(PKE + 0x00) & 0xFF
        self.ops.append(fn)
        nw = self.reg(PKE + 0x10) & 0x3FFF
        ew = self.reg(PKE + 0x14) & 0x3FFF
        pcon, pib0, pib1 = self.reg(PKE + 0x30), self.reg(PKE + 0x34), self.reg(PKE + 0x38)
        pkb, pob = self.reg(PKE + 0x3C), self.reg(PKE + 0x40)
        h = getattr(self, 'fn_%02x' % fn, None)
        if h is None:
            self.violations.append("PKE function 0x%02X not modelled" % fn)
            return
        h(nw, ew, pcon, pib0, pib1, pkb, pob)
        self.pke_done()

    def pib(self, off, n):
        return words_to_int(self.rd(PIB + 4 * off, n))

    def put_pob(self, off, v, n):
        self.wr(POB + 4 * off, int_to_words(v, n))

    # arbitrary modulus
    def fn_11(self, nw, ew, pcon, *_):           # RSAINIT
        k = (nw + 31) // 32
        self.n = words_to_int(self.rd(PCON + 4 * pcon, k)) & ((1 << nw) - 1)
        if self.n % 2 == 0:
            self.violations.append("RSAINIT with an even modulus")

    def _nwords(self):
        return (self.n.bit_length() + 31) // 32

    def _modop(self, f, pib0, pib1, pob):
        if self.n is None or self.n == 0:
            return
        k = self._nwords()
        x, y = self.pib(pib0, k), self.pib(pib1, k)
        self.put_pob(pob, f(x, y) % self.n, k)

    def fn_12(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # RSAMM
        if nw == 6144 and self.reg(PKE + 0x24) == 0x100:
            return                                           # cache clearing multiply
        self._modop(lambda a, b: a * b, pib0, pib1, pob)

    def fn_19(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # RSAMA
        self._modop(lambda a, b: a + b, pib0, pib1, pob)

    def fn_1a(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # RSAMS
        self._modop(lambda a, b: a - b, pib0, pib1, pob)

    def fn_13(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # RSAME
        k = self._nwords()
        x = self.pib(pib0, k)
        e = words_to_int(self.rd(PKB + 4 * pkb, (ew + 31) // 32)) & ((1 << ew) - 1)
        self.put_pob(pob, pow(x, e, self.n), k)

    def fn_fe(self, nw, ew, pcon, *_):           # INVINIT
        self.inv_p = words_to_int(self.rd(PCON + 4 * pcon, (nw + 31) // 32)) & ((1 << nw) - 1)

    def fn_18(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # MODINV
        k = (nw + 31) // 32
        x = self.pib(pib0, k)
        if math.gcd(x, self.inv_p) != 1:
            return
        self.put_pob(pob, pow(x, -1, self.inv_p), k)
        self.setreg(PKE + 0x08, self.reg(PKE + 0x08) | 0x100)

    def fn_51(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # GCD
        d = nw // 32
        self.put_pob(pob, math.gcd(self.pib(pib0, d), self.pib(pib1, d)), d)

    # curves
    def fn_01(self, nw, ew, pcon, *_):           # ECINIT
        pw = ((nw + 63) // 64) * 2
        p = words_to_int(self.rd(PCON + 4 * pcon, pw))
        c = words_to_int(self.rd(PCON + 4 * (pcon + pw), pw))
        if p.bit_length() != nw:
            self.violations.append("ECINIT bit length %d does not match p" % nw)
        if p == P25519:
            self.curve = ('x', p, c) if c == 121665 else ('ed', p, c)
        else:
            self.curve = ('w', p, c % p)
        self.cw = pw                      # words per padded coordinate

    def _w_add(self, P, Q):
        _, p, a = self.curve
        if P is None: return Q
        if Q is None: return P
        if P[0] == Q[0] and (P[1] + Q[1]) % p == 0: return None
        if P == Q: l = (3 * P[0] * P[0] + a) * pow(2 * P[1], -1, p) % p
        else: l = (Q[1] - P[1]) * pow(Q[0] - P[0], -1, p) % p
        x = (l * l - P[0] - Q[0]) % p
        return (x, (l * (P[0] - x) - P[1]) % p)

    def _ed_add(self, P, Q):
        _, p, d = self.curve
        (x1, y1), (x2, y2) = P, Q
        t = d * x1 * x2 * y1 * y2 % p
        return ((x1 * y2 + x2 * y1) * pow(1 + t, -1, p) % p, (y1 * y2 + x1 * x2) * pow(1 - t, -1, p) % p)

    def _mul(self, add, k, P, zero):
        R = zero
        while k:
            if k & 1: R = add(R, P)
            P = add(P, P); k >>= 1
        return R

    # A point is kept as its affine coordinates, padded to cw words each,
    # in three (Weierstrass) or four (Edwards) slots so the copies the
    # driver makes have the same sizes as on hardware.
    def _w_read(self, off):
        cw = self.cw
        return (self.pib(off, cw), self.pib(off + cw, cw))

    def _w_write(self, P):
        cw = self.cw
        self.put_pob(0, P[0], cw); self.put_pob(cw, P[1], cw); self.put_pob(2 * cw, 1, cw)

    def fn_04(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # ECI2MD / EDI2MD
        if self.curve[0] == 'w':
            self._w_write(self._w_read(pib0))
        elif self.curve[0] == 'ed':
            x, y = self.pib(pib0, 8), self.pib(pib0 + 8, 8)
            for i, v in enumerate((x, y, 1, x * y % P25519)):
                self.put_pob(8 * i, v, 8)
        else:
            self.put_pob(0, self.pib(pib0, 8), 8)
            for i in range(1, 4):
                self.put_pob(8 * i, 0, 8)
    fn_24 = fn_04

    def fn_06(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # ECPD
        P = self._w_read(pib0)
        self._w_write(self._w_add(P, P))

    def fn_05(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # ECPA
        self._w_write(self._w_add(self._w_read(pib0), self._w_read(pib1)))

    def fn_0b(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # ECPM
        k = words_to_int(self.rd(PKB + 4 * pkb, (ew + 31) // 32)) & ((1 << ew) - 1)
        if k.bit_length() != ew:
            self.violations.append("ECPM OPTEW %d does not match k" % ew)
        P = self._w_read(pib0)
        twoP = self._w_read(pib1)
        if twoP != self._w_add(P, P):
            self.violations.append("ECPM PIB1 does not hold 2P")
        self._w_write(self._mul(self._w_add, k, P, None))

    def fn_07(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # ECM2I
        P = self._w_read(pib0)
        cw = self.cw
        self.put_pob(0, P[0], cw); self.put_pob(cw, P[1], cw)

    def _ed_read(self, off):
        return (self.pib(off, 8), self.pib(off + 8, 8))

    def _ed_write(self, P):
        for i, v in enumerate((P[0], P[1], 1, P[0] * P[1] % P25519)):
            self.put_pob(8 * i, v, 8)

    def fn_26(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # EDPD
        P = self._ed_read(pib0)
        self._ed_write(self._ed_add(P, P))

    def fn_25(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # EDPA
        self._ed_write(self._ed_add(self._ed_read(pib0), self._ed_read(pib1)))

    def fn_2b(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # EDPM
        k = words_to_int(self.rd(PKB + 4 * pkb, 8)) & ((1 << ew) - 1)
        P = self._ed_read(pib0)
        if self._ed_read(pib1) != self._ed_add(P, P):
            self.violations.append("EDPM PIB1 does not hold 2P")
        self._ed_write(self._mul(self._ed_add, k, P, (0, 1)))

    def fn_27(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # EDM2I
        x, y = self.pib(pib0, 8), self.pib(pib0 + 8, 8)
        self.put_pob(0, x, 8)
        if self.curve[0] == 'ed':
            self.put_pob(8, y, 8)

    def fn_3b(self, nw, ew, pcon, pib0, pib1, pkb, pob):     # X25519
        k = words_to_int(self.rd(PKB + 4 * pkb, 8)) & ((1 << 255) - 1)
        u = self.pib(pib0, 8)
        p = P25519
        x1, x2, z2, x3, z3, sw = u, 1, 0, u, 1, 0
        for t in reversed(range(255)):
            kt = (k >> t) & 1; sw ^= kt
            if sw: x2, x3, z2, z3 = x3, x2, z3, z2
            sw = kt
            A = x2 + z2; AA = A * A; B = x2 - z2; BB = B * B; E = AA - BB
            C = x3 + z3; D = x3 - z3; DA = D * A; CB = C * B
            x3 = (DA + CB) ** 2 % p; z3 = x1 * (DA - CB) ** 2 % p
            x2 = AA * BB % p; z2 = E * (AA + 121665 * E) % p
        if sw: x2, z2 = x3, z3
        self.put_pob(0, x2 * pow(z2, p - 2, p) % p if z2 else 0, 8)
        for i in range(1, 4):
            self.put_pob(8 * i, 0, 8)

    # ALU
    def alu_op(self):
        if self.dead:
            return
        if not (self.reg(CTRL + 0x04) & CLK_ALU):
            self.violations.append("ALU started with its clock off")
            return
        fn = self.reg(ALU + 0x00) & 0xFF
        self.ops.append(0x100 | fn)
        lens = self.reg(ALU + 0x10)
        de_w, ds_w = (lens & 0xFF) * 2, ((lens >> 8) & 0xFF) * 2

        def seg(reg):
            v = self.reg(ALU + reg)
            sid, off = (v >> 12) & 0xFF, v & 0xFFF
            if sid not in SEG_BASE:
                self.violations.append("ALU pointer to unknown segment %d" % sid)
                return SEG_BASE[9]
            return SEG_BASE[sid] + 4 * off

        de, ds, qt, rm = seg(0x30), seg(0x34), seg(0x38), seg(0x3C)

        def unswap(ws):
            out = list(ws)
            for i in range(0, len(out) - 1, 2):
                out[i], out[i + 1] = out[i + 1], out[i]
            return out

        if fn in (0x01, 0x02, 0x03):
            x = words_to_int(unswap(self.rd(de, de_w)))
            y = words_to_int(unswap(self.rd(ds, ds_w)))
            if fn == 0x01:
                q, r = divmod(x, y)
                qw = max(2, ((q.bit_length() + 63) // 64) * 2)
                rw = max(2, ((r.bit_length() + 63) // 64) * 2)
                self.wr(qt, int_to_words(q, qw)); self.wr(rm, int_to_words(r, rw))
                self.setreg(ALU + 0x14, (qw // 2) | ((rw // 2) << 8))
            else:
                n = max(de_w, ds_w)
                v = x + y if fn == 0x02 else x - y
                flag = (v >> (32 * n)) & 1 if fn == 0x02 else (1 if v < 0 else 0)
                self.wr(qt, int_to_words(v % (1 << (32 * n)), n))
                st = self.reg(ALU + 0x08) & ~0x100
                self.setreg(ALU + 0x08, st | (0x100 if flag else 0))
        elif fn in (0x10, 0x11, 0x20):
            def be(ws):
                return words_to_int(list(reversed(ws)))

            def wbe(addr, v, n):
                self.wr(addr, list(reversed(int_to_words(v, n))))
            opt = self.reg(ALU + 0x18)
            x = be(self.rd(de, de_w))
            if fn == 0x10:
                wbe(qt, x >> ((opt >> 8) & 0xFF), de_w)
            elif fn == 0x11:
                wbe(qt, x >> (32 * ((opt >> 8) & 0xFF)), de_w)
            else:
                y = be(self.rd(ds, ds_w))
                n = max(de_w, ds_w)
                out = 0
                for b in range(32 * n):
                    xb, yb = (x >> b) & 1, (y >> b) & 1
                    bit = {(1, 1): 7, (0, 1): 6, (1, 0): 5, (0, 0): 4}[(xb, yb)]
                    if opt & (1 << bit): out |= 1 << b
                wbe(qt, out, n)
        self.setreg(ALU + 0x0C, self.reg(ALU + 0x0C) | 1)
