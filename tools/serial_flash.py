#!/usr/bin/env python3
"""
serial_flash.py: flash a signed UF2 to the Dabao through the boot1 serial REPL

Copyright (c) 2026 Armstrong Subero
SPDX-License-Identifier: Apache-2.0

Modes:
  (default)        Press RESET first. bootwait left enabled after the flash
  --persistent     Press RESET first. bootwait disabled after the flash
  --icsp           DTR reset via serial adapter, flash via USB CDC (two ports)
  --icsp-single    DTR reset and flash over the same adapter (one port, no USB)

This follows the Rovari Studio flash path (flash_mixin.py) exactly.

Why it is written this way

  1. The RRAM controller stages a write and commits it to the last address
     touched. boot1 programs RRAM with its USB and UART interrupts live, and
     those handlers execute from RRAM, so traffic arriving during a commit is
     the hazard. Each block is therefore sent in one write and acknowledged
     ("Wrote 256 to 0x60060000") before the next is sent. The wire is idle
     while boot1 programs the page, and a block that is not acknowledged, or
     is acknowledged for the wrong address or length, stops the flash.

  2. bootwait is not a flag. In the boot chain it is a ONE WAY COUNTER and
     every change spends an increment that can never be undone. It is read
     with 'bootwait check' first and only changed when it differs from the
     wanted state. An unreadable state is left alone.

  3. Every UF2 record is checked before a byte is sent. Anything below
     0x60060000 (boot0, boot1) or past 0x60160000 (runtime storage) refuses
     the whole image.

  4. Before the first block, boot1's 'audit' is run to read SHA-512 hashes of
     all of boot0 and boot1. After the last block it is run again. If either
     hash changed, the flash stops with the board still in the REPL: no
     reset, no bootwait change, no boot. audit only reads.

  5. When bootwait is on and the board is on USB CDC, the board is reset and
     the script waits for boot1 to enumerate and answer again. That proves
     boot0 and boot1 survived the flash while the REPL is still there to
     recover with. bootwait is only disabled after that check passes.

Every bootwait change and read is appended to ~/.rovari_flash.log, the same
journal Rovari Studio writes.

Source: bao1x-boot/boot1/src/repl.rs
"""

import os
import re
import sys
import time
import base64
import struct

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed")
    print("Install with: pip install pyserial")
    sys.exit(1)


UF2_BLOCK_SIZE = 512
UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FAMILY = 0xA7D76373

BOOT1_START = 0x60020000
PROGRAM_START = 0x60060000
PROGRAM_END = 0x60160000

BOOT1_UART_BAUD = 1000000
CDC_BAUD = 115200
DABAO_VID = 0x1D50
DABAO_PID = 0x6196

CHUNK_SIZE = 64
CHUNK_DELAY = 0.005
RESP_TIMEOUT = 3.0

ACK_COUNTED = re.compile(
    rb'\bWrote[ \t]+([0-9]+)[ \t]+to[ \t]+0x([0-9a-fA-F]{8})(?![0-9a-fA-F])',
    re.IGNORECASE)
ACK_ADDRESS = re.compile(
    rb'\bWrote\s+(?:0x)?([0-9a-fA-F]{8})(?![0-9a-fA-F])', re.IGNORECASE)
ACK_LEGACY = re.compile(rb'\bWrote[ \t]*\r?\n', re.IGNORECASE)

AUDIT_TIMEOUT = 20.0
AUDIT_QUIET = 0.4
AUDIT_BOOT0 = re.compile(rb'boot0 partition:\s*([0-9a-fA-F]{128})')
AUDIT_BOOT1 = re.compile(rb'boot1 partition:\s*([0-9a-fA-F]{128})')


class FlashError(Exception):
    pass


