# SPDX-License-Identifier: Apache-2.0
"""Check every generated SDK artifact against a matching local WLC compiler."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wlc", required=True, type=Path)
    args = parser.parse_args()
    compiler = args.wlc.resolve()
    root = Path(__file__).resolve().parents[2]
    checked = 0
    with tempfile.TemporaryDirectory(prefix="wirelink-generated-sdk-") as temporary:
        for example, name in (("06_bindings", "calculator"), ("07_device_sdk", "device")):
            project = root / "examples" / example
            output = Path(temporary) / name
            subprocess.run([
                str(compiler), "sdk", str(project / "schema" / f"{name}.wl"),
                "--profile", str(project / "schema" / f"{name}.bind.wl"),
                "--out-dir", str(output), "--name", name,
            ], check=True)
            for artifact in sorted(output.rglob("*")):
                if artifact.is_file():
                    relative = artifact.relative_to(output)
                    expected = project / relative
                    if not expected.is_file() or artifact.read_bytes() != expected.read_bytes():
                        raise SystemExit(f"SDK differs from WLC output: {expected}")
                    checked += 1
    print(f"Verified {checked} generated SDK artifacts")


if __name__ == "__main__":
    main()
