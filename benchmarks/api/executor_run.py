# SPDX-License-Identifier: Apache-2.0
"""Repeat real executor workloads; save every log, including failed executions."""
import argparse
import hashlib
import json
import platform
import random
import subprocess
from pathlib import Path


def parse_stages(lines):
    stages = {}
    for line in lines:
        fields = dict(part.split("=", 1) for part in line.split(",")[1:] if "=" in part)
        if "stage" in fields:
            name = fields.pop("stage")
            stages[name] = {key: int(value) for key, value in fields.items()}
    return stages


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--saturated-calls", type=int, default=5000)
    parser.add_argument("--paced-calls", type=int, default=1000)
    parser.add_argument("--profile", choices=["off", "locks", "full"], required=True)
    parser.add_argument("--skip-paced", action="store_true")
    args = parser.parse_args()
    if args.out.exists() or min(args.repetitions, args.saturated_calls, args.paced_calls) < 1:
        parser.error("require positive counts and a new output file")
    logdir = args.out.with_suffix("")
    logdir.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    report = {"format": "wirelink-executor-performance-v1", "profile": args.profile,
              "host": platform.node(), "system": platform.platform(),
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "runs": []}
    cases = [(mode, producers, size, period, repeat)
             for repeat in range(args.repetitions)
             for mode in ["rpc", "proxy", "latest", "shared"]
             for producers in [1, 2, 4, 8] for size in [32, 512]
             for period in ([0] if args.skip_paced else [0, 1000])]
    random.Random(28).shuffle(cases)
    for index, (mode, producers, size, period, repeat) in enumerate(cases):
        count = args.paced_calls if period else args.saturated_calls
        command = [str(binary), mode, str(producers), str(count), str(size), str(period)]
        filename = f"{index:03d}-{mode}-p{producers}-b{size}-us{period}-r{repeat}.log"
        try:
            result = subprocess.run(command, capture_output=True, text=True,
                                    timeout=max(30, count * period / 1e6 + 15))
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")
            (logdir / filename).write_text(decoded(error.stdout) + decoded(error.stderr) + "\nTIMEOUT\n",
                                          encoding="utf-8")
            raise RuntimeError(f"benchmark timed out: {logdir / filename}") from error
        (logdir / filename).write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"benchmark failed ({result.returncode}): {logdir / filename}")
        lines = result.stdout.splitlines()
        row = json.loads(lines[0])
        if row["calls"] != count * producers or row["errors"]:
            raise RuntimeError(f"incomplete workload: {filename}")
        stages = parse_stages(lines[1:])
        if row["profile"] != args.profile or bool(stages) != (args.profile != "off"):
            raise RuntimeError("requested profiling mode does not match the executable")
        row.update(repetition=repeat, stages=stages, log=filename)
        report["runs"].append(row)
        print(f"{index+1}/{len(cases)} {mode} p{producers} {size}B period={period}: "
              f"p50={row['p50_ns']/1000:.2f}us cpu={row['cpu_ns_per_call']/1000:.2f}us", flush=True)
    with args.out.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")


if __name__ == "__main__":
    main()
