# SPDX-License-Identifier: Apache-2.0
"""Build a test-only wrapper around the product HIL without editing its sources."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--workspace", type=Path, required=True)
parser.add_argument("--wlc", type=Path, required=True)
parser.add_argument("--build-dir", type=Path, required=True)
parser.add_argument("--lock-profile", choices=["on", "off"], required=True)
parser.add_argument("--period-us", type=int, choices=[1000, 2000], default=2000)
args = parser.parse_args()
workspace = args.workspace.resolve()
modules = ["firmware", "modules/lib/wirelink", "modules/lib/protocol", "modules/lib/onepid",
           "modules/hal/cmsis_6", "modules/hal/stm32", "modules/debug/segger"]
subprocess.run([str(workspace / ".venv/bin/west"), "build", "--cmake",
    "-s", str(Path(__file__).resolve().parent), "-b", "dm_mc02",
    "-d", str(args.build_dir.resolve()), "-o=-j2", "--",
    f"-DRAGTIME_FIRMWARES_ROOT={workspace}", f"-DWLC_EXECUTABLE={args.wlc.resolve()}",
    "-DWIRELINK_WLC_AUTO_DOWNLOAD=OFF",
    f"-DWIRELINK_H7_LOCK_PROFILING={args.lock_profile.upper()}",
    f"-DCONFIG_WILLOW_WIRELINK_HIL_CONTROL_PERIOD_US={args.period_us}",
    "-DZEPHYR_MODULES=" + ";".join(str(workspace / path) for path in modules)],
    cwd=workspace, env=os.environ, check=True)
