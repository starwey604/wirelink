# Wirelink 协议规范（中文审阅版）

状态：v1 release candidate。数字分配和规范行为由
[`conformance-v1.md`](conformance-v1.md) 冻结。不兼容 wire-format 修改必须使用新协议
版本；1.0 前仍可澄清不改变收发字节的文字。英文版 [`protocol.md`](protocol.md) 是唯一
规范来源；本译文中的“必须/不得/应/可以”对应 MUST/MUST NOT/SHOULD/MAY。

## 1. 范围与术语

Wirelink 是在 byte stream、packet transport 和 master-driven bus 上承载应用消息的小型
peer-to-peer link protocol，提供 fire-and-forget、stop-and-wait reliable transaction、
可选 corruption detection、stream framing/resync、可靠 RX duplicate suppression 和跨平台
无动态分配 C 实现。

它不访问硬件、不拥有 clock、不创建 thread、不分配内存；adapter 负责 UART/USB/SPI/
I2C/UDP 等 transmission unit，应用传入 monotonic time。每个 context 恰好代表一个 logical
peer；multipoint addressing、arbitration、routing、discovery 不在 v1 header。

packet 是 base header、可选 extension、payload、integrity trailer；envelope 是 transport
表示；transmission unit 是一次 sink submit 的完整 envelope；link profile 是 envelope、
integrity、MTU 和 limit。reliable DATA 需要 ACK 且可重传；unreliable DATA 不做 ACK/重传。
session ID 与 sequence 一起区分新 sender 实例和旧延迟包。

## 2. 架构边界

WLC codec 处理 typed payload；core 处理 packet/integrity/dedup/ACK/timeout/retry；adapter
映射平台 transport。ACK 只证明可靠消息进入 peer 的稳定 event storage，不表示 command
成功。需要应用结果的 command 必须有显式 WLC response。link dedup 有界且通常易失；关键
command 应幂等或携带持久 operation ID。typed routing、mailbox、RPC、bulk 都不修改 v1
header。

## 3. Link Profile

双方交换前必须带外配置兼容 profile；v1 不在线协商：

```text
envelope              COBS_STREAM | NATIVE_PACKET | BUS_LENGTH_16
integrity             NONE | CRC16_CCITT_FALSE | CRC32C
max_packet_size       最大 decoded packet bytes
max_payload_size      最大 application payload bytes
max_transmission_unit transport adapter 最大接收 bytes
```

integrity 是 profile 属性，不是每 packet option。发送方不得按 packet type 静默去 trailer；
接收方必须始终按配置解析。config 必须拒绝最坏编码结果超 storage/MTU 的组合。

| Transport | Envelope | Integrity | 重点 |
| --- | --- | --- | --- |
| UART、RS-232、RS-485 | `COBS_STREAM` | `CRC32C` | 小帧可评估 CRC-16 |
| USB CDC ACM | `COBS_STREAM` | 建议 `CRC32C`，允许 `NONE` | USB transfer boundary 不是消息边界 |
| USB vendor bulk v1 profile | `COBS_STREAM` | `NONE` | 每个 unit 携带终止 delimiter |
| UDP/IPv6 v1 profile | `COBS_STREAM` | `NONE` | 每 datagram 一个完整 unit，控制 path MTU |
| UDP/IPv4 v1 profile | `COBS_STREAM` | `NONE` | 必须启用并验证 UDP checksum |
| Raw SPI | `BUS_LENGTH_16` 或 `NATIVE_PACKET` | `CRC32C` | SPI 无通用 framing/integrity |
| I2C/I3C | `NATIVE_PACKET` 或 `BUS_LENGTH_16` | `CRC32C` | bus ACK 不是 payload integrity |
| CAN/CAN FD + ISO-TP | `NATIVE_PACKET` | 可 `NONE`，可选 CRC32C | adapter/lower layer 分片 |
| TCP、Unix stream、pipe | `COBS_STREAM` | 可 `NONE`，可选 CRC32C | 都是 byte stream |

UART parity 不替代 packet CRC。任何 CRC 都不认证 peer。

## 4. Packet Format

多字节整数均为 network byte order；不得直接发送 C struct 表示。

