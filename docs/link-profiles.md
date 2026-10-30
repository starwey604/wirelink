# Link build profiles

`multi` is the default. One process can initialize endpoints using COBS stream, native packets or length16, each with NONE, CRC16 or CRC32C integrity.

```sh
cmake -S . -B build/fixed -DWIRELINK_LINK_PROFILE=cobs-crc16
cmake --build build/fixed --parallel
```

Zephyr uses `CONFIG_WIRELINK_LINK_PROFILE_COBS_CRC16=y`; the default choice is `CONFIG_WIRELINK_LINK_PROFILE_MULTI=y`. The definition is private to every actual core target, including its internal framing dependency. Direct-source consumers default to multi unless they define `WL_LINK_PROFILE_COBS_CRC16=1` consistently for every core source.

The fixed profile requires COBS/CRC16 endpoint configuration. Generated COBS endpoints using `wl_link_defaults()` default to CRC16; the USB NONE preset is rejected; other generated envelope defaults still need an explicit COBS choice. Explicit CRC32C overrides are rejected. `wl_config_requirements`, `wl_init` and frame tools reject valid omitted modes with `WL_ERR_NOT_SUPPORTED`; invalid enum values return `WL_ERR_INVALID_ARG`. Frame size helpers return zero for unsupported modes. Existing argument/packet validation precedes capability rejection. No configuration is silently changed. Independent CRC and COBS tools keep their full utility API.

Installed packages report their capability through CMake `Wirelink_LINK_PROFILE` and `pkg-config --variable=link_profile wirelink`. Public structures, wire bytes and the 0.12 WLC contract are unchanged. Native-packet examples and the native-only loopback test are omitted for the fixed profile; its default framing diagnostic matrix contains only COBS/CRC16.

Both profiles use the same complete CRC16/COBS kernels for normal disjoint, spare-capacity TX and stream RX. Exact-capacity and overlapping TX use a shared bounded writer. RX publishes a view only after COBS, structure and integrity checks. Cached reliable images retain the same BUSY/retry/consumed/drained lifetime.

The common owner retains frame-level admission guards. In a fixed build the rejected branches contain only `WL_ERR_NOT_SUPPORTED` boundaries, with no alternate wire engine; the successful branches call the compile-time COBS/CRC16 kernels directly. There is no byte-level selector or callback, global mutable scratch, extra packet buffer or heap allocation.

Zephyr keeps the common framing/owner code with the core and links standalone/cold framing and optional modes through a private archive after it. Xtensa GCC uses `-mtext-section-literals` for these framing/owner units to retain literals with their text. Optimization level, LTO, cache size, vendor linker script and IRAM coverage are unchanged. Performance remains subject to target/layout verification; the Round 3 report records the measured configurations.
