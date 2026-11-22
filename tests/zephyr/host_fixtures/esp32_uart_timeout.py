#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the patched driver configure path with real HAL timeout arithmetic.

The register type comes from the selected ESP32-S3 HAL. Only peripheral I/O is
mocked; the driver and HAL functions under test are extracted from their sources.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zephyr-base", type=Path, required=True)
    parser.add_argument("--hal", type=Path)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    hal = args.hal or args.zephyr_base.parent / "modules/hal/espressif"
    driver = (args.zephyr_base / "drivers/serial/uart_esp32.c").read_text()
    hal_source = (hal / "components/esp_hal_uart/uart_hal.c").read_text()
    register = hal / "components/soc/esp32s3/register/soc/uart_struct.h"
    fixture = Path(__file__).with_name("esp32_uart_timeout_fixture.c").read_text()
    source = f'#include "{register.resolve()}"\n' + fixture.split("/* DRIVER */")[0]
    for signature in ("uint8_t uart_hal_get_symb_len(", "void uart_hal_set_rx_timeout(",
                      "uint16_t uart_hal_get_max_rx_timeout_thrd("):
        source += function(hal_source, signature)
    for signature in ("static int uart_esp32_validate_rx_timeout(",
                      "static int uart_esp32_configure("):
        source += function(driver, signature)
    source += fixture.split("/* DRIVER */")[1]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        path = root / "test.c"
        path.write_text(source)
        for timeout in (2, 22, 85, 86, 102, 103, 126):
            binary = root / f"timeout-{timeout}"
            subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wpedantic", f"-DCONFIG_UART_ESP32_RX_TIMEOUT={timeout}",
                            str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
