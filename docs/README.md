# Wirelink 中文工程文档导航

[返回仓库首页](../README.md) · [English](README-en.md) · [WLC 编译器与 Schema 手册](../wlc/README.md) · [在线交互式教程站点 (`site/`)](../site/)

本目录收录了面向 **Wirelink 0.14.0 本地候选**（Wire v2 / 单文件节点声明 / Codegen 0.14）的深度工程文档。如果你是第一次接触 Wirelink，推荐先阅读仓库首页的 **[三步快速上手](../README.md)** 或启动 [`site/`](../site/) 下的 **6 节交互式课程**；从 0.13 升级请先查阅 **[0.14 迁移说明](release-v0.14.0-cn.md)**。

| 核心文档 | 适合谁读？ | 你能在这里查到什么？ |
| :--- | :--- | :--- |
| **[WLC 与 Schema 语言指南](../wlc/README.md)** | 应用与固件开发者 | `.wl` 类型系统、默认必选与 `optional`、`node` 五项配置、生成的 C11 函数签名与 CMake 集成 |
| **[传输层驱动移植指南](porting-adapters.md)** | MCU 驱动 / BSP 工程师 | 默认 `embedded` 发送钩子与串口 `rx_feed` 留尾规则；进阶 `external` 异步零拷贝 DMA 租约协议 |
| **[Wire v2 二进制编码规范](wire-format.md)** | 协议调试、抓包分析者 | 4B 小端帧头、8/12B RPC 子头、Schema-v1 TLV / Positional-v1 与 COBS+CRC16 字节级布局 |
| **[开发与验证指南](development.md)** | 核心贡献者、CI 维护者 | 仓库模块红线、CMake / Cargo / Zephyr Twister / Fuzz / 静态 RAM 体积测算与全量测试命令 |

---

## 循序渐进的可运行示例（`examples/`）

仓库 [`examples/`](../examples/) 下的每一个示例都是真实参与编译和 CTest 测试的工程。文档中所有带 `<!-- [docs:...] -->` 标记的代码块均由 [`tests/tutorials/doc_sources.py`](../tests/tutorials/doc_sources.py) 自动校验，确保与源码逐字节同步：

1. **免 WLC 裸机内存回环**：[`examples/bare_metal_loopback.c`](../examples/bare_metal_loopback.c) —— 30 秒验证工具链，体验双向并发 RPC、遥测与安全停机。
2. **第 01 站 · 单向遥测发布与订阅**：[`examples/00_telemetry/`](../examples/00_telemetry/) —— 最精简的默认 `embedded + static` 节点，演示单向发送与单槽背压。
3. **第 02 站 · 简化/并发 RPC 与取消超时**：[`examples/01_rpc/`](../examples/01_rpc/) —— 演示 4 路并发调用、业务拒绝（`WL_KERNEL_REJECTED`）以及底层生命周期测试。
4. **第 03 站 · 有界字符串与回调生命周期**：[`examples/02_device_info/`](../examples/02_device_info/) —— 演示零 `malloc` 的 `string<N>` 内联数组与跨回调按值拷贝。
5. **第 04 站 · 有状态设备服务与长耗时任务**：[`examples/03_device_service/`](../examples/03_device_service/) —— 演示用“短 RPC 领 `job_id` + 主循环轮询发进度消息”解耦耗时自检。
6. **第 05 站 · 多模块 `.wl` 组合**：[`examples/04_composed_services/`](../examples/04_composed_services/) —— 演示通过 `import` 将多个业务模块汇聚到一个产品节点。
7. **第 06 站 · Zephyr / Host UDP 双端联调**：[`examples/05_zephyr_udp/`](../examples/05_zephyr_udp/) —— 真实 UDP 传输下的服务端事件循环与客户端联调。
8. **第 07 站 · 上位机 Python / C++20 SDK**：[`examples/06_bindings/`](../examples/06_bindings/)、[`examples/07_device_sdk/`](../examples/07_device_sdk/) —— 使用 `wlc sdk` 生成的可安装 Wheel 与 C++20 客户端工程。

### 极简味道速览：单向遥测只需写什么？

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

生成代码后，发布端实现整帧发送钩子 `pub_tx_write`，订阅端直接实现同名接收函数 `sub_on_telemetry` 即可收到强类型结构体（无需注册回调表，也没有繁琐的 `has_*` 字段）：

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
void sub_on_telemetry(sub_node_t *self, const telemetry_t *message) {
  (void)self;
  EXAMPLE_REQUIRE(message->sample == telemetry_received);
  EXAMPLE_REQUIRE(message->temperature_centi_c == -1234);
  ++telemetry_received;
}
```
<!-- [/docs:telemetry_receive] -->
