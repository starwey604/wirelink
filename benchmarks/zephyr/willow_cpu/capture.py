# SPDX-License-Identifier: Apache-2.0
"""Capture from the active Commander's RTT server; never opens a second probe."""
import argparse
from pathlib import Path
import socket
import time

parser = argparse.ArgumentParser()
parser.add_argument("--seconds", type=float, default=120)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
if args.seconds <= 0 or args.out.exists():
    parser.error("require a positive duration and a new output file")
deadline = time.monotonic() + args.seconds
with args.out.open("xb") as output, socket.create_connection(("127.0.0.1", 19021), timeout=5) as stream:
    stream.settimeout(1)
    print("RTT connected", flush=True)
    while time.monotonic() < deadline:
        try:
            chunk = stream.recv(16384)
        except socket.timeout:
            continue
        if not chunk:
            break
        output.write(chunk)
        output.flush()
