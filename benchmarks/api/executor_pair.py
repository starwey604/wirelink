# SPDX-License-Identifier: Apache-2.0
"""Alternate frozen baseline/candidate executables; never run them concurrently."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import random
import subprocess

from executor_analyze import summarize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--saturated-calls", type=int, default=3000)
    parser.add_argument("--paced-calls", type=int, default=300)
    args = parser.parse_args()
    if args.out.exists() or min(args.repetitions, args.saturated_calls, args.paced_calls) < 1:
        parser.error("require a new output directory and positive counts")
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    reports = {name: {"format": "wirelink-executor-performance-v1", "profile": "off",
                     "host": platform.node(), "system": platform.platform(),
                     "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "runs": []}
               for name, binary in binaries.items()}
    args.out.mkdir(parents=True, exist_ok=False)
    cases = [(mode, producers, size, period, repeat)
             for repeat in range(args.repetitions)
             for mode in ["rpc", "proxy", "latest", "shared"]
             for producers in [1, 8] for size in [32, 512] for period in [0, 1000]]
    rng = random.Random(30)
    rng.shuffle(cases)
    for index, (mode, producers, size, period, repeat) in enumerate(cases):
        count = args.paced_calls if period else args.saturated_calls
        order = list(binaries)
        rng.shuffle(order)
        for name in order:
            command = [str(binaries[name]), mode, str(producers), str(count), str(size), str(period)]
            log = args.out / f"{index:03d}-{name}-{mode}-p{producers}-b{size}-us{period}-r{repeat}.log"
            try:
                process = subprocess.run(command, capture_output=True, text=True,
                                         timeout=max(30, count * period / 1e6 + 15))
            except subprocess.TimeoutExpired as error:
                def decoded(value):
                    return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")
                log.write_text(decoded(error.stdout) + decoded(error.stderr) + "\nTIMEOUT\n", encoding="utf-8")
                raise RuntimeError(f"timed out: {log}") from error
            log.write_text(process.stdout + process.stderr, encoding="utf-8")
            if process.returncode:
                raise RuntimeError(f"failed ({process.returncode}): {log}")
            row = json.loads(process.stdout.splitlines()[0])
            if (row["errors"] or row["calls"] != count * producers or row["profile"] != "off"
                    or row["activity_enabled"]):
                raise RuntimeError(f"invalid workload or enabled instrumentation: {log}")
            row.update(repetition=repeat, stages={}, log=log.name)
            reports[name]["runs"].append(row)
        print(f"{index + 1}/{len(cases)} paired {mode} p{producers} {size}B period={period}", flush=True)
    before = reports["before"]["runs"][0]
    after = reports["after"]["runs"][0]
    if before["abi"] != after["abi"]:
        raise RuntimeError("different generated ABI; investigate before comparing")
    for name, report in reports.items():
        (args.out / f"{name}.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        (args.out / f"{name}-summary.json").write_text(
            json.dumps(summarize(report), indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
