# Wirelink 中文工程文档

[返回仓库首页](../README.md) · [English Placeholder](README-en.md) · [WLC 编译器](../wlc/README.md)

本目录收敛为三份直接面向开发的离线工程文档，与当前 Wirelink **0.13.0**（Wire v2 / 单结构体 API / Profile v2）代码完全对齐：

| 文档 | 目标读者 | 核心内容 |
| --- | --- | --- |
| [Wire v2 与编码规范](wire-format.md) | 协议调试、抓包分析、跨语言实现者 | 4B 小端帧头、8/12B RPC 头、Schema-v1 TLV / Positional-v1 与 COBS+CRC16 字节图解 |
| [传输层移植指南](porting-adapters.md) | MCU 驱动 / BSP / 硬件接入工程师 | 如何用 `wl_packet_port_t`、`wl_packet_stream_t` 与 `wl_byte_fifo_t` 接入 UART/USB/SPI/UDP |
| [开发与验证指南](development.md) | 核心贡献者、CI 与发布维护者 | 目录职责、CMake / Cargo / Zephyr Twister / Fuzz / 体积测算与全量验收命令 |

---

## 核心教学示例与锚点校验

仓库 [`examples/`](../examples/) 下的所有示例均参与构建测试，文档中嵌有 `<!-- [docs:...] -->` 标记的代码块由 [`tests/tutorials/doc_sources.py`](../tests/tutorials/doc_sources.py) 自动校验与源码完全一致：

- **裸机无硬件回环**：[`examples/bare_metal_loopback.c`](../examples/bare_metal_loopback.c)（无需 WLC 即可编译运行）
- **单向遥测发布/订阅**：[`examples/00_telemetry/`](../examples/00_telemetry/)
- **同步 / 并发 RPC 与取消超时**：[`examples/01_rpc/`](../examples/01_rpc/)
- **定长缓冲区与变长字符串/字节寿命**：[`examples/02_device_info/`](../examples/02_device_info/)
- **有状态设备业务与异步进度通知**：[`examples/03_device_service/`](../examples/03_device_service/)
- **多服务 Profile 组合**：[`examples/04_composed_services/`](../examples/04_composed_services/)
- **Zephyr / Host UDP 联调**：[`examples/05_zephyr_udp/`](../examples/05_zephyr_udp/)
- **C++20 与 Python SDK 包**：[`examples/06_bindings/`](../examples/06_bindings/)、[`examples/07_device_sdk/`](../examples/07_device_sdk/)

### 遥测定义与接收回调速览

<!-- [docs:telemetry_schema] -->
```wl
message Telemetry @id(10) {
  required uint32 sample @id(1);
  required int32 temperature_centi_c @id(2);
}
```
<!-- [/docs:telemetry_schema] -->

<!-- [docs:telemetry_bind] -->
```wl
profile version 2;
endpoint { envelope=native_packet; }

direct Telemetry {
  delivery = unreliable;
}

send Telemetry { delivery=unreliable; }
```
<!-- [/docs:telemetry_bind] -->

<!-- [docs:telemetry_receive] -->
```c
unsigned telemetry_received;
void telemetry_receive(void *user, const telemetry_t *message) {
  (void)user;
  EXAMPLE_REQUIRE(message->sample == telemetry_received);
  EXAMPLE_REQUIRE(message->temperature_centi_c == -1234);
  ++telemetry_received;
}
```
<!-- [/docs:telemetry_receive] -->
