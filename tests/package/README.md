# Installed-contract checks

Configure the consumers against a fresh Wirelink installation with
`-DCMAKE_PREFIX_PATH=<prefix>`. Each project builds and runs with CMake and CTest.

| Project or setting | Coverage |
| --- | --- |
| `installed_core` | Installed C and C++20 public headers, wire operations and ABI types. CI also compiles the consumer with `-fshort-enums` against a normal-width library. |
| `installed_core -DWIRELINK_CONSUMER_C_ONLY=ON` | C11 CMake and pkg-config consumers in a C-only project. Add the installed pkgconfig directory to `PKG_CONFIG_PATH`. |
| `installed_core -DWIRELINK_WLC_EXECUTABLE=<wlc>` | Generated native and COBS generated nodes, C++ calls, keyword modules, node selection and generator diagnostics. |
| `installed_core -DWIRELINK_CONSUMER_HOST=ON` | Installed Executor and C/C++ UDP consumers. Supplying WLC also runs the installed C++ Session with a generated node. |
| `installed_core -DWIRELINK_TEST_ASTRIAL_SOURCE_DIR=<source>` | Installed Astrial USB and serial components, controlled transfers and missing-backend diagnostics. Install Wirelink with these adapters enabled first. |
| `calculator_consumer`, `device_consumer` | Installed SDK headers and libraries. Install both example SDKs into the prefix before running the device consumer. |

The bindings workflow builds and tests wheels against independent C peers.
Zephyr patch checks and their host fixtures live in `tests/zephyr/host_fixtures/`.
