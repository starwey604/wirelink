# CI source and execution contract (0.13)

The local setup-wlc composite action builds independent WLC
`7eabbf4dd767f968123917ba02431fa0a0ef184e` with Rust 1.96.1, verifies version
0.13.0/codegen 0.13 and exports its absolute executable. This commit must be
available in the independent remote before remote CI can fetch it. Updating
these workflows does not push that repository or imply a published compiler.
No fallback release download is used.

The host matrix explicitly enables current generated tests, C tutorials,
platform/storage, bounded owner and C++ Session. Installed consumers use
`wlc_current`, public C/C++ headers, fixed-width outcome tests and the current
UDP component. C11 atomic storage stays in C. ASan/UBSan and libFuzzer exercise
current execution; optional profiling is an application tool, not core hooks.
Benchmark parser tests run via CTest; argument-dependent model tests remain
registered with the real model build. Manual timing requires an explicit
compatible base revision, not an automatically selected pre-cut release.

Zephyr is pinned to `ca10f7fc7a5b5b1129823cfc57506e81bb109743`; CI applies the
three reviewed platform patches only after applicability checks. A local
isolated application of those patches reproduces all four modified platform
files byte-for-byte. UART/USB capability constraints remain explicit; a board
build does not qualify physical I/O.

Astrial CI pins `7ffc5c34327d231e4bbca4fadb2157b8bba5a42d`. Physical research
also records the pre-existing dirty Astrial tree separately. Controlled
adapter-boundary tests and a source pin alone do not qualify that dirty driver
implementation or physical hardware. No external Astrial/vendor edits are made.

The manylinux job builds a static musl WLC executable before entering the
older-glibc container and explicitly supplies that container path. Local
verification built the static executable and compared all checked-in SDK
artifacts. This is not a completed manylinux run or a wheel publication.

Remote Windows/macOS runners, the manylinux image and release/deployment jobs
are not executed merely by editing the workflow. Local evidence records the
Linux-equivalent product/install tests, source pins, patch comparison and YAML/
script syntax checks separately. Publishing/tags/deployment remain outside this
refactor execution.
