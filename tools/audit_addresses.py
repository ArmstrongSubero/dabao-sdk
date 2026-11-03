#!/usr/bin/env python3
"""
audit_addresses.py: refuse source code that names a board killing address

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
RRC_BASE, CGU_*, PMU_*, AON_*, RTC_*, WDT_*, AT0_*, AT1_*, ATIMER_BASE,
ATIMER_AO_BASE), the same list Rovari Studio checks. Comments are ignored.
The driver level API (wdt_start, rtc_init) is not flagged; what is flagged
is an application reaching the registers itself.

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
NAME_PREFIXES = ("CGU_", "PMU_", "AON_", "RTC_", "WDT_", "AT0_", "AT1_")

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
