#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Capture the ESP32-S3 benchmark console to a log file.

The firmware benchmarks print to the DevKitC USB Serial/JTAG console. Start
this script, then reset the board so the boot banner and the full result are
captured. The port is reopened if USB re-enumerates during reset.
"""
import argparse
from pathlib import Path
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True,
                        help="Serial device, for example /dev/ttyACM0 or COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--reset", action="store_true",
                        help="Hard-reset an ESP USB Serial/JTAG target after "
                             "opening")
    parser.add_argument("--until", default="",
                        help="Stop after this substring appears (for example result=pass)")
    parser.add_argument("--seconds", type=float, default=0.0,
                        help="Overall capture limit; 0 requires --until")
    args = parser.parse_args()
    if args.seconds <= 0 and not args.until:
        parser.error("pass --seconds or --until so the capture terminates")
    try:
        import serial
    except ImportError:
        raise SystemExit("pyserial is required: python -m pip install pyserial")

    deadline = time.monotonic() + args.seconds if args.seconds > 0 else None
    marker = args.until.encode()
    seen = bytearray()
    reset_pending = args.reset
    while True:
        try:
            with serial.Serial(args.port, args.baud, timeout=0.2) as port, \
                    args.out.open("ab") as log:
                if reset_pending:
                    # On Espressif USB Serial/JTAG, RTS drives active-low EN
                    # and DTR drives active-low GPIO0. Keep GPIO0 deasserted so
                    # the target boots the application instead of the loader.
                    port.dtr = False
                    port.rts = True
                    time.sleep(0.1)
                    port.rts = False
                    reset_pending = False
                while True:
                    chunk = port.read(4096)
                    if chunk:
                        log.write(chunk)
                        log.flush()
                        seen.extend(chunk)
                        sys.stdout.buffer.write(chunk)
                        sys.stdout.buffer.flush()
                        if marker and marker in seen:
                            print(f"\ncaptured {args.out}", file=sys.stderr)
                            return
                    if deadline is not None and time.monotonic() >= deadline:
                        print(f"\ncaptured {args.out}", file=sys.stderr)
                        return
        except (serial.SerialException, OSError):
            if deadline is not None and time.monotonic() >= deadline:
                print(f"\ncaptured {args.out}", file=sys.stderr)
                return
            time.sleep(0.5)


if __name__ == "__main__":
    main()
