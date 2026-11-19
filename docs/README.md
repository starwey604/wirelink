# Wirelink 中文工程文档导航

[返回仓库首页](../README.md) · [English](README-en.md) · [WLC 编译器与 Schema 手册](../wlc/README.md) · [在线交互式教程站点 (`site/`)](../site/)

本目录收录了 Wirelink 的核心工程文档。如果你是第一次接触 Wirelink，推荐先阅读仓库首页的 **[三步打通上下位机通信](../README.md)** 或浏览 [`site/`](../site/) 下的 **6 节渐进式实战课程**。

| 核心文档 | 适合谁读？ | 你能在这里查到什么？ |
| :--- | :--- | :--- |
| **[WLC 与 Schema 语言指南](../wlc/README.md)** | 应用与固件开发者 | `.wl` 类型系统、有界字符串 `string<N>`、可选字段 `optional`、`node` 配置、生成的 C11 函数签名与 CMake 集成 |
| **[传输层驱动移植指南](porting-adapters.md)** | MCU 驱动 / BSP 工程师 | 默认 `embedded` 发送钩子与串口 `rx_feed` 留尾规则；进阶 `external` 异步零拷贝 DMA 租约协议 |
| **[Wire v2 二进制编码规范](wire-format.md)** | 协议调试、抓包分析者 | 4B 小端帧头、8/12B RPC 子头、Schema-v1 TLV / Positional-v1 与 COBS+CRC16 字节级布局 |
| **[开发与验证指南](development.md)** | 核心贡献者、CI 维护者 | 仓库模块红线、CMake / Cargo / Zephyr Twister / Fuzz / 静态 RAM 体积测算与全量测试命令 |

---

## 循序渐进的可运行示例（`examples/`）

仓库 [`examples/`](../examples/) 下的每一个示例都是真实参与编译和 CTest 测试的工程。文档中所有带 `<!-- [docs:...] -->` 标记的代码块均由 [`tests/tutorials/doc_sources.py`](../tests/tutorials/doc_sources.py) 自动校验，确保与源码逐字节同步：

1. **上下位机 RPC 服务与并发/取消生命周期**：[`examples/01_rpc/`](../examples/01_rpc/) —— 演示请求/响应配对、业务拒绝（`WL_RPC_REJECTED`）、4 路并发调用与主动取消。
2. **单向遥测发布与订阅**：[`examples/00_telemetry/`](../examples/00_telemetry/) —— 演示单向 `send`/`recv` 节点、输入立即快照与单消息槽背压。
3. **有状态设备服务（短 RPC + 异步进度遥测）**：[`examples/03_device_service/`](../examples/03_device_service/) —— 演示用“短 RPC 领 `job_id` + 主循环轮询发进度消息”解耦耗时硬件自检。
4. **有界字符串与回调生命周期**：[`examples/02_device_info/`](../examples/02_device_info/) —— 演示零 `malloc` 的 `string<N>` 内联数组与跨回调按值拷贝（`result = *response`）。
5. **多模块 `.wl` 组合**：[`examples/04_composed_services/`](../examples/04_composed_services/) —— 演示通过 `import` 将多个业务子模块汇聚到一个产品节点。
6. **Zephyr / Host UDP 双端联调**：[`examples/05_zephyr_udp/`](../examples/05_zephyr_udp/) —— 真实 UDP 传输下一面响应 RPC、一面定时每 20ms 推遥测的服务端主循环与客户端联调。
7. **上位机 Python / C++20 SDK**：[`examples/06_bindings/`](../examples/06_bindings/)、[`examples/07_device_sdk/`](../examples/07_device_sdk/) —— 使用 `wlc sdk` 生成的可安装 Wheel 与 C++20 客户端工程。
8. **免 WLC 底层内存回环**：[`examples/bare_metal_loopback.c`](../examples/bare_metal_loopback.c) —— 演示 `io = external; handlers = dynamic;` 下的底层端口租约与 `quiescent` 安全停机。

### 极简代码速览：单向遥测收发只需写什么？

在 [`examples/00_telemetry/`](../examples/00_telemetry/) 中，先用一份 `telemetry.wl` 声明数据结构与收发两端角色：

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

生成代码后，发布端实现整帧发送钩子 `pub_tx_write`，订阅端直接实现同名接收函数 `sub_on_telemetry` 即可收到解码好的结构体：

<!-- [docs:telemetry_tx_write] -->
```c
wl_err_t pub_tx_write(pub_node_t *self, const uint8_t *data, size_t length) {
  (void)self;
  return sub_rx_packet(&subscriber, data, length);
}
```
<!-- [/docs:telemetry_tx_write] -->

<!-- [docs:telemetry_receive] -->
```c
unsigned telemetry_received;
telemetry_t latest_telemetry;

void sub_on_telemetry(sub_node_t *self, const telemetry_t *message) {
  (void)self;
  latest_telemetry = *message;
  ++telemetry_received;
}
```
<!-- [/docs:telemetry_receive] -->