```text
 Offset  Size  Field
 ------  ----  -------------------------------------------------
 0       1     marker/version/kind  0b1010_vv_kk; vv=01 for v1
 1       1     reserved             v1 必须为零
 2       2     message_id           network byte order
 4       8     session_id           仅 reliable DATA/ACK
 12      4     sequence             仅 reliable DATA/ACK
 4/16    ...   payload
 ...     0/2/4 profile 选择的 integrity trailer
```

byte 0 高 nibble 为 `0xA`，bits 3-2 是 version，bits 1-0 是 kind：0=unreliable DATA
（4-byte header），1=reliable DATA（16），2=ACK（16），3=reserved。payload length 从
transmission-unit boundary 减去 header/trailer 推出。byte 1 必须为零。可靠 DATA/ACK 的
`session_id` 不得为零；unreliable 不带 session/sequence；`message_id=0` 保留给 control。

logical DATA API value 为 `0x01`，ACK 为 `0x02`，NACK `0x03` 仅在 API 中保留、v1 无
对应 kind。未知 standard type 必须丢弃且不得 ACK。reliability 由 kind 表示；其他 flag
reserved。

可靠 DATA 的 message ID 非零；sequence 单调分配，发送方不得复用
`(session_id, sequence)`，32-bit sequence wrap 前必须换新非零 session 或停止发送。
v1 一包承载一个完整应用消息，不定义 protocol fragmentation；ISO-TP 等 lower layer 可以在
调用 unit-feed 前无损重组。

ACK 复制 DATA 的 session/sequence，message ID 为零、无 payload，并使用同一 integrity。
只有匹配 active transaction 且通过所有检查的 ACK 才接受；unmatched/duplicate ACK 静默
忽略并计数。

## 5. Integrity Trailer

校验覆盖 decoded packet 从 marker 到最后 payload byte，不含 trailer/envelope；trailer 大端。

- `NONE`：无 trailer，但仍验证 marker/version/kind/reserved/ID/header/boundary；若没有其他
  完整 packet integrity，不得用于 raw UART/RS-232/485/SPI/I2C。
- CRC-16/CCITT-FALSE：poly `0x1021`、init `0xffff`、不反射、xor `0`，
  `"123456789" = 0x29b1`。
- CRC32C：poly `0x1edc6f41`、init/xor `0xffffffff`、输入输出反射，
  `"123456789" = 0xe3069283`；反射实现常用 `0x82f63b78`。

CRC 失败必须整包丢弃，不产生 event 或 ACK。CRC 不是 MAC，不能防伪造、replay 或恶意修改。

## 6. Transport Envelope

### COBS Stream

```text
COBS(packet) || 0x00
```

必须支持任意 feed chunk、一次多 frame、delimiter 与 frame 分离。空 segment 可以忽略；
encoded frame 超 storage 后丢到下一个 `0x00` 以 resync。COBS 恢复 boundary，不检测 corruption。

### Native Packet

无额外 envelope。一次 sink submit 恰好对应一个 datagram/保边界 transaction；一次 unit feed
恰好一个 packet。adapter unit boundary 是权威；UDP 不得拼接 datagram。UDP 默认
`max_transmission_unit=1200`，只有确认 path MTU/fragment 行为后才可增大；v1 不做 PMTU
discovery。

### 16-bit Bus Length

```text
packet_length_be16 || packet || optional padding
```

length 包含 integrity、不含 2-byte prefix，必须非零且不超 profile maximum。padding 仅在
adapter 有带外 transaction/fixed-slot length 时允许。完整 unit 必须原子 feed；这不是无限
stream 的自同步格式。使用 length copy/clock 前 adapter 必须先 bound-check。

## 7. Reliable Transaction

v1 每 context 使用 stop-and-wait ARQ，当前一次只有一个 outgoing reliable DATA：

```text
IDLE -> SENDING -> WAITING_ACK -> SUCCESS
             |             |
             +-------------+----------> FAILED
             +----------------------------------> CANCELLED
```

sink 暂时 busy 不消费 attempt。core 保留单个 unit，首次 send 仍成功，之后由 `wl_poll()`
重试；期间其他 send 返回 `WL_ERR_BUSY`。异步 attempt 在 adapter 本地成功 completion 后才
开始 ACK timer，同步 sink 在返回 SENT 时开始。

timeout/retry 是本地策略，不上 wire。比较必须 wrap-safe：

```c
(uint32_t)(now_ms - started_at) >= interval_ms
```

`ack_timeout_ms=0` 禁用 timeout retry/failure；非零 interval 小于 `2^31` ms。

