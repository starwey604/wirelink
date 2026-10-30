# SPDX-License-Identifier: Apache-2.0
"""Verify generator/version rejection before a legacy engine can be compiled."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cmake', required=True)
p.add_argument('--prefix', required=True)
p.add_argument('--wlc', required=True)
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='wirelink-current-negative-') as temporary:
    root = Path(temporary)
    project = root / 'project'
    project.mkdir()
    (project / 'test.wl').write_text('version 1; message Tick @id(1) {}\n')
    (project / 'test.bind.wl').write_text('profile version 1; send Tick { delivery=unreliable; }\n')
    common = '''cmake_minimum_required(VERSION 3.21)
project(current_negative LANGUAGES C)
find_package(Wirelink CONFIG REQUIRED)
'''
    cases = [
        ('legacy_profile', 'wirelink_wlc_generate(TARGET probe SCHEMA test.wl PROFILE test.bind.wl)', a.wlc, 'requires profile version 2'),
        ('legacy_runtime', 'wirelink_wlc_generate_runtime(TARGET probe)', a.wlc, 'Separate legacy runtime generation was removed'),
        ('missing_compiler', 'wirelink_wlc_generate_codec(TARGET probe SCHEMA test.wl)', str(root / 'absent'), 'Supply matching WLC 0.13.0'),
    ]
    if os.name == 'posix':
        fake = root / 'old-wlc'
        fake.write_text('#!/bin/sh\nprintf "wlc 0.12.0\\n"\n')
        fake.chmod(0o755)
        cases.append(('old_compiler', 'wirelink_wlc_generate_codec(TARGET probe SCHEMA test.wl)', str(fake), "expected 'wlc 0.13.0'"))
    for name, invocation, compiler, diagnostic in cases:
        (project / 'CMakeLists.txt').write_text(common + invocation + '\n')
        result = subprocess.run([a.cmake, '-S', str(project), '-B', str(root / name),
                                 '-DCMAKE_PREFIX_PATH=' + a.prefix,
                                 '-DWIRELINK_WLC_EXECUTABLE=' + compiler], capture_output=True, text=True)
        output = result.stdout + result.stderr
        assert result.returncode and diagnostic in output, (name, output)
        assert not list((root / name).rglob('*_runtime.c'))
print('Current profile, removed runtime and missing compiler diagnostics PASS')
