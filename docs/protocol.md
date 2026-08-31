# Wirelink Protocol Specification

Status: **v1 release candidate**. Numeric assignments and normative behavior
are frozen by the [v1 conformance vectors](conformance-v1.md). Any incompatible
wire-format change requires a new protocol version; clarifications that do not
change accepted or emitted bytes may still be made before the 1.0 release.

## 1. Scope

Wirelink is a small peer-to-peer link protocol for carrying application
messages over byte streams, packet transports, and master-driven peripheral
buses. It provides:

- fire-and-forget application messages;
- stop-and-wait reliable transactions with acknowledgement and retransmission;
- corruption detection when the selected transport does not provide adequate
  end-to-end integrity;
- stream framing and resynchronization;
- duplicate suppression for reliable receives; and
- a platform-independent, allocation-free C implementation.

Wirelink does not access hardware, own a clock, create a thread, or allocate
memory. A platform adapter moves transmission units to and from UART, USB, SPI,
I2C, UDP, or another transport. The application supplies current monotonic time
to the protocol engine.

Each Wirelink context represents exactly one logical peer. Multipoint
addressing, bus arbitration, routing, and peer discovery are adapter or future
protocol functions, not v1 header fields.

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**,
and **MAY** are to be interpreted as normative requirements.

## 2. Terminology

- **Application message**: a message ID and its opaque payload, normally
  encoded or decoded by WLC-generated code.
- **Packet**: the Wirelink base header, optional header extension, payload, and
  optional integrity trailer before transport framing.
- **Envelope**: the transport-facing representation of a packet, such as COBS
  plus a delimiter or a length-prefixed bus slot.
- **Transmission unit**: one complete envelope passed to a sink in one submit
  operation.
- **Link profile**: the envelope, integrity mode, MTU, and limits configured for
  a context.
- **Reliable message**: a DATA packet that requires a Wirelink ACK and may be
  retransmitted.
- **Unreliable message**: a DATA packet for which Wirelink performs no ACK or
  retransmission. This is fire-and-forget, not a guarantee of loss or
  duplication behavior in the underlying transport.
- **Session ID**: a sender boot/session epoch used with a sequence number to
  distinguish a new sender instance from delayed packets from an old one.

## 3. Architecture and boundaries

Wirelink separates three concerns:

1. WLC-generated codecs convert typed application objects to and from payload
   bytes. Payload schema and codec rules are defined by
   [`docs/schema-v1.md`](schema-v1.md).
2. The Wirelink core adds packet format, integrity, duplicate suppression, ACK,
   timeout, and retransmission behavior.
3. A platform adapter maps transmission units to hardware or an operating
   system transport.

An ACK means that a valid reliable message has been accepted into stable
Wirelink event storage on the receiving peer. It does **not** mean that the
application executed the command successfully. Commands requiring an
application result MUST define an explicit WLC response message.

Link-level duplicate suppression is bounded and normally volatile. It cannot
guarantee exactly-once execution across power loss, state loss, or session-ID
collision. Safety-critical commands SHOULD be idempotent or carry a persistent
application operation ID.

Typed routing, latest-value mailboxes, request/response correlation, and bulk
object transfer are application-layer facilities. Their common ownership and
semantic contract is defined in
[`docs/application-layer.md`](application-layer.md); none adds fields to the
v1 packet header.

## 4. Link profile

Both peers MUST be configured with a compatible link profile before exchanging
packets. V1 does not negotiate profiles on the wire.

A profile contains at least:

```text
envelope              COBS_STREAM | NATIVE_PACKET | BUS_LENGTH_16
integrity             NONE | CRC16_CCITT_FALSE | CRC32C
max_packet_size       maximum decoded Wirelink packet bytes
max_payload_size      maximum application payload bytes
max_transmission_unit maximum bytes accepted by the transport adapter
```