`wl_poll_get_hint()` 无副作用，分别返回 `work_pending` 和相对
`next_deadline_ms`；无 deadline 为 `WL_POLL_NO_DEADLINE_MS`，到期为零且算 immediate。
调用者必须对 hint/poll 使用同一 monotonic clock。hint 覆盖 queued event、完整 COBS、
overflow、committed unit、retry/exhaustion；partial stream 等待 adapter wake。RX event 被借用
时后续 RX 不算 immediate。sink busy 不持续拉高 work bit，adapter writable/completion 必须
wake owner，owner poll 一次后再查 hint，避免忙循环。

可靠 RX 必须先保留稳定 event storage，才记录 `(session,sequence)`、优先 queue ACK，并把
应用 event 暴露一次。无 event storage 不 ACK。duplicate 必须重发 ACK 但不得再次交付应用。
ACK/control 应优先于 DATA，但不得永久饿死应用。

cancel 停止后续 submit/retry，但不保证撤回已经交给硬件的 byte；之后 ACK 被忽略。
有界 retry 提供 attempted at-least-once transport 和 active dedup window 内单次应用 delivery，
不承诺 retry exhaustion 后送达、durable exactly-once、与 unreliable 的相对顺序、认证，或
复用 session ID 后的保护。

## 8. Session ID

每个 sender session 必须使用不易对同一 peer 重复的非零 64-bit ID。优先来源：持久单调
boot counter 加稳定 device value；每 boot 的强随机值；通信前更新的 deployment epoch。
仅 uptime 不够。core 只接受调用方 ID，不要求 RNG/filesystem/NV driver。

新 peer session 的 sequence 属于新 epoch。实现应保留少量近期 session/sequence，降低旧延迟
包立即重投递风险。跨重启 replay protection 需要持久状态或 authenticated higher layer。

## 9. Platform Adapter Contract

sink 结果：`SENT` 同步消费；`STARTED` 借用 byte 到相同 token completion；`BUSY` 不取得
ownership，core 稍后重试；`FAILED` 不取得 ownership 且本次终态失败。STARTED 后 adapter
不得修改 byte 或在 completion 后保留 pointer；completion 仅表示 local I/O。

除非 API 明示，Wirelink 不 reentrant、也不 thread/ISR safe；sink 不得在返回前回调同一
context。UART/RS-485 的 ring、DE timing/arbitration，USB stream boundary/stall，UDP remote
demux/source validation，SPI CS/master/READY/fill，I2C register/READY/clock stretch，以及 CAN
segmentation 都由 adapter 负责。multi-remote UDP 使用独立 context；broadcast/multicast 只能
unreliable；partial CAN segment 不得 feed 为 unit。

## 10. Flow Control 与可观测性

所有 storage 有界。stream feed 报告接受字节数；unit feed 原子接受/拒绝；unreliable send
成功不表示 peer 收到；每个已接受 unreliable 的本地 completion 保留为 event，capacity 在
调用 sink 前检查；无 reliable slot 时 start 失败；可靠 RX 取得 event storage 前不 ACK；
oversize stream 丢到 delimiter，oversize unit 整体拒绝。

local API error 不是 wire value。可靠异步完成通过 transaction result/event 报告。实现应提供
饱和 counter：valid DATA/ACK、delivery、attempt/retry、duplicate/unmatched ACK、malformed/
unsupported/oversize、COBS/integrity、overflow/resync、sink busy/I/O failure、reliable
success/timeout/cancel。畸形输入必须有界处理、计数、安全丢弃，不破坏 context。

## 11. 安全性与 Conformance

Wirelink v1 不是 secure transport。CRC/COBS 不提供 identity、confidentiality、replay
prevention 或 tamper resistance。非可信 link 必须置于 DTLS/TLS/authenticated serial tunnel
等安全层。session ID 是 uniqueness value，不是 secret/token。parser 必须先验证所有算术和
实际 unit length，再 copy。

v1 conformance 必须覆盖大小端 header/CRC golden vector、COBS 边界和任意 chunk、
truncation/concat/corruption/unknown/bad length、overflow resync、unreliable、可靠 ACK/
DATA loss/duplicate/stale ACK、sink busy/async/failure、retry exhaustion/cancel/backpressure/
stale handle、time/sequence wrap，以及所有支持的 envelope/integrity 组合。
