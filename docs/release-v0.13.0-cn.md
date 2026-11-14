# Wirelink v0.13.0

Wirelink 0.13 将执行入口统一为 allocation-free C11 endpoint kernel，并与
WLC 0.13.0、codegen contract 0.13、SDK binding API 3 配套发布。

## 主要变化

- Core 由应用提供单调时间和执行机会，不依赖操作系统、线程、驱动或堆分配。
- 四个出站 RPC 调用、四个回复快照和一个普通消息快照，通过九项 admission queue 调度。
  已接纳的调用产生一次终态回调，排队时间计入 timeout。
- Packet port 区分源缓冲已消费和物理发送已排空，保留双 RX 单元、借用及 token 校验。
- Profile v2 生成类型化 kernel，支持 native/NONE、COBS/CRC16 和 length16/NONE 连接器。
- C++ 和 Python SDK 提供自持结果、同步与 asyncio 调用，以及确定性关闭。
- 修复 Windows C11/原子编译要求、生成 C 的严格编译警告、UDP 测试时序及 Python 工具版本混用。

## 从旧版本迁移

这是从旧 link/endpoint/pump 执行接口到 kernel 的接口变更。旧 profile v1
reliable/replay/mapped/deferred 配置不能直接用于当前 kernel；业务层负责重试、幂等和长任务协议。
RPC 的返回 view 只在回调期间有效，需要保留的数据应复制，或使用 SDK 的自持结果。

同时更新 Wirelink 与 WLC，使用 profile v2 重新生成 C、kernel 和 SDK，连同 provenance
manifest 一起提交。发布包中的 `wirelink-release.json` 与 WLC 的 `wlc-release.json`
记录精确提交配对；只匹配版本号不足以标识这组发布产物。

参阅 [执行模型](https://github.com/starwey604/wirelink/blob/v0.13.0/docs/kernel-cn.md)、[API](https://github.com/starwey604/wirelink/blob/v0.13.0/docs/kernel-api-cn.md)、[安装](https://github.com/starwey604/wirelink/blob/v0.13.0/docs/installation-cn.md)
与 [开发检查](https://github.com/starwey604/wirelink/blob/v0.13.0/docs/development-cn.md)。Schema-v1 codec 与 frozen-v1 conformance vectors
继续约束 wire 行为；新的执行接口不改变这些编码规则。

## 发布验证与产物

- Linux、Windows、macOS Intel/ARM 的 core、owner、适配器、生成示例和安装后消费者。
- CPython 3.10–3.14：manylinux_2_28 x86-64、Windows x86-64、macOS 13+ Intel/ARM。
- 两套 SDK 共 40 个 wheel 与两个 sdist，离线安装并与独立 C 服务互通。
- Clang ASan/UBSan、libFuzzer smoke、Zephyr native/QEMU 测试，以及 ESP32-S3 USB bulk 构建。

ESP32-S3 的这一项是构建验证；物理 UART/USB I/O 资格仍按各适配器的硬件验证记录判断。
发布资产附带 `SHA256SUMS`，供下载完整性验证。