Integrity mode is a connection/profile property, not a per-packet option. A
sender MUST NOT silently remove a trailer based on packet type. A receiver MUST
parse every packet using the configured mode. This avoids ambiguous lengths
and prevents a corrupted per-frame option from accidentally disabling the
integrity check.

Configuration APIs MUST reject combinations whose worst-case encoded packet
cannot fit the configured storage or transport MTU.

### 4.1 Recommended profiles

| Transport | Envelope | Integrity | Notes |
| --- | --- | --- | --- |
| UART, RS-232, RS-485 | `COBS_STREAM` | `CRC32C` | Arbitrary byte chunks, insertion/deletion, and line noise require delimiter resync and an integrity check. CRC-16 is acceptable for very small constrained frames. |
| USB CDC ACM | `COBS_STREAM` | `CRC32C` recommended; `NONE` permitted | CDC exposes a byte stream; USB transfer boundaries are not application-message boundaries. USB CRC is hop-local. |
| USB vendor bulk (v1 product profile) | `COBS_STREAM` | `NONE` | The terminating delimiter is transported with each application unit. USB already supplies link integrity and retry, so this profile avoids a redundant Wirelink CRC. |
| UDP/IPv6 (v1 product profile) | `COBS_STREAM` | `NONE` | One complete delimiter-terminated unit is carried per datagram. IPv6 requires the UDP checksum; keep the unit below the path MTU. |
| UDP/IPv4 (v1 product profile) | `COBS_STREAM` | `NONE` | One complete delimiter-terminated unit is carried per datagram. Deployments MUST enable and verify the UDP checksum. |
| Raw SPI | `BUS_LENGTH_16` or `NATIVE_PACKET` | `CRC32C` | SPI has no standard integrity protection or universal transaction framing. |
| I2C/I3C | `NATIVE_PACKET` or `BUS_LENGTH_16` | `CRC32C` | I2C ACK/NACK acknowledges bytes/addresses; it is not a payload integrity code. SMBus PEC may justify a deployment-specific profile, although its CRC-8 is weaker. |
| CAN/CAN FD through ISO-TP | `NATIVE_PACKET` | `NONE` permitted; `CRC32C` optional | ISO-TP or the adapter performs segmentation. CAN already has per-frame CRC, but it is not an authenticated end-to-end check. |
| TCP, Unix stream, pipe | `COBS_STREAM` | `NONE` permitted; `CRC32C` optional | These are byte streams. Wirelink ARQ may be unnecessary but can still represent peer acceptance rather than only transport delivery. |

The v1 USB vendor-bulk and UDP profiles use `NONE` to avoid duplicating the USB
link integrity or UDP checksum in both CPU work and packet size. UDP/IPv4
products using this profile MUST keep UDP checksums enabled. UART, raw buses,
or gateways whose complete path lacks an integrity check continue to use a
Wirelink CRC. None of these checks authenticate a peer or payload.

UART parity is not a replacement for a packet CRC. It detects only a subset of
errors in individual characters and does not protect packet length, ordering,
or multi-bit corruption.

## 5. Packet format

All multibyte integer fields use network byte order (most significant byte
first). Implementations MUST encode fields individually and MUST NOT transmit a
C structure representation.

### 5.1 V1 compact header

```text
 Offset  Size  Field
 ------  ----  -------------------------------------------------
 0       1     marker/version/kind  0b1010_vv_kk; vv=01 for v1
 1       1     reserved             zero in v1
 2       2     message_id           network byte order
 4       8     session_id           reliable DATA and ACK only
 12      4     sequence             reliable DATA and ACK only
 4/16    ...   payload
 ...     0/2/4 integrity trailer selected by the link profile
```

The upper nibble of byte 0 is the marker `0xA`; bits 3-2 carry the version and
bits 1-0 carry the packet kind:

| Kind | Meaning | Header bytes |
| ---: | --- | ---: |
| `0` | Unreliable DATA | 4 |
| `1` | Reliable DATA | 16 |
| `2` | ACK | 16 |
| `3` | Reserved | — |

