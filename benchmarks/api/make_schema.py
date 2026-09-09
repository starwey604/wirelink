# SPDX-License-Identifier: Apache-2.0
"""Generate deterministic benchmark workloads, not application source templates."""
import argparse
from pathlib import Path


def generate(count, delivery):
    schema = ["version 1;"]
    profile = ["profile version 1;"]
    for index in range(count):
        name = "Echo" if index == 0 else f"Service{index:02d}"
        for role, offset in [("Request", 0), ("Response", 1)]:
            schema.append(f"message {name}{role} @id({100 + index * 2 + offset}) {{\n"
                          "  required fixed32 sequence @id(1);\n"
                          "  required bytes<1024> body @id(2);\n}")
        profile.append(f"rpc {name} {{\n  request = {name}Request @delivery({delivery});\n"
                       f"  response = {name}Response @delivery({delivery});\n}}")
    schema.append("message Telemetry @id(1000) { required packed fixed32 values[128] @id(1); }")
    profile.append("latest Telemetry { delivery = unreliable; }")
    return "\n".join(schema) + "\n", "\n".join(profile) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--services", type=int, choices=[1, 12, 64], required=True)
    parser.add_argument("--delivery", choices=["reliable", "unreliable"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in zip(["load.wl", "load.bind.wl"], generate(args.services, args.delivery)):
        path = args.output / name
        if not path.exists() or path.read_text(encoding="utf-8") != data:
            path.write_text(data, encoding="utf-8")
