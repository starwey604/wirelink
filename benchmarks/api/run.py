# SPDX-License-Identifier: Apache-2.0
"""Collect Google Benchmark CPU results and real two-process UDP distributions."""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests/tutorials"))
from udp_processes import Process, endpoint_ports

VARIANTS = ["s1_c4_reliable", "s12_c1_reliable", "s12_c4_reliable",
            "s64_c4_reliable", "s12_c4_unreliable"]


def binary(directory, name):
    for subdir in [directory, directory / "Release"]:
        for suffix in ["", ".exe"]:
            path = subdir / (name + suffix)
            if path.is_file():
                return path.resolve()
    raise FileNotFoundError(name)


def build_context(directory):
    keys = {"CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER",
            "CMAKE_C_FLAGS", "CMAKE_C_FLAGS_RELEASE", "CMAKE_CXX_FLAGS",
            "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_INTERPROCEDURAL_OPTIMIZATION",
            "WIRELINK_WLC_CODEGEN_ABI"}
    for parent in [directory.resolve(), *directory.resolve().parents]:
        cache = parent / "CMakeCache.txt"
        if cache.is_file():
            values = {}
            for line in cache.read_text(encoding="utf-8").splitlines():
                key = line.split(":", 1)[0]
                if key in keys and "=" in line:
                    values[key] = line.split("=", 1)[1]
            return values
    return {}  # Standalone copied executables have no discoverable build cache.


def udp_run(executable, samples, warmup, size):
    client_port, server_port = endpoint_ports()
    common = [str(samples), str(warmup), str(size)]
    server = Process([str(executable), "server", str(server_port), str(client_port), *common])
    client = None
    try:
        server.ready("READY")
        client = Process([str(executable), "client", str(client_port), str(server_port), *common])
        code, output = client.output(timeout=max(30, samples * 0.02 + warmup * 0.02))
        if code or server.process.poll() is not None:
            raise RuntimeError(f"UDP failure: {code}\n{output}\n{''.join(server.lines)}")
        server.ready('"role":"server"')
        result = json.loads(output)
        result["server"] = json.loads(next(line for line in server.lines if line.startswith("{")))
        if result["calls"] != samples or result["server"]["calls"] != samples:
            raise RuntimeError("incomplete UDP sample window")
        return result
    finally:
        if client:
            client.close()
        server.close()


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, help="omit for a non-persistent smoke run")
    parser.add_argument("--repetitions", type=positive, default=5)
    parser.add_argument("--min-time", type=float, default=0.05)
    parser.add_argument("--samples", type=positive, default=2000)
    parser.add_argument("--warmup", type=positive, default=200)
    parser.add_argument("--udp-rounds", type=positive, default=3)
    parser.add_argument("--skip-micro", action="store_true")
    parser.add_argument("--skip-udp", action="store_true")
    args = parser.parse_args()
    if args.out is not None and args.out.exists():
        parser.error("output already exists; use a new filename to preserve evidence")
    if args.skip_micro and args.skip_udp:
        parser.error("cannot skip both workloads")
    if not 0 < args.min_time <= 60 or args.samples > 1000000 or args.warmup > 1000000:
        parser.error("invalid timing/sample bounds")
    report = {"format": "wirelink-performance-v1", "context": {
        "system": platform.system(), "machine": platform.machine(), "host": platform.node(),
        "samples": args.samples, "warmup": args.warmup,
        "build": build_context(args.bin_dir),
        "micro_repetitions": args.repetitions, "micro_min_time_s": args.min_time,
        "udp_rounds": args.udp_rounds,
    }, "artifacts": {}, "micro": {}, "udp": []}
    def record_executable(name):
        path = binary(args.bin_dir, name)
        report["artifacts"][name] = hashlib.sha256(path.read_bytes()).hexdigest()
        return path
    with tempfile.TemporaryDirectory(prefix="wirelink-benchmark-") as work:
        if not args.skip_micro:
            for variant in VARIANTS:
                output = Path(work) / f"{variant}.json"
                subprocess.run([str(record_executable(f"wirelink_api_{variant}")),
                                f"--benchmark_min_time={args.min_time}s",
                                f"--benchmark_repetitions={args.repetitions}",
                                "--benchmark_enable_random_interleaving=true",
                                f"--benchmark_out={output}", "--benchmark_out_format=json"],
                               check=True, timeout=max(120, args.repetitions * args.min_time * 200))
                result = json.loads(output.read_text(encoding="utf-8"))
                if not result["benchmarks"] or any(b.get("error_occurred") for b in result["benchmarks"]):
                    raise RuntimeError(f"failed benchmark: {variant}")
                report["micro"][variant] = result
        if not args.skip_udp:
            worker = record_executable("wirelink_api_udp")
            for size in [0, 32, 512, 1024]:
                for repetition in range(args.udp_rounds):
                    result = udp_run(worker, args.samples, args.warmup, size)
                    result["repetition"] = repetition
                    report["udp"].append(result)
                    print(f"UDP {size} B: p50={result['p50_ns']/1000:.1f} us "
                          f"p99={result['p99_ns']/1000:.1f} us", flush=True)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2, allow_nan=False)
            stream.write("\n")


if __name__ == "__main__":
    main()