Payload length is inferred from the decoded transmission-unit boundary after
subtracting the kind-selected header and configured integrity trailer. It is
not repeated in the packet. Byte 1 is reserved for a future header variant and
MUST be zero in v1.

`session_id` zero is reserved in reliable DATA and ACK. Unreliable DATA omits
both session and sequence. `message_id` zero is reserved for protocol control
packets.

### 5.2 Logical packet types

| Value | Name | Meaning |
| ---: | --- | --- |
| `0x01` | `DATA` | Carries an application message; kind selects reliability. |
| `0x02` | `ACK` | Acknowledges one reliable DATA packet. |
| `0x03` | `NACK` | Reserved in the API; no v1 packet kind is assigned. |

Unknown standard packet types MUST be discarded. A receiver MUST NOT send an
ACK for an unknown packet type.

### 5.3 Flags

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `RELIABLE` | DATA requires ACK and duplicate suppression. |
| 1-7 | — | Reserved; MUST be zero in v1. |

The flag is represented by the DATA kind rather than a separate wire byte. A
receiver rejects reserved kinds and a nonzero reserved byte. Integrity remains
a profile property rather than a per-packet flag.

### 5.4 DATA packets

For reliable DATA:

- `session_id` identifies the sender's current session;
- `sequence` identifies this DATA packet within that session;
- `message_id` MUST be nonzero;
- payload length comes from the transmission-unit boundary; and
- kind `1` selects reliable behavior.

Unreliable DATA uses kind `0`, omits `session_id` and `sequence`, and therefore
has a four-byte aligned payload start. It is fire-and-forget and does not
participate in duplicate suppression.

Sequence numbers are assigned monotonically to reliable DATA packets. A sender MUST NOT
reuse `(session_id, sequence)`. Before a 32-bit sequence wraps, it MUST start a
new nonzero session ID or stop sending and report exhaustion.

V1 carries one complete application message in one DATA packet. Protocol-level
fragmentation is not defined. An adapter MAY use a lossless lower-layer
segmentation protocol, such as ISO-TP, as long as it reconstructs exactly one
complete Wirelink transmission unit before calling the unit-feed API.

### 5.5 ACK packets

For ACK:

- `session_id` and `sequence` copy the corresponding fields of the DATA being
  acknowledged;
- `message_id` MUST be zero;
- no payload bytes may follow the 16-byte header; and
- the configured integrity trailer applies exactly as it does to DATA.

Because a context represents one peer, the ACK does not carry a separate ACK
sender identity. A sender accepts an ACK only when it matches an active reliable
transaction's `(session_id, sequence)` and passes every normal packet and
integrity check. Unmatched and duplicate ACKs are silently ignored and counted.

## 6. Integrity trailers

The integrity calculation covers every decoded packet byte from the compact
marker/version/kind byte
through the final payload byte. The trailer itself and any transport envelope
bytes are excluded. The transmitted trailer is in network byte order.

### 6.1 NONE

No integrity trailer is appended. The receiver still validates marker,
version, kind, the reserved byte, message ID, kind-selected header fields, and
the transport-supplied transmission-unit boundary.

`NONE` MUST NOT be used on raw UART, RS-232, RS-485, SPI, or I2C unless another
layer supplies an explicitly assessed integrity check for the complete
Wirelink packet.

### 6.2 CRC-16/CCITT-FALSE

```text
width       16
polynomial  0x1021
init        0xffff
reflect-in  false
reflect-out false
xor-out     0x0000
check("123456789") = 0x29b1
```

CRC-16 reduces overhead on small, low-bandwidth links. Deployments with large
packets, high traffic volume, or industrial safety implications SHOULD use
CRC32C.

### 6.3 CRC32C (Castagnoli)

```text
width       32
polynomial  0x1edc6f41
init        0xffffffff
reflect-in  true
reflect-out true
xor-out     0xffffffff
check("123456789") = 0xe3069283
```

The reflected implementation commonly uses polynomial `0x82f63b78`.

