#!/usr/bin/env python3
"""
board_vs_emu.py: run examples on the Dabao and on baoemu and compare the console output

Copyright (c) 2026 Armstrong Subero
SPDX-License-Identifier: Apache-2.0

Usage:
  python tools/board_vs_emu.py --port COM27 --console COM5 bio_isa bio_div aes_modes
  python tools/board_vs_emu.py --port COM27 --console COM5 --mmu mmu_stack mmu_gap
  python tools/board_vs_emu.py --emu-only hello_uart blink:3000

  --port      boot1 USB port used for flashing
  --console   USB serial adapter on PB14 (TX) and PB13 (RX)
  --ms        how long to run each example, default 2000 (name:ms overrides it)
  --mmu       build with the MMU sandbox
  --emu       path to baoemu.exe or baoemu
  --emu-only  skip the board, emulator output only
  --no-build  use what is already in build/

Writes a PDF report and the raw logs to compare_<date>_<time>/.
Needs: pip install pyserial reportlab
"""

import argparse
import datetime
import difflib
import os
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIN = os.name == "nt"


def find_emu(given):
    if given:
        return given
    exe = "baoemu.exe" if WIN else "baoemu"
    for p in (os.path.join(ROOT, "baoemu", "build", exe),
              os.path.join(ROOT, "baoemu", "bin", "windows" if WIN else "linux", exe)):
        if os.path.exists(p):
            return p
    sys.exit("baoemu not found, build it in baoemu/ or pass --emu")


def build(name, mmu):
    env = dict(os.environ)
    if mmu:
        env["BAO_MMU"] = "1"
    else:
        env.pop("BAO_MMU", None)
    cmd = ["cmd", "/c", "bao_flash.bat", "build", name] if WIN else ["./bao_flash.sh", "build", name]
    r = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)
    ok = os.path.exists(os.path.join(ROOT, "build", name + ".elf")) and r.returncode == 0 \
        and "FAILED" not in r.stdout
    return ok, r.stdout + r.stderr


def run_emu(emu, name, ms):
    elf = os.path.join(ROOT, "build", name + ".elf")
    r = subprocess.run([emu, "--ms", str(ms), "--keep-going", elf],
                       capture_output=True, text=True, errors="replace", timeout=120)
    return r.stdout, r.stderr


class Console:
    def __init__(self, port):
        import serial
        self.ser = serial.Serial(port, 115200, timeout=0.05)
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.run = True
        self.t = threading.Thread(target=self._read, daemon=True)
        self.t.start()

    def _read(self):
        while self.run:
            d = self.ser.read(256)
            if d:
                with self.lock:
                    self.buf += d

    def clear(self):
        self.ser.reset_input_buffer()
        with self.lock:
            self.buf = bytearray()

    def text(self):
        with self.lock:
            return bytes(self.buf).decode("latin-1")

    def close(self):
        self.run = False
        self.t.join()
        self.ser.close()


def flash(port, name):
    uf2 = os.path.join(ROOT, "build", name + ".uf2")
    if not os.path.exists(uf2):
        return False, "no " + uf2
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "serial_flash.py"), port, uf2],
                       cwd=ROOT, capture_output=True, text=True)
    return r.returncode == 0, r.stdout + r.stderr


def clean(text):
    lines = []
    for raw in text.replace("\r", "\n").split("\n"):
        s = "".join(c for c in raw if 32 <= ord(c) < 127).rstrip()
        if s:
            lines.append(s)
    return lines


def trim_noise(board, emu):
    """The first board line often carries reset noise in front of real text."""
    out = list(board)
    for i, b in enumerate(out[:3]):
        for e in emu:
            if len(e) >= 6 and b != e and b.endswith(e):
                out[i] = e
                break
    while out and not any(out[0] == e for e in emu) and len(out) > 1 and \
            sum(c.isalnum() or c == " " for c in out[0]) < len(out[0]) * 0.8:
        out.pop(0)
    return out