def flash_log(line):
    try:
        path = os.path.join(os.path.expanduser("~"), ".rovari_flash.log")
        with open(path, "a", encoding="utf-8") as f:
            f.write("%s  %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), line))
            f.flush()
    except Exception:
        pass


def send_throttled(ser, data):
    """Hardware UART path only. USB CDC takes a whole line at once."""
    encoded = data.encode('ascii') if isinstance(data, str) else data
    for i in range(0, len(encoded), CHUNK_SIZE):
        ser.write(encoded[i:i + CHUNK_SIZE])
        time.sleep(CHUNK_DELAY)


def send_line(ser, data, throttle):
    if throttle:
        send_throttled(ser, data)
    else:
        ser.write(data.encode('ascii') if isinstance(data, str) else data)
        ser.flush()


def is_boot1_active(ser):
    ser.reset_input_buffer()
    ser.write(b'\r\n')
    time.sleep(0.3)
    response = ser.read(ser.in_waiting or 1)
    text = response.decode('utf-8', errors='replace').strip()
    return ('Commands include' in text or 'uf2' in text
            or 'bootwait' in text), text


def try_open_port(port_name, baudrate, timeout=2):
    try:
        return serial.Serial(port_name, baudrate, timeout=timeout)
    except (serial.SerialException, OSError):
        return None


def check_uf2(uf2_data):
    """Return None if every record is safe to send, otherwise the reason."""
    if not uf2_data or len(uf2_data) % UF2_BLOCK_SIZE:
        return "UF2 is empty or not made of complete 512 byte blocks"
    n = len(uf2_data) // UF2_BLOCK_SIZE

    seen = set()
    ranges = []
    for i in range(n):
        b = uf2_data[i * UF2_BLOCK_SIZE:(i + 1) * UF2_BLOCK_SIZE]
        m0, m1, flags, addr, payload, bno, nb, family = \
            struct.unpack_from('<IIIIIIII', b, 0)
        mend = struct.unpack_from('<I', b, UF2_BLOCK_SIZE - 4)[0]

        if m0 != UF2_MAGIC0 or m1 != UF2_MAGIC1 or mend != UF2_MAGIC_END:
            return f"block {i} is not a UF2 block"
        if family != UF2_FAMILY:
            return f"block {i} is not for the Baochip-1x"
        if (payload == 0 or payload > 476 or (flags & 1)
                or nb != n or bno >= n or bno in seen):
            return f"block {i} is malformed"
        seen.add(bno)

        if addr < PROGRAM_START:
            region = 'boot0' if addr < BOOT1_START else 'boot1'
            return (f"block {i} targets 0x{addr:08X} in {region}. "
                    f"Nothing was sent")
        if addr + payload > PROGRAM_END:
            return (f"block {i} ends at 0x{addr + payload:08X}, past program "
                    f"memory at 0x{PROGRAM_END:08X}. Image too large")
        ranges.append((addr, addr + payload, i))

    ranges.sort()
    for (_s, end, i), (ns, _e, ni) in zip(ranges, ranges[1:]):
        if ns < end:
            return f"blocks {i} and {ni} overlap"
    return None


def boot_chain_hashes(ser, throttle=False):
    """Run boot1 'audit' and return (boot0, boot1) SHA-512 digests."""
    ser.reset_input_buffer()
    send_line(ser, b'audit\r\n', throttle)
    deadline = time.time() + AUDIT_TIMEOUT
    buf = b''
    b0 = b1 = None
    last_rx = time.time()
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
            last_rx = time.time()
            if b0 is None:
                m = AUDIT_BOOT0.search(buf)
                if m:
                    b0 = m.group(1).decode('ascii').lower()
            if b1 is None:
                m = AUDIT_BOOT1.search(buf)
                if m:
                    b1 = m.group(1).decode('ascii').lower()
        else:
            if b0 and b1 and time.time() - last_rx >= AUDIT_QUIET:
                break
            time.sleep(0.01)
    if not (b0 and b1):
        return None
    return b0, b1


def bootwait_state(ser, throttle=False):
    """Return 'enabled', 'disabled', or None if it cannot be read."""
    try:
        ser.reset_input_buffer()
        send_line(ser, b'bootwait check\r\n', throttle)
        deadline = time.time() + 2.0
        buf = b''
        while time.time() < deadline:
            chunk = ser.read(ser.in_waiting or 1)
            if chunk:
                buf += chunk
                text = buf.decode('utf-8', errors='replace').lower()
                if 'bootwait is disable' in text:
                    return 'disabled'
                if 'bootwait is enable' in text:
                    return 'enabled'
            else:
                time.sleep(0.02)
    except Exception:
        return None
    return None


def set_bootwait(ser, want, throttle=False):
    """Change bootwait only if it differs. Each change is permanent."""
    have = bootwait_state(ser, throttle)
    if have == want:
        print(f"  bootwait already {want}, counter untouched")
        return True
    if have is None:
        print("  bootwait unreadable, left unchanged")
        return False

    print(f"  bootwait {have} to {want}, one counter step used")
    flash_log(f"bootwait {have} -> {want}  ONE WAY INCREMENT")
    try:
        ser.reset_input_buffer()
        send_line(ser, f'bootwait {want[:-1]}\r\n', throttle)
        time.sleep(0.3)
        ser.read(ser.in_waiting or 1)
    except Exception:
        return False

    now = bootwait_state(ser, throttle)
    if now != want:
        print(f"  bootwait reads {now} after the change, expected {want}")
        flash_log(f"bootwait readback {now}, expected {want}")
        return False
    return True


def confirm_boot_chain(timeout=20.0):
    """Wait for boot1 to enumerate on USB and answer. The caller resets."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        for p in serial.tools.list_ports.comports():
            if p.vid != DABAO_VID or p.pid != DABAO_PID:
                continue
            cand = try_open_port(p.device, CDC_BAUD, timeout=1)
            if cand is None:
                continue
            try:
                time.sleep(0.2)
                active, _text = is_boot1_active(cand)
                if active:
                    return cand
                cand.close()
            except (serial.SerialException, OSError):
                try:
                    cand.close()
                except Exception:
                    pass
        time.sleep(0.5)
    return None


def write_blocks(ser, uf2_data, throttle):
    num_blocks = len(uf2_data) // UF2_BLOCK_SIZE
    commands = []
    for i in range(num_blocks):
        block = uf2_data[i * UF2_BLOCK_SIZE:(i + 1) * UF2_BLOCK_SIZE]
        b64 = base64.b64encode(block).decode('ascii')
        addr, length = struct.unpack_from('<II', block, 12)
        commands.append((f"uf2 {b64}\r\n".encode('ascii'), addr, length))

    ser.reset_input_buffer()
    prev_timeout = ser.timeout
    ser.timeout = 0.05
    t0 = time.time()
    try:
        for i, (cmd, addr, length) in enumerate(commands):
            send_line(ser, cmd, throttle)

            resp = b''
            counted = ack = None
            legacy = False
            deadline = time.time() + RESP_TIMEOUT
            while time.time() < deadline:
                chunk = ser.read(ser.in_waiting or 1)
                if not chunk:
                    continue
                resp += chunk
                if b'invalid' in resp.lower():
                    break
                counted = ACK_COUNTED.search(resp)
                ack = ACK_ADDRESS.search(resp)
                legacy = bool(ACK_LEGACY.search(resp))
                if counted or ack or legacy:
                    break

            text = resp.decode('utf-8', errors='replace').strip()
            if 'invalid' in text.lower():
                raise FlashError(f"boot1 rejected block {i + 1}: {text}")
            if not counted and not ack and not legacy and resp.strip().lower() == b'wrote':
                legacy = True
            if not counted and not ack and not legacy:
                raise FlashError(
                    f"block {i + 1} at 0x{addr:08X} not acknowledged within "
                    f"{RESP_TIMEOUT:.0f} s ({resp[-160:]!r})")
            if counted:
                got_len = int(counted.group(1))
                got_addr = int(counted.group(2), 16)
                if got_addr != addr or got_len != length:
                    raise FlashError(
                        f"block {i + 1} sent for 0x{addr:08X}, {length} bytes, "
                        f"acknowledged for 0x{got_addr:08X}, {got_len} bytes")
            if ack and int(ack.group(1), 16) != addr:
                raise FlashError(
                    f"block {i + 1} sent for 0x{addr:08X}, acknowledged for "
                    f"0x{int(ack.group(1), 16):08X}")

            if i % 50 == 0 or i == num_blocks - 1:
                print(f"  [{i + 1}/{num_blocks}] 0x{addr:08X}")
    finally:
        ser.timeout = prev_timeout

    elapsed = time.time() - t0
    rate = (num_blocks * UF2_BLOCK_SIZE / 1024.0 / elapsed) if elapsed > 0 else 0.0
    print(f"{num_blocks} blocks written in {elapsed:.1f} s ({rate:.0f} KB/s)")


def do_flash(ser, uf2_data, persistent=False, throttle=False, can_verify=True):
    """Write, settle bootwait with the fewest counter steps, check, boot."""
    print("  reading boot chain hashes")
    before = boot_chain_hashes(ser, throttle)
    if before is None:
        ser.close()
        raise FlashError("boot1 audit gave no partition hashes, nothing was written")
    flash_log(f"boot chain before flash boot0:{before[0]} boot1:{before[1]}")

    write_blocks(ser, uf2_data, throttle)

    after = boot_chain_hashes(ser, throttle)
    if after != before:
        changed = [n for n, a, b in zip(('boot0', 'boot1'), before, after or (None, None))
                   if a != b] if after else ['boot chain']
        flash_log(f"BOOT CHAIN CHANGED during flash: {' and '.join(changed)}")
        ser.close()
        raise FlashError(
            f"{' and '.join(changed)} changed during the flash. Do not reset, "
            f"power cycle or flash this board again. It is still in boot1 now "
            f"and may not be after a reset")
    print("  boot chain unchanged")

    ser.reset_input_buffer()
    state = bootwait_state(ser, throttle)
    print(f"  bootwait is {state if state else 'unreadable'}")
    flash_log(f"bootwait read {state}")

    verify = False
    disable_after = False
    if state is None:
        print("  bootwait unreadable, left unchanged and reset check skipped")
    elif persistent:
        if state == 'enabled':
            verify = True
            disable_after = True
        else:
            print("  bootwait already off, reset check skipped to save two counter steps")
    else:
        if state == 'disabled':
            if set_bootwait(ser, 'enabled', throttle):
                verify = True
            else:
                print("  bootwait could not be turned on, reset check skipped")
        else:
            print("  bootwait already on, counter untouched")
            verify = True

    if verify and not can_verify:
        print("  reset check needs USB CDC, skipped on a hardware UART flash")
        verify = False

    if verify:
        print("  resetting to check the boot chain")
        try:
            send_line(ser, b'reset\r\n', throttle)
            time.sleep(0.3)
        except (serial.SerialException, OSError):
            pass
        try:
            ser.close()
        except (serial.SerialException, OSError):
            pass
        ser = confirm_boot_chain()
        if ser is None:
            raise FlashError(
                "boot1 did not come back after reset, though bootwait is on. "
                "Do not power cycle repeatedly or flash another board yet. "
                "Check UART2 at 1 Mbaud: a healthy board prints the boot0 "
                "banner twice, then boot1")
        print("  board reset, boot1 back up")
        ser.reset_input_buffer()

    permanent_ok = False
    if disable_after:
        permanent_ok = set_bootwait(ser, 'disabled', throttle)
        if not permanent_ok:
            print("  bootwait could not be turned off, the board will still wait in boot1")

    print("  sending boot")
    time.sleep(0.3)
    try:
        send_line(ser, b'boot\r\n', throttle)
        time.sleep(0.5)
        ser.read(ser.in_waiting or 1)
    except (serial.SerialException, OSError):
        pass
    try:
        ser.close()
    except (serial.SerialException, OSError):
        pass

    if persistent and not (state == 'disabled' or permanent_ok):
        raise FlashError("firmware written, but permanent boot mode could not "
                         "be confirmed. The board may still wait in boot1")
    if persistent:
        print("Boots straight into the program from now on. "
              "To flash again, hold PROG through reset")


def dtr_reset(dtr_port, cdc_port, baudrate=CDC_BAUD):
    """Two port ICSP: DTR reset on adapter, flash on USB CDC."""
    ser = try_open_port(dtr_port, baudrate)
    if ser is None:
        raise FlashError(f"could not open {dtr_port}")

    print(f"Resetting board via DTR on {dtr_port}...")
    ser.dtr = True
    time.sleep(0.1)
    ser.dtr = False
    ser.close()

    print(f"Waiting for boot1 on {cdc_port}...")
    time.sleep(3.0)

    for attempt in range(10):
        cdc = try_open_port(cdc_port, baudrate)
        if cdc is not None:
            time.sleep(0.5)
            active, _text = is_boot1_active(cdc)
            if active:
                print("Boot1 REPL detected.")
                return cdc
            cdc.close()
        if attempt < 9:
            print(f"  Waiting... ({attempt + 1}/10)")
            time.sleep(1.5)

    raise FlashError(f"could not reach boot1 REPL on {cdc_port}")


def dtr_reset_single(port_name):
    """Single port ICSP: DTR reset, flash over hardware UART at 1 Mbaud."""
    ser = try_open_port(port_name, BOOT1_UART_BAUD)
    if ser is None:
        raise FlashError(f"could not open {port_name}")

    print(f"Resetting board via DTR on {port_name}...")
    ser.dtr = True
    time.sleep(0.1)
    ser.dtr = False

    print(f"Waiting for boot1 on hardware UART ({BOOT1_UART_BAUD} baud)...")
    time.sleep(3.0)

    for attempt in range(10):
        active, _text = is_boot1_active(ser)
        if active:
            print("Boot1 REPL detected.")
            ser.reset_input_buffer()
            send_throttled(ser, 'localecho off\r\n')
            time.sleep(0.3)
            ser.reset_input_buffer()
            return ser
        if attempt < 9:
            print(f"  Waiting... ({attempt + 1}/10)")
            time.sleep(1.5)

    ser.close()
    raise FlashError(f"could not reach boot1 REPL on {port_name}")


def flash_uf2(port_name, uf2_file, persistent=False, icsp=False,
              icsp_single=False, icsp_dtr=None):
    with open(uf2_file, 'rb') as f:
        uf2_data = f.read()

    problem = check_uf2(uf2_data)
    if problem:
        raise FlashError(f"flash refused: {problem}")

    num_blocks = len(uf2_data) // UF2_BLOCK_SIZE
    mode = "permanent" if persistent else "development"
    print(f"Flashing {uf2_file}: {num_blocks} blocks, {mode} mode")

    if icsp_single:
        ser = dtr_reset_single(port_name)
        do_flash(ser, uf2_data, persistent, throttle=True, can_verify=False)
    elif icsp:
        ser = dtr_reset(icsp_dtr, port_name)
        do_flash(ser, uf2_data, persistent)
    else:
        ser = try_open_port(port_name, CDC_BAUD)
        if ser is None:
            raise FlashError(f"could not open {port_name}")
        time.sleep(0.5)
        active, _text = is_boot1_active(ser)
        if not active:
            ser.close()
            raise FlashError("boot1 is not answering. Hold PROG through reset, "
                             "then flash again")
        do_flash(ser, uf2_data, persistent)

    print("Running new firmware.")


def main():
    args = sys.argv[1:]
    persistent = '--persistent' in args
    icsp = '--icsp' in args
    icsp_single = '--icsp-single' in args
    for flag in ('--persistent', '--icsp', '--icsp-single'):
        if flag in args:
            args.remove(flag)

    if sum([persistent, icsp, icsp_single]) > 1:
        print("ERROR: Only one mode flag allowed.")
        sys.exit(1)

    try:
        if icsp:
            if len(args) != 3:
                print(f"Usage: {sys.argv[0]} --icsp <DTR_PORT> <CDC_PORT> <uf2_file>")
                print(f"Example: {sys.argv[0]} --icsp COM4 COM9 blink.uf2")
                sys.exit(1)
            flash_uf2(args[1], args[2], icsp=True, icsp_dtr=args[0])
        elif icsp_single:
            if len(args) != 2:
                print(f"Usage: {sys.argv[0]} --icsp-single <PORT> <uf2_file>")
                print(f"Example: {sys.argv[0]} --icsp-single COM4 blink.uf2")
                sys.exit(1)
            flash_uf2(args[0], args[1], icsp_single=True)
        else:
            if len(args) != 2:
                print(f"Usage: {sys.argv[0]} [mode] <serial_port> <uf2_file>")
                print("")
                print("Modes:")
                print("  (default)        Press RESET, bootwait left on after flash")
                print("  --persistent     Press RESET, bootwait turned off after flash")
                print("  --icsp           DTR reset + USB CDC flash (two ports)")
                print("  --icsp-single    DTR reset + UART flash (one port, no USB)")
                sys.exit(1)
            flash_uf2(args[0], args[1], persistent=persistent)
    except FlashError as e:
        print(f"Flash failed: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
