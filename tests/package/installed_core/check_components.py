# SPDX-License-Identifier: Apache-2.0
"""Require specific dependency diagnostics, not just any configure failure."""
import pathlib
import subprocess
import sys
cmake, prefix, binary = sys.argv[1:]
source = pathlib.Path(binary, "source")
source.mkdir(parents=True, exist_ok=True)
(source / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.21)
project(missing_backend LANGUAGES C)
if(WIRELINK_TEST_CPP)
  enable_language(CXX)
endif()
find_package(Wirelink CONFIG REQUIRED COMPONENTS astrial_usb)
""")
for name, cpp, diagnostic in [
    ('backend', 'ON', 'Supply astrial_usb or WirelinkBackend::astrial_usb'),
    ('cxx', 'OFF', 'requires CXX in project LANGUAGES'),
]:
    result = subprocess.run([cmake, '-S', source, '-B', binary + '/' + name,
                             '-DCMAKE_PREFIX_PATH=' + prefix, '-DWIRELINK_TEST_CPP=' + cpp],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pathlib.Path(binary, name + '.log').write_text(result.stdout)
    assert result.returncode != 0 and diagnostic in result.stdout, result.stdout
print('PASS: explicit missing-backend and missing-CXX component diagnostics')
