# Wirelink

[简体中文主页 (Simplified Chinese)](README.md) · [Documentation Index (中文)](docs/README-en.md)

**Documentation in English is coming soon.**

Wirelink is a strict ISO C11, zero-heap-allocation communication middleware for embedded MCUs (bare-metal, RTOS, Zephyr) and host tools over UART, USB Bulk, UDP, or SPI.

In the meantime, please read the Simplified Chinese documentation:

- [Main README (简体中文)](README.md)
- [Documentation Overview (`docs/README.md`)](docs/README.md)
- [Wire v2 Format Specification (`docs/wire-format.md`)](docs/wire-format.md)
- [Porting Transport Adapters (`docs/porting-adapters.md`)](docs/porting-adapters.md)
- [Development & Testing Guide (`docs/development.md`)](docs/development.md)

## Quick Local Build & Test

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
./build/core/examples/wirelink_bare_metal_loopback
```

[Apache-2.0 License](LICENSE)
