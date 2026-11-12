# Current-contract fuzz targets

Use Clang with libFuzzer and matching local WLC 0.13:

```sh
cmake -S . -B build/fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DWIRELINK_BUILD_EXAMPLES=OFF -DWIRELINK_BUILD_FUZZERS=ON \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc-0.13
cmake --build build/fuzz --parallel
ctest --test-dir build/fuzz -R fuzz --output-on-failure
```

The private fuzz library compiles the same production source files with coverage,
ASan and UBSan. It introduces no alternate engine or installed target. Current
pure codecs are generated into the build directory; frozen 0.12 conformance
codec files remain byte-identical and run in their independent unit suite.
No warning suppression is applied to the new code or generator output.

COBS and public frame fuzzers preserve independent byte/alias oracles, including
historical frame encodings supported by the standalone utility. The current
kernel fuzzer interleaves admission, bounded steps, deadlines/wrap, cancellation,
ordinary sends, arbitrary/looped RX, BUSY/failure and separate physical consumed,
drained/error events. It checks source immutability until consumption, at-most-once
terminal callbacks, fresh-instance reinitialization and close/quiescence. The
controlled driver really abandons its source before publishing cancellation.
It owns no call matching or deadline table outside the kernel.

The stream fuzzer independently selects COBS/CRC16 or length16/NONE, feeds partial
chunks, drains bounded packet units and aborts incomplete input only after its
single synchronous producer has stopped. Application object semantics run in
`tests/usb_sample` and the composed-service tutorial.

CTest performs 5000 bounded runs per target, not an exhaustive fuzz campaign.
For a longer campaign supply a writable corpus and retain seed, command,
compiler, crash artifacts and source/binary hashes outside the repository.
