#!/usr/bin/env python3
"""
serial_flash.py - Flash UF2 to Dabao via boot1 serial REPL

Modes:
  (default)        Press RESET first, bootwait enabled after flash
  --persistent     Press RESET first, bootwait disabled after flash
  --icsp           DTR reset via serial adapter, flash via USB CDC (two ports)
  --icsp-single    DTR reset + flash over same serial adapter (one port, no USB)

Source: bao1x-boot/boot1/src/repl.rs
"""

import sys
import time
import base64
import struct

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed")
    print("Install with: pip install pyserial")
    sys.exit(1)


UF2_BLOCK_SIZE = 512
UF2_PAYLOAD_SIZE = 256
BOOT1_BAUD = 1000000
CHUNK_SIZE = 64
CHUNK_DELAY = 0.005


def send_throttled(ser, data):
    """Send data in small chunks with delays so the REPL can keep up."""
    encoded = data.encode('ascii') if isinstance(data, str) else data
    for i in range(0, len(encoded), CHUNK_SIZE):
        ser.write(encoded[i:i + CHUNK_SIZE])
        time.sleep(CHUNK_DELAY)


def wait_for_response(ser, keyword, timeout=3.0):
    """Wait until a keyword appears in the response."""
    response = b''
    deadline = time.time() + timeout
    while time.time() < deadline:
        n = ser.in_waiting
        if n > 0:
            response += ser.read(n)
            if keyword.encode() in response:
                time.sleep(0.02)
                response += ser.read(ser.in_waiting or 0)
                return response.decode('utf-8', errors='replace').strip()
        time.sleep(0.005)
    return response.decode('utf-8', errors='replace').strip()


def send_command(ser, cmd, wait=0.2):
    ser.write((cmd + '\r\n').encode('ascii'))
    time.sleep(wait)
    response = ser.read(ser.in_waiting or 1)
    return response.decode('utf-8', errors='replace').strip()


def send_command_throttled(ser, cmd, wait=0.5):
    ser.reset_input_buffer()
    send_throttled(ser, cmd + '\r\n')
    time.sleep(wait)
    response = ser.read(ser.in_waiting or 1)
    return response.decode('utf-8', errors='replace').strip()


def is_boot1_active(ser):
    ser.reset_input_buffer()
    ser.write(b'\r\n')
    time.sleep(0.3)
    response = ser.read(ser.in_waiting or 1)
    text = response.decode('utf-8', errors='replace').strip()
    return 'Commands include' in text or 'uf2' in text or 'bootwait' in text, text


def try_open_port(port_name, baudrate, timeout=2):
    try:
        return serial.Serial(port_name, baudrate, timeout=timeout)
    except (serial.SerialException, OSError):
        return None


def dtr_reset(dtr_port, cdc_port, baudrate=115200):
    """Two-port ICSP: DTR reset on adapter, flash on USB CDC."""
    ser = try_open_port(dtr_port, baudrate)
    if ser is None:
        print(f"ERROR: Could not open {dtr_port}")
        sys.exit(1)

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
            active, text = is_boot1_active(cdc)
            if active:
                print("Boot1 REPL detected.")
                return cdc
            cdc.close()
        if attempt < 9:
            print(f"  Waiting... ({attempt + 1}/10)")
            time.sleep(1.5)

    print(f"ERROR: Could not reach boot1 REPL on {cdc_port}")
    sys.exit(1)


def dtr_reset_single(port_name):
    """Single-port ICSP: DTR reset, flash over hardware UART at 1 Mbaud."""
    ser = try_open_port(port_name, BOOT1_BAUD)
    if ser is None:
        print(f"ERROR: Could not open {port_name}")
        sys.exit(1)

    print(f"Resetting board via DTR on {port_name}...")
    ser.dtr = True
    time.sleep(0.1)
    ser.dtr = False

    print(f"Waiting for boot1 on hardware UART ({BOOT1_BAUD} baud)...")
    time.sleep(3.0)

    for attempt in range(10):
        active, text = is_boot1_active(ser)
        if active:
            print("Boot1 REPL detected.")

            # Disable local echo to reduce traffic
            ser.reset_input_buffer()
            send_throttled(ser, 'localecho off\r\n')
            time.sleep(0.3)
            ser.reset_input_buffer()

            return ser
        if attempt < 9:
            print(f"  Waiting... ({attempt + 1}/10)")
            time.sleep(1.5)

    print(f"ERROR: Could not reach boot1 REPL on {port_name}")
    ser.close()
    sys.exit(1)


