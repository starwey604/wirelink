# SPDX-License-Identifier: Apache-2.0
"""Install both SDK wheels offline with no compiler PATH and call independent C peers."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import venv
import zipfile


SMOKE = r'''
import importlib.util
import shutil
import subprocess
import sys
import device_sdk as device
import calculator_sdk as calculator

for module in ('nanobind', 'scikit_build_core', 'build', 'pytest'):
    assert importlib.util.find_spec(module) is None, module
for tool in ('cc', 'c++', 'cl', 'cmake', 'cargo', 'wlc'):
    assert shutil.which(tool) is None, tool
assert calculator._native.Error is not device._native.Error
with subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, text=True) as calc_peer, \
     subprocess.Popen([sys.argv[2]], stdout=subprocess.PIPE, text=True) as device_peer:
    try:
        cp = int(calc_peer.stdout.readline())
        dp = int(device_peer.stdout.readline())
        with calculator.Client.connect(calculator.Udp(peer=('127.0.0.1', cp))) as c, \
             device.Client.connect(device.Udp(peer=('127.0.0.1', dp))) as d:
            assert c.add(left=20, right=22).sum == 42
            info = d.get_info()
            assert info.settings.mode == 12345
            assert info.settings.label is None
            assert info.settings.label_or_default == '设备'
            response = d.configure(settings=info.settings, opaque=b'')
            assert response.opaque == b''
            assert response.settings == info.settings
            try:
                c.add(left=2**31 - 1, right=1)
            except calculator.RejectedError as error:
                assert error.rejection == 1
            else:
                raise AssertionError('expected business rejection')
        assert response.settings.token == b'\0\x80\xff'
    finally:
        calc_peer.terminate()
        device_peer.terminate()
        calc_peer.wait(timeout=5)
        device_peer.wait(timeout=5)

# A fresh pair of C peers also exercises both independent asyncio bridges.
import asyncio
with subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, text=True) as calc_peer, \
     subprocess.Popen([sys.argv[2]], stdout=subprocess.PIPE, text=True) as device_peer:
    try:
        cp = int(calc_peer.stdout.readline())
        dp = int(device_peer.stdout.readline())
        async def exercise():
            async with calculator.AsyncClient.connect(calculator.Udp(peer=('127.0.0.1', cp))) as c, \
                       device.AsyncClient.connect(device.Udp(peer=('127.0.0.1', dp))) as d:
                result, info = await asyncio.gather(c.add(left=20, right=22), d.get_info())
                assert result.sum == 42 and info.settings.mode == 12345
                saved = await d.configure(settings=info.settings, opaque=b'')
            assert saved.settings.token == b'\0\x80\xff' and saved.opaque == b''
        asyncio.run(exercise())
    finally:
        calc_peer.terminate()
        device_peer.terminate()
        calc_peer.wait(timeout=5)
        device_peer.wait(timeout=5)
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--calculator-wheel", required=True, type=Path)
    parser.add_argument("--device-wheel", required=True, type=Path)
    parser.add_argument("--calculator-server", required=True, type=Path)
    parser.add_argument("--device-server", required=True, type=Path)
    args = parser.parse_args()
    wheels = [args.calculator_wheel.resolve(), args.device_wheel.resolve()]
    for wheel, package in zip(wheels, ("calculator_sdk", "device_sdk")):
        with zipfile.ZipFile(wheel) as archive:
            paths = archive.namelist()
            assert f"{package}/py.typed" in paths
            assert any(p.endswith((".so", ".pyd")) for p in paths)
            assert not any(p.endswith((".a", ".lib", ".h", ".hpp")) for p in paths)
            for license in ("wirelink.txt", "nanobind.txt", "asio.txt", "robin_map.txt"):
                assert any("licenses/" in p and p.endswith(license) for p in paths), license
    with tempfile.TemporaryDirectory(prefix="wirelink-generated-wheels-") as temporary:
        root = Path(temporary)
        venv.EnvBuilder(with_pip=True).create(root / "venv")
        scripts = root / "venv" / ("Scripts" if os.name == "nt" else "bin")
        python = scripts / ("python.exe" if os.name == "nt" else "python")
        env = dict(os.environ)
        for key in list(env):
            if key.startswith(("PYTHON", "WIRELINK_")) or key in ("CMAKE_PREFIX_PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
                env.pop(key)
        subprocess.run([str(python), "-m", "pip", "install", "--no-index", "--no-deps", *map(str, wheels)],
                       check=True, cwd=root, env=env, timeout=60)
        env["PATH"] = str(scripts)
        subprocess.run([str(python), "-I", "-c", SMOKE, str(args.calculator_server.resolve()),
                        str(args.device_server.resolve())], check=True, cwd=root, env=env, timeout=20)
    print("Two offline SDK wheels and independent C peers PASS")


if __name__ == "__main__":
    main()
