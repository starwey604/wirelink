# SPDX-License-Identifier: Apache-2.0
"""Run the current many-service application demo; Python is not a protocol peer."""
import argparse
import subprocess


def check_demo(executable, extended=False):
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
    expected = ['device/calculator handlers, application job ID, ordinary completion and retained results passed']
    if extended:
        expected.append('new GetBuildLabel RPC: OK')
    assert result.returncode == 0 and all(value in result.stdout for value in expected), result.stdout + result.stderr
    print(result.stdout, end='')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--demo', required=True)
    check_demo(parser.parse_args().demo)