A failed CRC causes the complete packet to be discarded. It MUST NOT produce
an application event or ACK.

CRC detects accidental corruption. It is not a message authentication code and
provides no protection against a malicious peer, spoofed ACK, replay, or
intentional modification.

## 7. Transport envelopes

### 7.1 COBS stream

`COBS_STREAM` is used when reads do not preserve message boundaries. The
transmission unit is:

```text
COBS(packet) || 0x00
```

The COBS input includes the integrity trailer. The delimiter is not included in
the COBS data or integrity calculation.

Receivers MUST support arbitrary feed chunking: one byte per call, multiple
frames per call, or a delimiter split from its frame. Empty delimiter-separated
segments MAY be ignored. If an encoded frame exceeds configured storage, the
receiver enters resynchronization mode and discards bytes through the next
`0x00` delimiter.

COBS restores frame boundaries after insertion, deletion, truncation, and
buffer overflow. It does not detect corruption; the integrity trailer performs
that job.

### 7.2 Native packet

`NATIVE_PACKET` transmits the decoded packet without extra envelope bytes. One
sink submission MUST map to exactly one datagram or preserved transport
transaction, and one unit-feed call MUST contain exactly one packet.

Every byte after the kind-selected header belongs to the payload (apart from a
configured integrity trailer), so the adapter's unit boundary is authoritative.
UDP adapters MUST preserve datagram boundaries and MUST NOT concatenate
datagrams merely because one socket read returned several internal buffers.

For UDP, a conservative default `max_transmission_unit` of 1200 bytes avoids IP
fragmentation on common paths. A deployment MAY choose a larger value only when
its path MTU and fragmentation behavior are known. Wirelink v1 itself does not
perform path-MTU discovery.

### 7.3 16-bit bus length prefix

`BUS_LENGTH_16` is intended for bounded SPI/I2C transactions and fixed-size bus
slots. The transmission unit is:

```text
packet_length_be16 || packet || optional padding
```

`packet_length` is the decoded packet length including its integrity trailer
and excluding the two-byte prefix. It MUST be nonzero and no greater than the
profile maximum. Padding is permitted only when the platform adapter has an
out-of-band transaction or fixed-slot length. Padding bytes are not part of the
packet and their value is unspecified.

The entire length-prefixed unit MUST be delivered atomically through the
unit-feed API. This envelope is not a self-resynchronizing unbounded byte-stream
format. A continuous SPI stream without reliable chip-select/slot boundaries
SHOULD use `COBS_STREAM` instead.

The length prefix supplies the packet boundary from which payload length is
derived. When CRC is enabled, a corrupted prefix cannot make a corrupt packet
valid, but adapters MUST apply bounds before using the prefix to copy or clock
data.

## 8. Reliable transaction behavior

V1 uses stop-and-wait ARQ per context. The initial implementation permits one
outgoing reliable DATA transaction at a time. The API may expose handles and
multiple storage slots so a future version can queue or pipeline transactions.

### 8.1 Sender states

An outgoing reliable transaction is implemented as follows in this revision:

```text
IDLE -> SENDING -> WAITING_ACK -> SUCCESS
             |             |
             +-------------+----------> FAILED
             +----------------------------------> CANCELLED
```

- `SENDING`: the sink accepted an asynchronous submission.
- `WAITING_ACK`: local transmission completed; the peer ACK is pending.
- `SUCCESS`: a valid matching ACK was received.
- `FAILED`: local I/O policy or retry exhaustion ended the transaction.
- `CANCELLED`: the application cancelled it.

A sink submission that reports temporary busy does not consume a transmission
attempt. The core retains that single application unit in its TX slot, reports
success from `wl_send_unreliable()` or `wl_send_reliable()`, and retries the
submission from a later `wl_poll()`. While the unit is queued, another
application send returns `WL_ERR_BUSY`. `WL_SINK_BUSY` therefore means
temporary adapter backpressure, not that the caller must resubmit the message.