def do_flash(ser, uf2_data, persistent=False, use_throttle=False):
    """Flash UF2 blocks, set bootwait, and boot."""
    num_blocks = len(uf2_data) // UF2_BLOCK_SIZE

    ser.reset_input_buffer()

    for i in range(num_blocks):
        offset = i * UF2_BLOCK_SIZE
        block = uf2_data[offset:offset + UF2_BLOCK_SIZE]
        b64 = base64.b64encode(block).decode('ascii')

        cmd = f"uf2 {b64}\r\n"

        if use_throttle:
            ser.reset_input_buffer()
            send_throttled(ser, cmd)
            resp_text = wait_for_response(ser, 'Wrote', timeout=3.0)
            if 'Wrote' not in resp_text:
                resp_text = wait_for_response(ser, 'rote', timeout=2.0)
        else:
            ser.write(cmd.encode('ascii'))
            time.sleep(0.05)
            response = ser.read(ser.in_waiting or 1)
            resp_text = response.decode('utf-8', errors='replace').strip()

        addr = struct.unpack_from('<I', block, 12)[0]

        # Extract the Wrote line
        display = resp_text
        for line in resp_text.split('\n'):
            line = line.strip()
            if 'Wrote' in line or 'Invalid' in line:
                display = line
                break

        print(f"  [{i+1}/{num_blocks}] 0x{addr:08X} - {display}")

    print("")
    print("All blocks written.")

    if persistent:
        print("Disabling boot-wait for persistent boot...")
        if use_throttle:
            resp = send_command_throttled(ser, 'bootwait disable')
        else:
            resp = send_command(ser, 'bootwait disable', wait=0.3)
        print(f"  {resp}")
    else:
        print("Enabling boot-wait (reset will return to REPL)...")
        if use_throttle:
            resp = send_command_throttled(ser, 'bootwait enable')
        else:
            resp = send_command(ser, 'bootwait enable', wait=0.3)
        print(f"  {resp}")

    print("Sending boot command...")
    time.sleep(0.5)

    if use_throttle:
        send_throttled(ser, 'boot\r\n')
    else:
        ser.write(b'boot\r\n')
    time.sleep(0.5)
    response = ser.read(ser.in_waiting or 1)
    print(f"Boot response: {response.decode('utf-8', errors='replace').strip()}")

    ser.close()


def flash_uf2(port_name, uf2_file, persistent=False, icsp=False,
              icsp_single=False, icsp_dtr=None, baudrate=115200):
    with open(uf2_file, 'rb') as f:
        uf2_data = f.read()

    num_blocks = len(uf2_data) // UF2_BLOCK_SIZE
    print(f"Flashing {uf2_file}: {num_blocks} blocks")

    if icsp_single:
        print("ICSP single-port mode: DTR reset + hardware UART flash")
        ser = dtr_reset_single(port_name)
        do_flash(ser, uf2_data, persistent, use_throttle=True)
    elif icsp:
        print("ICSP mode: DTR reset via serial adapter")
        ser = dtr_reset(icsp_dtr, port_name, baudrate)
        do_flash(ser, uf2_data, persistent)
    else:
        if persistent:
            print("Persistent mode: boot-wait will be disabled after flashing")
        ser = serial.Serial(port_name, baudrate, timeout=2)
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.write(b'\r\n')
        time.sleep(0.2)
        response = ser.read(ser.in_waiting or 1)
        print(f"Boot1 response: {response.decode('utf-8', errors='replace').strip()}")
        do_flash(ser, uf2_data, persistent)

    if persistent:
        print("Done. Firmware will run on every power cycle and reset.")
    else:
        print("Done. Board should be running new firmware.")
        if icsp_single:
            print(f"Re-flash anytime with: flash_icsp_single {port_name} <example>")
        elif icsp:
            print(f"Re-flash anytime with: flash_icsp {icsp_dtr} {port_name} <example>")


def main():
    args = sys.argv[1:]
    persistent = False
    icsp = False
    icsp_single = False

    if '--persistent' in args:
        persistent = True
        args.remove('--persistent')

    if '--icsp' in args:
        icsp = True
        args.remove('--icsp')

    if '--icsp-single' in args:
        icsp_single = True
        args.remove('--icsp-single')

    mode_count = sum([persistent, icsp, icsp_single])
    if mode_count > 1:
        print("ERROR: Only one mode flag allowed.")
        sys.exit(1)

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
            print(f"")
            print(f"Modes:")
            print(f"  (default)        Press RESET, bootwait enabled after flash")
            print(f"  --persistent     Press RESET, bootwait disabled after flash")
            print(f"  --icsp           DTR reset + USB CDC flash (two ports)")
            print(f"  --icsp-single    DTR reset + UART flash (one port, no USB)")
            sys.exit(1)
        flash_uf2(args[0], args[1], persistent=persistent)


if __name__ == '__main__':
    main()