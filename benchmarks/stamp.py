#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stamp a Google Benchmark report with its workload source hash.

Run it right after every capture, before editing workload sources:

    python3 benchmarks/stamp.py before.json \\
      benchmarks/framing/workload.c benchmarks/framing/workload.h \\
      benchmarks/framing/micro.cpp

Comparators treat two reports stamped from different workload sources as
incomparable (exit 2), never as a regression. See benchmarks/SPEC.md.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from report import manifest  # noqa: E402  (sys.path set up above)

if __name__ == "__main__":
    raise SystemExit(manifest.main(["stamp", *sys.argv[1:]]))