An asynchronous attempt starts its ACK/retry timing only after the adapter
reports successful local completion. A synchronous sink starts timing when its
submit callback returns `SENT`.

Timeout and retry values are local policy and are not sent on the wire. Time
comparisons MUST remain correct across unsigned 32-bit millisecond wrap:

```c
(uint32_t)(now_ms - started_at) >= interval_ms
```

An `ack_timeout_ms` of zero disables the ACK timeout, so no timeout-driven
retry or failure occurs. Non-zero configured intervals MUST be less than
`2^31` milliseconds.

#### 8.1.1 Consumer scheduling hint

The C API exposes `wl_poll_get_hint(ctx, now_ms, &hint)` so a bare-metal loop,
RTOS task, or host event loop can sleep without periodically calling
`wl_poll()`. The query is side-effect free and reports two independent values:

- `work_pending` is one when `wl_poll()` can make consumer-side progress
  without another external I/O notification; and
- `next_deadline_ms` is the relative delay until the next ACK/retry deadline,
  or `WL_POLL_NO_DEADLINE_MS` when no timed deadline is active.

A deadline that is already due has delay zero and is also immediate work.
Relative-delay calculation uses the same unsigned subtraction as timeout
processing, so it remains correct across the 32-bit millisecond wrap. The
caller MUST pass values from the same monotonic clock domain to both the query
and `wl_poll()`.

The immediate bit covers queued events, complete COBS units, RX overflow
recovery, committed native units, retries, and retry exhaustion. It does not
claim that partial stream bytes form work: their adapter must wake the consumer
when more bytes arrive. While an RX event is leased, later RX units are also
not immediate until `wl_event_release()` makes their storage consumable.

`WL_SINK_BUSY` means an attempted control or DATA submission is waiting for
external transport progress. Such a queue does not continuously assert
`work_pending`; otherwise an event loop would retry the same busy sink without
bound. An adapter that can become writable asynchronously MUST wake the
consumer on that activity. The consumer calls `wl_poll()` once after the wake,
then obtains a new hint. TX completion follows the same pattern: the adapter
wakes its consumer, forwards completion with `wl_tx_complete()`, and queries
again. The core deliberately owns no scheduling or wait primitive.

### 8.2 Receiver acceptance and ACK

After receiving a valid reliable DATA packet, the receiver MUST reserve stable
event storage before considering it accepted. Once accepted it:

1. records `(session_id, sequence)` in its bounded deduplication state;
2. queues a corresponding ACK with control-traffic priority; and
3. exposes the application event exactly once.

If no event storage is available, the receiver MUST NOT ACK the packet. The
sender can retry later. A future NACK/BUSY control packet may optimize this
case, but it is not required by v1.

When a duplicate of an accepted reliable DATA packet arrives, the receiver
MUST queue the ACK again and MUST NOT expose a second application event. This
handles an ACK lost after the first delivery.

ACK/control traffic SHOULD be scheduled ahead of ordinary application DATA,
while preventing permanent application starvation.

### 8.3 Cancellation

Cancelling a transaction stops future submissions and retransmissions. If a
hardware transmission is already in progress, Wirelink cannot necessarily
abort it; the adapter may complete it normally and Wirelink ignores a later ACK
for the cancelled transaction. Cancellation therefore does not retract bytes
already handed to the transport.

### 8.4 Delivery guarantees

With bounded retries, Wirelink offers attempted at-least-once transport and
deduplicated application delivery within the receiver's active session/dedup
window. It does not promise:

- delivery after retry exhaustion;
- durable exactly-once execution;
- ordering relative to unreliable messages;
- authentication of the sender; or
- protection from a peer restart that reuses a session ID.

## 9. Session IDs and restart behavior

Every sender session MUST use a nonzero 64-bit ID that is unlikely to repeat for
the same logical peer. Recommended sources, in order, are:

1. a persistent monotonic boot counter combined with a stable device value;
2. a cryptographically strong random value generated once per boot; or
3. a deployment-provisioned epoch updated before communication begins.

