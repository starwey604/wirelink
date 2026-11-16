# CI source and release contract (0.14 candidate)

The local candidate is Wirelink/WLC **0.14.0**, generated-code contract **0.14**,
with SDK binding API 3. This round is local-only: no commits, pushes, tags or
release workflows have been authorized or performed. `.github/release-pair.json`
still records the frozen **0.13.0 / 0.13** pair; it is not evidence of a qualified
0.14 pair. Remote matrix and release gates remain pending; the release pair/version
check remains unsatisfied until the new pair is frozen.

Once the 0.14 pair is frozen, `.github/release-pair.json` is the authoritative
WLC source pin, compiler version, codegen contract, binding API and Rust
toolchain. The setup-wlc action builds that exact independent commit and checks
the executable before exporting it. Until the pin is updated, local 0.14 checks
must explicitly use the locally built matching WLC; setup-wlc still selects
0.13. Normal CI does not silently substitute a public compiler release.

The full matrix exercises core/owner/tutorials, installed C/C++ and UDP consumers,
Astrial adapters, forty SDK wheel combinations and two source distributions,
ASan/UBSan, libFuzzer and Zephyr native/QEMU. CPython 3.10–3.14 is the SDK runtime
matrix; benchmark tooling uses its separate Python 3.11+ interpreter.

Zephyr remains pinned to `ca10f7fc7a5b5b1129823cfc57506e81bb109743`; CI applies the
three reviewed platform patches after applicability checks. ESP32-S3 USB bulk is
a build qualification. Physical UART/USB I/O follows the adapter HIL records.
Astrial remains pinned to `7ffc5c34327d231e4bbca4fadb2157b8bba5a42d`.

## Freeze source once, record the pair in artifacts

When remote work is authorized, freeze in dependency order:

1. Freeze the Wirelink core implementing contract 0.14 as an immutable API
   baseline; retain its successful core/ABI checks.
2. Point WLC verification at that core baseline (update the qualified default
   or supply the explicit `wirelink_ref`), then verify and freeze the WLC 0.14
   commit. The old qualified default must not stand in for the new core API.
3. Record that exact WLC SHA, version 0.14.0 and contract 0.14 in Wirelink's
   release pair; freeze the integrated Wirelink candidate and run its full
   paired matrix. WLC release rehearsal uses that explicit core candidate.

Wirelink's own final SHA is supplied by the workflow at runtime, not embedded
in its pair file. Both artifact sets record the exact core/compiler SHAs, so
neither repository needs a circular reference to the other's final commit.
Updating a pin or passing local tests does not complete remote qualification.

A WLC rehearsal extracts each of the five platform archives, checks version,
codegen 0.14 and binding API 3, compares all checked-in generated artifacts, then
builds and runs single-file node RPC and installed-generator consumers against the
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
GitHub does not recursively launch another tag-push workflow. See
[development checks](../docs/development.md) and
[0.14 migration notes](../docs/release-v0.14.0-cn.md) for local gate details.
