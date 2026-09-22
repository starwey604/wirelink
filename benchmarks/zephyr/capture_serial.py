#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Capture the ESP32-S3 benchmark console to a log file.

The firmware benchmarks print to the DevKitC USB Serial/JTAG console. Start
this script, then reset the board so the boot banner and the full result are
captured. The port is reopened if USB re-enumerates during reset.

Exit codes follow benchmarks/SPEC.md: 0 when the capture is complete (the
--until marker was seen, or the run is seconds-bounded with no marker
requested), 2 when the capture ends without a requested marker. An
incomplete log therefore never looks like a successful run.
"""
import argparse
from pathlib import Path
import sys
import time

EXIT_OK = 0
EXIT_INCOMPLETE = 2


def create_log(path):
    """Create the capture file exclusively; never append to a stale log.

    The file is created once up front and only reopened for append when USB
    re-enumeration forces a reopen, so an existing log from a previous run
    fails fast instead of polluting the new capture.
    """
    try:
        with path.open("xb"):
            pass
    except FileExistsError:
        raise SystemExit(
            f"{path} already exists; refusing to append to an existing "
            "capture. Remove it or pass a new --out path.")


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

    create_log(args.out)
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
                            return EXIT_OK
                    if deadline is not None and time.monotonic() >= deadline:
                        return timed_out(args, marker, seen)
        except (serial.SerialException, OSError):
            if deadline is not None and time.monotonic() >= deadline:
                return timed_out(args, marker, seen)
            time.sleep(0.5)


def timed_out(args, marker, seen):
    """End of a seconds-bounded capture: fail if a marker was requested."""
    if marker and marker not in seen:
        print(f"\ncapture of {args.out} timed out before {args.until!r}; "
              "the log is incomplete", file=sys.stderr)
        return EXIT_INCOMPLETE
    print(f"\ncaptured {args.out}", file=sys.stderr)
    return EXIT_OK


if __name__ == "__main__":
    raise SystemExit(main())
