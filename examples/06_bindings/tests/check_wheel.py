# SPDX-License-Identifier: Apache-2.0
"""Install the wheel offline in a clean venv and communicate with a C service."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import venv
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wheel", required=True, type=Path)
    parser.add_argument("--server", required=True, type=Path)
    args = parser.parse_args()
    wheel, server = args.wheel.resolve(), args.server.resolve()
    with zipfile.ZipFile(wheel) as archive:
        names = archive.namelist()
        assert "calculator_sdk/py.typed" in names
        assert any(name.endswith((".so", ".pyd")) for name in names)
        assert not any(name.endswith((".a", ".lib", ".h", ".hpp")) for name in names)
        assert any("licenses/" in name and name.endswith("nanobind.txt") for name in names)

    with tempfile.TemporaryDirectory(prefix="wirelink-sdk-consumer-") as temporary:
        root = Path(temporary)
        venv.EnvBuilder(with_pip=True).create(root / "venv")
        scripts = root / "venv" / ("Scripts" if os.name == "nt" else "bin")
        python = scripts / ("python.exe" if os.name == "nt" else "python")
        env = dict(os.environ)
        for key in list(env):
            if key.startswith(("PYTHON", "WIRELINK_")) or key in (
                "CMAKE_PREFIX_PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
                env.pop(key)
        subprocess.run([str(python), "-m", "pip", "install", "--no-index", "--no-deps", str(wheel)],
                       check=True, cwd=root, env=env, timeout=60)
        # The consumer's PATH contains no C/C++ compiler, CMake, Rust or WLC.
        env["PATH"] = str(scripts)
        subprocess.run([str(python), "-I", "-c", SMOKE, str(server)],
                       check=True, cwd=root, env=env, timeout=15)
    print("Offline wheel installation and C peer communication PASS")


SMOKE = """
import importlib.util
import shutil
import subprocess
import sys
from calculator_sdk import Client, Udp, RejectedError

for module in ('nanobind', 'scikit_build_core', 'build', 'pytest'):
    assert importlib.util.find_spec(module) is None, module
for tool in ('cc', 'c++', 'cl', 'cmake', 'cargo', 'wlc'):
    assert shutil.which(tool) is None, tool
with subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, text=True) as server:
    try:
        port = int(server.stdout.readline())
        with Client.connect(Udp(peer=('127.0.0.1', port))) as client:
            saved = client.add(left=20, right=22)
            assert saved.sum == 42
            try:
                client.add(left=2**31 - 1, right=1)
            except RejectedError as error:
                assert error.rejection == 1
            else:
                raise AssertionError('expected business rejection')
        assert saved.sum == 42
    finally:
        server.terminate()
        server.wait(timeout=5)
"""


if __name__ == "__main__":
    main()
