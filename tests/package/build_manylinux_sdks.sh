#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Run inside quay.io/pypa/manylinux_2_28_x86_64 with this repository at /io.
set -euo pipefail
cd /io
export PATH="/opt/python/cp314-cp314/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=4
python -m pip install 'cmake>=3.21' build auditwheel pytest
cmake -S . -B build/manylinux-native -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/io/build/manylinux-install \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
  -DWIRELINK_BUILD_CPP_BINDINGS=ON -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_BUILD_EXAMPLES=OFF \
  -DWIRELINK_ASIO_INCLUDE_DIR=/io/.tools/asio/asio/include
cmake --build build/manylinux-native --parallel 4
ctest --test-dir build/manylinux-native --output-on-failure
cmake --install build/manylinux-native
for example in 06_bindings 07_device_sdk; do
  cmake -S "examples/$example" -B "build/manylinux-$example" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/io/build/manylinux-install \
    -DCMAKE_INSTALL_PREFIX=/io/build/manylinux-install
  cmake --build "build/manylinux-$example" --parallel 4
  ctest --test-dir "build/manylinux-$example" --output-on-failure
  cmake --install "build/manylinux-$example"
done
for consumer in calculator device; do
  cmake -S "tests/package/${consumer}_consumer" -B "build/manylinux-$consumer-consumer" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/io/build/manylinux-install
  cmake --build "build/manylinux-$consumer-consumer" --parallel 4
  ctest --test-dir "build/manylinux-$consumer-consumer" --output-on-failure
done
export WIRELINK_CALCULATOR_SERVER=/io/build/manylinux-06_bindings/tests/calculator_c_server
export WIRELINK_DEVICE_SERVER=/io/build/manylinux-07_device_sdk/tests/device_c_server
mkdir -p dist
for abi in cp310-cp310 cp311-cp311 cp312-cp312 cp313-cp313 cp314-cp314; do
  py="/opt/python/$abi/bin/python"
  "$py" -m pip install build pytest
  raw="build/manylinux-dist-$abi"
  for example in 06_bindings 07_device_sdk; do
    # The default build creates a wheel from the extracted sdist.
    "$py" -m build "examples/$example" --outdir "$raw" \
      -Ccmake.define.CMAKE_PREFIX_PATH=/io/build/manylinux-install
  done
  for wheel in "$raw"/*.whl; do
    auditwheel repair "$wheel" --plat manylinux_2_28_x86_64 --wheel-dir dist
  done
  cp "$raw"/*.tar.gz dist/
  "$py" -m pip install --no-deps --force-reinstall dist/*-"$abi"-*.whl
  "$py" -m pytest examples/06_bindings/tests examples/07_device_sdk/tests -q
  "$py" tests/package/check_sdk_wheels.py \
    --calculator-wheel dist/wirelink_calculator_sdk-*-"$abi"-*.whl \
    --device-wheel dist/wirelink_device_sdk-*-"$abi"-*.whl \
    --calculator-server "$WIRELINK_CALCULATOR_SERVER" \
    --device-server "$WIRELINK_DEVICE_SERVER"
done