def align(board, emu):
    rows = []
    sm = difflib.SequenceMatcher(a=board, b=emu, autojunk=False)
    for op, a0, a1, b0, b1 in sm.get_opcodes():
        if op == "equal":
            rows += [(board[a0 + k], emu[b0 + k], False) for k in range(a1 - a0)]
        else:
            n = max(a1 - a0, b1 - b0)
            for k in range(n):
                bl = board[a0 + k] if a0 + k < a1 else ""
                el = emu[b0 + k] if b0 + k < b1 else ""
                rows.append((bl, el, True))
    return rows


def report(path, results, args):
    from reportlab.lib import colors
    from reportlab.lib.pagesizes import letter, landscape
    from reportlab.lib.styles import ParagraphStyle
    from reportlab.lib.units import inch
    from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Table,
                                    TableStyle, PageBreak, KeepTogether)
    from xml.sax.saxutils import escape

    title = ParagraphStyle("t", fontName="Helvetica-Bold", fontSize=20, leading=26)
    h = ParagraphStyle("h", fontName="Helvetica-Bold", fontSize=13, leading=18, spaceBefore=6)
    body = ParagraphStyle("b", fontName="Helvetica", fontSize=10, leading=14)
    mono = ParagraphStyle("m", fontName="Courier", fontSize=7.5, leading=9.5)
    monob = ParagraphStyle("mb", parent=mono, fontName="Courier-Bold")
    cellb = ParagraphStyle("cb", fontName="Helvetica-Bold", fontSize=9, leading=12)
    cell = ParagraphStyle("c", fontName="Helvetica", fontSize=9, leading=12)

    def foot(c, d):
        c.saveState()
        c.setFont("Helvetica", 8)
        c.drawString(0.6 * inch, 0.4 * inch, "Armstrong Subero, RV Embedded")
        c.drawRightString(landscape(letter)[0] - 0.6 * inch, 0.4 * inch, "Page %d" % d.page)
        c.restoreState()

    doc = SimpleDocTemplate(path, pagesize=landscape(letter), leftMargin=0.6 * inch,
                            rightMargin=0.6 * inch, topMargin=0.6 * inch, bottomMargin=0.7 * inch)
    w = landscape(letter)[0] - 1.2 * inch
    rule = [("BOX", (0, 0), (-1, -1), 1.2, colors.black),
            ("LINEBELOW", (0, 0), (-1, 0), 1.2, colors.black),
            ("INNERGRID", (0, 0), (-1, -1), 0.4, colors.black),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]

    story = [Paragraph("Dabao Board and baoemu Console Comparison", title), Spacer(1, 6),
             Paragraph("Armstrong Subero, RV Embedded", body),
             Paragraph(datetime.datetime.now().strftime("%d %B %Y, %H:%M"), body),
             Paragraph("Flash port %s, console %s, %s build, %d ms per example"
                       % (args.port or "none", args.console or "none",
                          "MMU sandbox" if args.mmu else "normal", args.ms), body),
             Spacer(1, 14)]

    data = [[Paragraph("Example", cellb), Paragraph("Result", cellb),
             Paragraph("Board lines", cellb), Paragraph("Emulator lines", cellb),
             Paragraph("Lines that differ", cellb)]]
    for r in results:
        data.append([Paragraph(r["name"], cell), Paragraph(r["result"], cell),
                     Paragraph(str(len(r["board"])), cell), Paragraph(str(len(r["emu"])), cell),
                     Paragraph(str(sum(1 for x in r["rows"] if x[2])), cell)])
    t = Table(data, colWidths=[w * 0.3, w * 0.25, w * 0.15, w * 0.15, w * 0.15], repeatRows=1)
    t.setStyle(TableStyle(rule))
    story += [t, Spacer(1, 10),
              Paragraph("Rows that differ are set in bold and marked with an asterisk.", body)]

    for r in results:
        story.append(PageBreak())
        story.append(Paragraph(r["name"], h))
        story.append(Paragraph(r["result"], body))
        story.append(Spacer(1, 8))
        rows = [[Paragraph("", cellb), Paragraph("Board", cellb), Paragraph("Emulator", cellb)]]
        for b, e, diff in r["rows"] or [("", "", False)]:
            st = monob if diff else mono
            rows.append([Paragraph("*" if diff else "", st),
                         Paragraph(escape(b) or "&nbsp;", st), Paragraph(escape(e) or "&nbsp;", st)])
        t = Table(rows, colWidths=[0.2 * inch, (w - 0.2 * inch) / 2, (w - 0.2 * inch) / 2],
                  repeatRows=1)
        t.setStyle(TableStyle(rule))
        story.append(t)
        if r["note"]:
            story.append(Spacer(1, 10))
            story.append(KeepTogether([Paragraph("Notes", cellb)] +
                                      [Paragraph(escape(n), mono) for n in r["note"]]))
    doc.build(story, onFirstPage=foot, onLaterPages=foot)


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("examples", nargs="+")
    ap.add_argument("--port")
    ap.add_argument("--console")
    ap.add_argument("--ms", type=int, default=2000)
    ap.add_argument("--mmu", action="store_true")
    ap.add_argument("--emu")
    ap.add_argument("--emu-only", action="store_true")
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()

    if not args.emu_only and not (args.port and args.console):
        sys.exit("give --port and --console, or use --emu-only")
    emu = find_emu(args.emu)
    out = os.path.join(ROOT, "compare_" + datetime.datetime.now().strftime("%Y%m%d_%H%M"))
    os.makedirs(out, exist_ok=True)

    con = None if args.emu_only else Console(args.console)
    results = []
    try:
        for item in args.examples:
            name, _, ms = item.partition(":")
            ms = int(ms) if ms else args.ms
            print("\n== %s" % name)
            note = []
            if not args.no_build:
                ok, log = build(name, args.mmu)
                if not ok:
                    print("   build failed")
                    results.append(dict(name=name, result="Build failed", board=[], emu=[],
                                        rows=[], note=clean(log)[-12:]))
                    continue
            emu_out, emu_err = run_emu(emu, name, ms)
            emu_lines = clean(emu_out)
            note += [l for l in clean(emu_err) if "stop:" in l]
            board_lines = []
            if con:
                input("   Press RESET on the board, then Enter to flash ")
                con.clear()
                ok, log = flash(args.port, name)
                if not ok:
                    print("   flash failed")
                    note += clean(log)[-8:]
                else:
                    end = time.time() + ms / 1000.0 + 1.0
                    while time.time() < end and "[Done]" not in con.text():
                        time.sleep(0.05)
                    time.sleep(0.2)
                    board_lines = trim_noise(clean(con.text()), emu_lines)
            rows = align(board_lines, emu_lines) if con else [("", e, False) for e in emu_lines]
            diffs = sum(1 for x in rows if x[2])
            if not con:
                result = "Emulator only"
            elif not board_lines:
                result = "No board output"
            elif diffs == 0:
                result = "Match"
            else:
                result = "Differs (%d line%s)" % (diffs, "" if diffs == 1 else "s")
            print("   " + result)
            with open(os.path.join(out, name + "_emu.txt"), "w") as f:
                f.write(emu_out + "\n" + emu_err)
            if con:
                with open(os.path.join(out, name + "_board.txt"), "w") as f:
                    f.write("\n".join(board_lines) + "\n")
            results.append(dict(name=name, result=result, board=board_lines, emu=emu_lines,
                                rows=rows, note=note))
    finally:
        if con:
            con.close()

    pdf = os.path.join(out, "comparison.pdf")
    report(pdf, results, args)
    print("\nReport: " + pdf)


if __name__ == "__main__":
    main()