A raw uptime value alone is not sufficient because it commonly repeats after a
restart. The core MUST accept a caller-provided session ID and MUST NOT require
an RNG, filesystem, or nonvolatile driver.

On observing a new valid peer session ID, a receiver treats sequence numbers as
belonging to that new session. Implementations SHOULD retain a small bounded set
of recently seen session/sequence entries so a delayed packet from the previous
session is not immediately redelivered. Applications requiring replay
protection across restart need persistent state or an authenticated higher
layer.

## 10. Platform adapter contract

The core calls a sink with a pointer, length, opaque I/O token, and user data.
The sink result has four meanings:

- `SENT`: bytes were consumed synchronously; the pointer may be reused after
  callback return.
- `STARTED`: asynchronous I/O owns access to the bytes until the adapter reports
  completion with the same token.
- `BUSY`: no ownership was taken; Wirelink retains and retries according to its
  scheduler.
- `FAILED`: no ownership was taken and this submission definitively failed.

For `STARTED`, the sink MUST NOT modify the bytes or retain the pointer after
completion. Wirelink MUST keep them unchanged until completion. Adapter
completion indicates local I/O completion, not remote delivery; reliable remote
delivery requires ACK.

Unless a future API explicitly states otherwise, Wirelink calls are not
reentrant and not thread/ISR safe. A sink MUST NOT call back into the same
context before returning. An adapter must serialize access or use
platform-specific critical sections outside the core.

### 10.1 UART and RS-485

- RX interrupts or DMA may accumulate bytes in an adapter-owned SPSC/ring
  buffer; the main loop feeds chunks to Wirelink.
- An adapter may feed directly only if it guarantees serialization with poll
  and TX-completion calls.
- RS-485 driver-enable timing, turnaround delay, collision avoidance, node
  addressing, and bus arbitration remain adapter responsibilities.
- Half-duplex sink busy periods are normal and MUST NOT be interpreted as link
  failure.

### 10.2 USB

- CDC ACM is always treated as a byte stream even when a host API returns USB
  transfer-sized chunks.
- A native-packet USB profile is valid only when both adapters intentionally
  preserve one submission as one complete logical transfer.
- Endpoint stall, disconnect, suspend, and reset map to adapter busy/failure and
  session lifecycle policy; they are not encoded as DATA payloads.

### 10.3 UDP

- One context binds to one logical remote endpoint. A server with multiple
  remotes maintains separate contexts or demultiplexes peers before feeding.
- One sink call produces one UDP datagram; one received datagram produces one
  unit-feed call.
- Source-address validation is required in the adapter. UDP acceptance alone
  is not peer authentication.
- Packet loss, duplication, and reordering are expected. Reliable Wirelink DATA
  handles loss and duplicates; stop-and-wait plus ACK matching handles stale
  reordering.
- Broadcast/multicast MUST use unreliable DATA. ACK-based reliable multicast
  is not defined.

### 10.4 SPI

SPI defines electrical transfer, not a universal packet protocol. The adapter
must define:

- chip-select boundary or fixed slot size;
- which side is clock master;
- how a slave advertises pending TX data, such as a READY GPIO or master poll;
- idle/fill-byte handling;
- maximum transfer size and DMA alignment; and
- half/full-duplex ownership and collision rules.

A slave sink may return `STARTED` when it queues a unit and report completion
only after the master clocks it out. Fill bytes received during unrelated
full-duplex clocks MUST be removed by the adapter, not fed as native packets.

### 10.5 I2C, I3C, and SMBus

These buses are controller-driven. A target cannot transmit until addressed,
so its adapter queues sink units and exposes them during controller reads.
Register addressing, READY/interrupt GPIO, polling commands, clock stretching,
arbitration loss, and repeated-start behavior are adapter policy.

One bus transaction may map to one native unit. If a device/register protocol
allows fixed slots or combined reads, `BUS_LENGTH_16` identifies the used
portion. The adapter MUST strip register-selection bytes before feeding
Wirelink.

