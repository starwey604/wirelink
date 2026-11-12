# 选择 connector 并规划存储

[English](link-profiles.md) · [Wire 格式](packet-protocol-cn.md) · [Adapter 绑定](adapters-cn.md)

USB bulk/UDP 选择 native packet，UART 字节流选择 COBS/CRC16；传输提供可信长度分隔流时选择 length16/NONE。profile-v2 endpoint 选择一种 connector，不执行 wire 协商。

| `endpoint.envelope` | 分帧 / 校验 | 适用边界 |
| --- | --- | --- |
| `native_packet` | native/NONE | 完整包 |
| `cobs_stream` | COBS/CRC16-CCITT-FALSE，零分隔符 | UART/serial 字节 |
| `bus_length16` | 两字节大端长度，NONE | 长度分隔字节 |

两端必须约定 connector、schema/消息 ID，以及每条交换 route 的 codec。send/direct route 集合可按应用角色不同，但共享 ID 的字节必须一致。CRC 检测损坏，不认证 peer；native/NONE 和 length16/NONE 不提供 connector checksum。

## 生成显式部署

telemetry 示例的 `telemetry.wl` 使用以下完整 profile：

```wl
profile version 2;
endpoint { envelope=native_packet; }

direct Telemetry {
  delivery = unreliable;
}

send Telemetry { delivery=unreliable; }
```

使用[匹配的本地 WLC](development-cn.md#构建匹配-wlc-和生成教程)，通过一个 `wirelink_wlc_generate(TARGET ... SCHEMA ... PROFILE ...)` CMake target 生成公开 codec、带类型 route/storage、deployment/manifest，以及流式传输所选 connector 的 `*_kernel_transport.h/.c`。组合 profile 必须解析为一个显式 endpoint deployment。RPC role 默认为 `both`，这也是支持的端点角色，不创建独立 client/server core。

普通 send/direct route 使用 `delivery = unreliable`，codec 为默认 `schema_v1` 或 `positional_v1`。RPC 使用 `managed_tagged` 和不同请求/响应 ID。profile v2 拒绝 reliable delivery、replay/guarantee 声明、mapped RPC、positional RPC、retained LATEST/FIFO route 声明。本地保留数据使用独立容器，retry/job 使用应用策略。见 [payload 格式](packet-protocol-cn.md)及完整[遥测示例](../examples/00_telemetry/README.md)。

## 区分逻辑槽与字节容量

逻辑上限见 [kernel 容量参考](kernel-api-cn.md#capacities)。WLC 按所选 codec 计算每条消息的有限 encoded maximum。无界 string/bytes、general repeated，包括嵌套内的字段，不能为 kernel route 确定容量。canonical frame 超过协议上限会被拒绝；不能用较小 buffer 静默缩小 schema 值域。

业务 body 上界为 `B` 时，普通消息 native frame 上界为 `4+B`，请求为 `17+B`，响应为 `max(17+B, 21)`。请求/成功响应前缀含四字节 DATA 头和至多 13 字节 metadata，拒绝响应连同包头最多需 21 字节。WLC 生成：

- `*_KERNEL_REQUEST_FRAME_MAX`：至少 `17`，覆盖所有选定请求。
- `*_KERNEL_REPLY_FRAME_MAX`：至少 `21`，覆盖所有响应/拒绝。
- `*_KERNEL_MESSAGE_FRAME_MAX`：至少 `4`，覆盖普通 send route。
- `*_KERNEL_PACKET_FRAME_MAX`：最大 canonical frame，包含所选 incoming/direct route 及上述存储下限。
- `*_KERNEL_WIRE_FRAME_MAX`：该 packet maximum 在选定 connector 下的 wire bound，包含 framing/integrity。

端点按各自上界保存四份 request array、四份 reply array、一份普通 array，另外还有 core/handler/带类型 reply scratch 和对齐。用 `sizeof(generated_endpoint_type)` 得到对象大小，不是九倍全局 frame maximum。RX 与 connector/硬件存储单独分配。

## 独立规划 RX 与 TX

流式 TX scratch 必须覆盖 `maximum_frame_size` 的 wire bound，每个 RX unit 必须独立覆盖 `maximum_rx_frame_size` 的 wire bound。两个字段均计 **canonical** 字节。RX 字段为 `0` 时，connector init 按自己的 RX unit 容量推导 limit：

- COBS/CRC16 选择至多 `2052`、且完整 wire bound 能放进 unit 的最大 canonical 长度。
- length16/NONE 在 unit 容量允许时选择 `min(unit_size - 2, 2052)`。

推导长度小于四字节时 init 失败。增大 TX 上界不会扩大 RX，减小 TX 上界也不会缩小 RX。需要精确非对称接收 limit 时，在手工 connector 配置中显式设置 `maximum_rx_frame_size`，并提供对应 wire 容量。

生成的 `*_kernel_transport_init` 要求 `rx_unit_size` 和 TX `wire` span 至少为 `*_KERNEL_WIRE_FRAME_MAX`，RX 总存储容纳两个 unit；RX canonical limit 仍由上述容量推导。[Wire 参考](packet-protocol-cn.md#connector-字节与容量)定义 bound 公式及最大帧 `2052 / 2064 / 2054` 示例。手工组装用 `wl_packet_stream_cobs_crc16_bound` 或 `wl_packet_stream_length16_none_bound`。

native USB 还要求合法帧以外的严格 read headroom，Astrial host RX 容量必须为 IN-MPS 倍数；native UDP 要求 maximum datagram size 以外多一个字节。UART DMA 驱动 buffer 和 FIFO staging 需要独立容量与流控策略。这些要求属于 [adapter 配置](adapters-cn.md)，不属于调用槽计数。

## 验证所选路径

[`packet_stream` 测试](../tests/core/packet_stream.c)覆盖非对称 TX/RX 上界、所有 native payload 长度和 partial-feed 压力。WLC `tests/kernel.rs` 用共享 RPC kernel 检查两种 stream connector，`tests/codec_plan.rs`、`tests/static_max.rs` 验证所选 codec 容量。Installed `tests/package/wlc_current` 构建 native 和 COBS 端点。按[开发指南](development-cn.md)运行；物理测量记录 connector、schema/profile 和 compiler identity。
