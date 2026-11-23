#!/usr/bin/env python3
"""
dabao_sdk_tools.audit_addresses: refuse source code that names a board killing address

Copyright (c) 2026 Armstrong Subero
SPDX-License-Identifier: Apache-2.0

Usage:
  python3 audit_addresses.py <file or folder> [...]

Exit status 0 when nothing is found, 1 when something is.

A read only probe of the RRAM controller at 0x40000000 destroyed a board.
bao_forbidden.h catches a constant passed to BAO_READ32 or BAO_WRITE32, but
nothing in a header can catch a raw pointer written by hand from a datasheet
or an RTL listing. This sweeps the source text for hex literals inside these
ranges, however they are used:

  0x40000000 to 0x4000FFFF   RRC, the RRAM controller
  0x40040000 to 0x4007FFFF   CGU, sysctrl, EVC, WDT, ATIMER, AON, RTC
  0x60000000 to 0x6005FFFF   boot0 and boot1
  0x603DA000 to 0x603FFFFF   one way counters, access control, key slots

It also refuses the SDK register macro names for those blocks (RRC_CR,
RRC_BASE, CGU_*, PMU_*, AON_*, RTC_*, WDT_*, ATIMER_BASE, ATIMER_AO_BASE),
the same list Rovari Studio checks. Comments are ignored.

One exception is made, for the ATimer. Its two instances are the PULP
apb_timer_unit (sysctrl/rtl/apbsys_intf.sv and aoperi.sv), nothing in the
boot chain or Xous uses them, and they have been run on the board. Allowed are the ten register addresses of each instance,
0x40043000 to 0x40043024 and 0x40063000 to 0x40063024 in steps of 4, and
the fixed register macros AT0_*, AT1_*, AT0_AO_* and AT1_AO_* from
hardware/regs/timer.h. ATIMER_BASE and ATIMER_AO_BASE stay refused, so an
application can only name a register, never build an offset from the base.
Any other address in those 4 KB pages, the watchdogs at 0x40041000 and
0x40062000 and the EVC at 0x40044000 included, is still refused.

A second exception sits inside the last range. The first two data slots,
64 bytes at 0x603E0000 to 0x603E003F, hold the chip's public SERIAL_NUMBER
and UUID. bunnie's bao1x-api declares both open to every boot stage, and
reading them is how a program tells one board from another. A plain pointer
cannot write RRAM, and rram_write_page refuses this region, so naming these
two slots is allowed. Every other slot, the access control table and the
counters stay refused.

The watchdog and RTC drivers are approved to reach their blocks, and only
through the driver API (wdt_start, wdt_feed, wdt_get_count, rtc_init,
rtc_start, rtc_stop, rtc_set_time, rtc_get_time, rtc_set_divider,
rtc_set_alarm, rtc_alarm_fired, rtc_clear_alarm, rtc_alarm_wake). The
watchdog is ARM's CMSDK APB watchdog (sysctrl/rtl/apbsys_intf.sv, clocked
from pclk in soc_top.sv); the driver uses 0x40041000 to 0x40041014 and the
lock register at 0x40041C00, and its reset output only drives the reset
generator. The RTC driver uses 0x40061000 to 0x4006101C and, through
rtc_set_divider, the 14 bit 1 Hz divider CLK1HZ_FD at 0x40060004
(ao/rtl/ao_sysctrl.sv). None of these can change boot0, boot1 or the RRAM.
The power driver is approved the same way, through power_down only. It
writes the PMU wake mask (0x40060008), the power down regulator settings
PMUCRPD (0x40060018) and the power down key PDAR (0x40060044) in
ao/rtl/ao_sysctrl.sv, and through rtc_alarm_wake the RTC interrupt mask
(0x40061010). Power down holds the SoC in reset until an unmasked wake
source rises or the RESET pin is pressed, then the chip boots again; it
never writes the RRAM, and the 32 kHz clock select and the reset mask
are left alone.
An application calling those functions is not flagged; an application
naming the registers or the WDT_*, RTC_* or AON_* macros itself still is.

Run it on application code, not on the SDK. The SDK drivers reach some of
these blocks on purpose and are reviewed separately.
"""

import os
import re
import sys

REGIONS = (
    (0x40000000, 0x40010000, "the RRAM controller (RRC)"),
    (0x40040000, 0x40080000, "the boot chain register region (CGU, WDT, ATIMER, AON, RTC)"),
    (0x60000000, 0x60060000, "boot0 and boot1"),
    (0x603DA000, 0x60400000, "the one way counters and key slots"),
)

NAMES = ("RRC_BASE", "RRC_CR", "ATIMER_BASE", "ATIMER_AO_BASE")
NAME_PREFIXES = ("CGU_", "PMU_", "AON_", "RTC_", "WDT_")

ATIMER_REGS = frozenset(base + off
                        for base in (0x40043000, 0x40063000)
                        for off in range(0x00, 0x28, 4))

PUBLIC_SLOTS = (0x603E0000, 0x603E0040)

EXTENSIONS = (".c", ".h", ".cpp", ".hpp", ".cc", ".rova", ".S", ".s", ".inc")

_HEX = re.compile(r'\b0[xX]([0-9a-fA-F]{1,8})[uUlL]*\b')
_NAME = re.compile(r'\b(' + '|'.join(NAMES) + r'|(?:' + '|'.join(NAME_PREFIXES) + r')[A-Z0-9_]+)\b')


def _strip_comments(text):
    """Blank out comments and string contents, keeping line numbers."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join('\n' if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(c + ' ' * (j - i - 1) + (text[j] if j < n else ''))
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def _region(value):
    if value in ATIMER_REGS:
        return None
    if PUBLIC_SLOTS[0] <= value < PUBLIC_SLOTS[1]:
        return None
    for lo, hi, name in REGIONS:
        if lo <= value < hi:
            return name
    return None


def scan_file(path):
    """Return a list of (path, line, text, reason) for one file."""
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            raw = f.read()
    except OSError:
        return []
    code = _strip_comments(raw)
    raw_lines = raw.splitlines()
    hits = []
    for lineno, line in enumerate(code.splitlines(), 1):
        shown = raw_lines[lineno - 1].strip() if lineno <= len(raw_lines) else line.strip()
        for m in _HEX.finditer(line):
            name = _region(int(m.group(1), 16))
            if name:
                hits.append((path, lineno, shown,
                             "0x%s is in %s" % (m.group(1).upper(), name)))
        for m in _NAME.finditer(line):
            hits.append((path, lineno, shown, "%s names a boot chain block" % m.group(1)))
    return hits


def scan_paths(paths, skip_dirs=()):
    """Scan files and folders. skip_dirs are folder names left out."""
    hits = []
    for p in paths:
        if os.path.isfile(p):
            if p.endswith(EXTENSIONS):
                hits.extend(scan_file(p))
            continue
        for root, dirs, files in os.walk(p):
            dirs[:] = [d for d in dirs if d not in skip_dirs and not d.startswith('.')]
            for fn in sorted(files):
                if fn.endswith(EXTENSIONS):
                    hits.extend(scan_file(os.path.join(root, fn)))
    return hits


def main():
    if len(sys.argv) < 2:
        print("Usage: %s <file or folder> [...]" % sys.argv[0])
        sys.exit(2)
    hits = scan_paths(sys.argv[1:])
    for path, line, text, reason in hits:
        print("%s:%d: %s" % (path, line, reason))
        print("    %s" % text)
    if hits:
        print("%d forbidden address reference(s). Nothing should be built from this." % len(hits))
        sys.exit(1)
    print("No forbidden addresses found.")


if __name__ == '__main__':
    main()
