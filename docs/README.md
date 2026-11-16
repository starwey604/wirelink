# Wirelink 中文工程文档

[返回仓库首页](../README.md) · [English](README-en.md) · [WLC 编译器](../wlc/README.md)

本目录面向 Wirelink **0.14.0 本地候选**（Wire v2 / 单文件节点 / Codegen 0.14）。从旧版升级请先读 [0.14 迁移说明](release-v0.14.0-cn.md)，语言与生成 API 见 [WLC 指南](../wlc/README.md)。

| 文档 | 目标读者 | 核心内容 |
| --- | --- | --- |
| [Wire v2 与编码规范](wire-format.md) | 协议调试、抓包分析、跨语言实现者 | 4B 小端帧头、8/12B RPC 头、Schema-v1 TLV / Positional-v1 与 COBS+CRC16 字节图解 |
| [传输层移植指南](porting-adapters.md) | MCU 驱动 / BSP / 硬件接入工程师 | embedded 物理钩子、背压与关闭；external 端口与异步租约 |
| [开发与验证指南](development.md) | 核心贡献者、CI 与发布维护者 | 目录职责、CMake / Cargo / Zephyr Twister / Fuzz / 体积测算与全量验收命令 |

---

## 核心教学示例与锚点校验

仓库 [`examples/`](../examples/) 下的所有示例均参与构建测试，文档中嵌有 `<!-- [docs:...] -->` 标记的代码块由 [`tests/tutorials/doc_sources.py`](../tests/tutorials/doc_sources.py) 自动校验与源码完全一致：

- **裸机无硬件回环**：[`examples/bare_metal_loopback.c`](../examples/bare_metal_loopback.c)（无需 WLC 即可编译运行）
- **单向遥测发布/订阅**：[`examples/00_telemetry/`](../examples/00_telemetry/)
- **简化 / 并发 RPC 与取消超时**：[`examples/01_rpc/`](../examples/01_rpc/)
- **定长缓冲区与变长字符串/字节寿命**：[`examples/02_device_info/`](../examples/02_device_info/)
- **有状态设备业务与异步进度通知**：[`examples/03_device_service/`](../examples/03_device_service/)
- **跨文件消息与 RPC 组合**：[`examples/04_composed_services/`](../examples/04_composed_services/)
- **Zephyr / Host UDP 联调**：[`examples/05_zephyr_udp/`](../examples/05_zephyr_udp/)
- **C++20 与 Python SDK 包**：[`examples/06_bindings/`](../examples/06_bindings/)、[`examples/07_device_sdk/`](../examples/07_device_sdk/)

### 遥测定义与接收回调速览

<!-- [docs:telemetry_schema] -->
```wl
message Telemetry @id(10) {
  uint32 sample @id(1);
  int32 temperature_centi_c @id(2);
}
node pub { send Telemetry; }
node sub { recv Telemetry; }
```
<!-- [/docs:telemetry_schema] -->

<!-- [docs:telemetry_receive] -->
```c
unsigned telemetry_received;
void sub_on_telemetry(sub_node_t *self, const telemetry_t *message) {
  (void)self;
  EXAMPLE_REQUIRE(message->sample == telemetry_received);
  EXAMPLE_REQUIRE(message->temperature_centi_c == -1234);
  ++telemetry_received;
}
```
<!-- [/docs:telemetry_receive] -->