### 10.6 CAN and other small-MTU packet buses

A complete v1 Wirelink packet generally does not fit a classic CAN frame. The
adapter MUST use a lower-layer segmentation/reassembly protocol such as ISO-TP,
or the application must constrain packet size to the bus MTU. Partial segments
MUST NOT be passed to `wl_feed_unit()`.

## 11. Flow control, buffers, and backpressure

All storage is bounded. Exhaustion is observable and must not silently corrupt
state.

- Byte-stream feed reports how many input bytes were accepted.
- Unit feed is atomic: it accepts the complete unit or returns an error.
- An unreliable send may return would-block/queue-full; success does not imply
  peer receipt.
- A reliable start fails if no transaction/retransmission slot is available.
- A reliable receive is not ACKed until event storage is secured.
- Oversize stream frames are discarded through the next delimiter.
- Oversize native/bus units are rejected as a whole.

The configuration API SHOULD provide helpers that calculate required RX, TX,
event, and transaction storage from maximum payload and queue counts.

## 12. Error handling and observability

Local API errors are not wire values. They describe invalid arguments,
uninitialized state, storage exhaustion, would-block, bad frames, unsupported
versions, local I/O errors, and similar conditions.

Asynchronous reliable completion uses a transaction result/event rather than
returning timeout from an unrelated poll call. Implementations SHOULD expose
monotonic diagnostic counters at least for:

- valid DATA and ACK packets;
- unreliable and reliable application deliveries;
- TX attempts and retransmissions;
- duplicate reliable DATA and unmatched ACK;
- malformed, unsupported-version, and oversize packets;
- COBS failures and integrity failures;
- stream overflows/resynchronizations;
- sink busy and local I/O failures; and
- reliable success, timeout, and cancellation.

Malformed input is expected on stream and network boundaries and SHOULD result
in bounded work, a counter update, and safe discard rather than context
corruption.

Recommended local error mapping for protocol constraints:

- `WL_ERR_BAD_FRAME`: 可靠包的零 `session_id`、非法 marker/kind、非零保留字节、
  ACK 的 `message_id != 0` 或带 payload 等；
- `WL_ERR_PAYLOAD_TOO_LONG`: 已解码长度或运行时 payload 超过 `max_payload_len`；
- `WL_ERR_WOULD_BLOCK`/`WL_ERR_BUSY`: 当单槽 TX 单元已排队、正在本地发送、
  或等待 ACK 时又发起发送；
- `WL_ERR_QUEUE_FULL`: 已有待处理事件时无法接受新的接收事件（包括可恢复可靠 DATA）。

## 13. Security considerations

Wirelink v1 is not a secure transport. CRC and COBS do not provide identity,
confidentiality, replay prevention, or tamper resistance. In particular, an
attacker able to inject packets can forge DATA and ACK packets and can force
application actions or false delivery success.

Deployments with an untrusted link MUST use a secured lower layer such as DTLS,
TLS, an authenticated serial tunnel, or a future Wirelink authenticated
profile. Session IDs are uniqueness values, not secrets or authentication
tokens. Parsers must validate all arithmetic before allocation/copy, cap work by
configured sizes, and never trust length fields before comparing them with the
actual unit.

## 14. Initial conformance requirements

A conforming v1 implementation must be tested for:

- header and CRC golden vectors on little- and big-endian hosts;
- empty, maximum, zero-rich, and 254-byte COBS boundaries;
- arbitrary stream chunking and multiple frames per feed;
- truncation, concatenation, corruption, unknown flags/types, and bad lengths;
- overflow followed by delimiter resynchronization;
- unreliable delivery;
- reliable ACK success, DATA loss, ACK loss, duplicate DATA, and stale ACK;
- sink busy, async completion, and local failure;
- retry exhaustion, cancellation, event backpressure, and stale handles;
- time and sequence wrap boundaries; and
- every supported envelope/integrity profile combination.

Published conformance vectors will become normative before v1 is declared
stable.
