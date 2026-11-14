# CI source and release contract (0.13)

`.github/release-pair.json` is the authoritative WLC source pin, compiler version,
codegen contract, SDK binding API and Rust toolchain for this Wirelink candidate.
The setup-wlc action reads that file, builds the exact independent commit and
checks the executable before exporting it. Normal CI uses source builds; it does
not silently substitute an older public release.

The full matrix exercises core/owner/tutorials, installed C/C++ and UDP consumers,
Astrial adapters, forty SDK wheel combinations and two source distributions,
ASan/UBSan, libFuzzer and Zephyr native/QEMU. CPython 3.10–3.14 is the SDK runtime
matrix; benchmark tooling uses its separate Python 3.11+ interpreter.

Zephyr remains pinned to `ca10f7fc7a5b5b1129823cfc57506e81bb109743`; CI applies the
three reviewed platform patches after applicability checks. ESP32-S3 USB bulk is
a build qualification. Physical UART/USB I/O follows the adapter HIL records.
Astrial remains pinned to `7ffc5c34327d231e4bbca4fadb2157b8bba5a42d`.

## Freeze source once, record the pair in artifacts

WLC's shared verification workflow has one qualified Wirelink API baseline. Its
release workflow also accepts an explicit immutable Wirelink candidate. Wirelink
pins the resulting WLC commit; its own commit is supplied by the workflow at
runtime. Neither repository embeds a circular reference to the other's final
commit.

A WLC rehearsal extracts each of the five platform archives, checks version,
codegen 0.13 and binding API 3, compares all checked-in generated artifacts, then
builds and runs profile-v2 RPC and installed-generator consumers against the
exact paired core. Its archives and `wlc-release.json` record both full SHAs.
Wirelink's rehearsal runs the full matrix and collects exactly forty supported
wheels, two manylinux-tested sdists and `wirelink-release.json` for the same pair.
Each complete bundle has `SHA256SUMS` and is retained as a CI artifact.

## Promote successful bundles, WLC first

Both Release workflows support explicit `dry_run` and `promote_run` inputs. A
rehearsal creates no tag or GitHub release. Promotion checks the original release
run's successful conclusion, source SHA, workflow identity, complete inventory,
source pair and checksums. It publishes the original bundle, without rebuilding.
Wirelink publication additionally requires the public WLC release metadata to
name this exact pair. Existing releases are accepted only when their assets are
identical; different assets are never overwritten.

Tag-push and existing-tag manual WLC releases remain supported. New tags can also
be created by verified promotion. The repository token creates those tags, so
GitHub does not recursively launch another tag-push workflow. Use the paired
[release procedure](../docs/releasing-cn.md) for rehearsal and promotion commands.
