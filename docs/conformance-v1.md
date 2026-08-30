# Wirelink v1 conformance vectors

Status: **normative release-candidate vectors**.

The machine-readable vectors are defined in
[`tests/fixtures/conformance/v1_vectors.h`](../tests/fixtures/conformance/v1_vectors.h).
Every release claiming Wirelink v1 compatibility must reproduce their exact
bytes and accept them with the declared profile.

The canonical DATA input is:

| Field | Value |
| --- | --- |
| packet type | `DATA (0x01)` |
| session ID | `0x0102030405060708` |
| sequence | `0x0A0B0C0D` |
| message ID | `0x1234` |
| payload | `00 11 22 00 FF` |

The suite freezes that DATA packet for all nine envelope/integrity
combinations, plus reliable DATA, ACK in every envelope, and an empty DATA
packet. CRC16 is stored big-endian as `7C EE`; CRC32C is stored big-endian as
`A2 40 30 89` for the canonical unreliable DATA frame.

The rejection vectors freeze error classification for bad magic, unsupported
version, reserved flags, inconsistent payload length, corrupted CRC, and a
truncated frame. NACK remains reserved and unsupported in v1.

WLC payload compatibility vectors are maintained separately under
[`tests/fixtures/wlc`](../tests/fixtures/wlc); they validate the payload layer
selected by `message_id`, not the Wirelink frame itself.
