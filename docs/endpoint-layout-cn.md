# 按端侧角色和传输裁剪静态内存

状态：内部开发，WLC 生成 ABI 30；无新 tag、发布或 main 合并。
[English](endpoint-layout.md)。

## 只在端侧 profile 声明一次

共享 `services.bind.wl` 继续定义请求、响应和可靠性。设备端另有：

```wl
profile version 1;
endpoint {
  envelope = native_packet;
  rpc_role = server;
}
send DeviceTelemetry { delivery = unreliable; }
```

客户端的端侧文件改用 `rpc_role = client`，并用 `latest DeviceTelemetry`
声明接收。构建仍组合 `PROFILES services.bind.wl server.bind.wl`；不增加
逐 RPC 的开关，也不修改业务 handler。完整例子见
[12 个设备服务](../examples/03_device_service/README-cn.md)。

`envelope` 指“一帧数据如何装进传输”，不是驱动名称：

| 取值 | 用途和布局 |
| --- | --- |
| `native_packet` | UDP 等保留包边界的传输；不分配 stream FIFO |
| `bus_length16` | 带 16 位长度前缀的完整单元；不分配 stream FIFO |
| `cobs_stream` | 串口等字节流；保留 COBS 编码空间和 stream FIFO |
| `any` | 保留所有封装所需的最大空间，允许运行时选择 |

`rpc_role` 是本端能力，不是单次调用状态：`client` 发起调用，`server` 处理请求，
`both` 两者都需要。没有 RPC 定义就不分配 RPC 组件。
省略整个配置块或其中的属性，分别沿用 `any` / `both`，不会悄悄裁掉原有能力。
组合后的 profile 至多有一个 `endpoint` 块；重复即报错，没有文件覆盖优先级。

## 实际裁掉什么

- 固定封装按它的最坏帧长预留空间；packet 布局移除 stream FIFO。
- client 移除服务端实例、pending/cache 槽及响应缓存、请求解码和业务请求暂存。
- server 移除客户端实例、调用/响应槽、异步提交队列、排队请求和同步调用状态。
- client 普通入口不再有 `on_<rpc>` 字段；server 不再生成 `_async/_sync/cancel`。
  服务端的即时 handler 和高级延迟回复仍可用。

CRC 仍按 CRC32C 上限预留，允许使用较小校验。RPC 槽数仍通过构建定义
`<PREFIX>_ENDPOINT_RPC_CAPACITY` 统一设置，默认 4；必须在所有消费该头的翻译单元一致。
运行时只能调整已有能力，不能重新开启被裁掉的角色或切换到另一种固定封装；
初始化返回 `WL_ERR_NOT_SUPPORTED`。这些选择错误在请求 session 前检查。

高级 runtime 的公共指针和部分诊断元数据仍保留，未追求逐字节删除所有字段；
mapped RPC 的公共编码 scratch 仍是共享 union。本轮不引入堆分配，也不复用有重叠
生命周期的 TX payload / unit。可靠 RPC 去重、重传和业务 owned value 合同不变。

## 尺寸验收

对照 WLC `115f481` / ABI 29 和本轮 ABI 30，保持 Wirelink 核心不变，默认四槽。
用 `examples/03_device_service` 的真实 12 RPC 定义；两端仍共用一份服务合同。

| 编译目标 | 端点 | 原布局 / 字节 | 裁剪后 / 字节 | 减少 |
| --- | --- | ---: | ---: | ---: |
| Linux x86-64 | client | 7,712 | 6,352 | 17.6% |
| Linux x86-64 | server | 6,288 | 4,592 | 27.0% |
| Cortex-M7 / ARM EABI | client | 7,000 | 5,664 | 19.1% |
| Cortex-M7 / ARM EABI | server | 5,608 | 4,016 | 28.4% |

M7 两端合计少 2,928 字节。ARM 数字来自 `arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb`
编译的 `sizeof(endpoint_t)`，不是 H7 烧录/运行结果，也不包含驱动、线程栈或整机 RAM。
没有 CPU/延迟 benchmark，不能据此宣称 CPU 加速。
主机 GCC 16.2.1、ARM GCC 16.2.0，均为 C11 / `-O2`。把 `sizeof` 写入静态字节数组的
长度，再用 `nm -S --radix=d` 读取符号尺寸；不需要在主机运行 ARM 程序。

另一个近 2 KiB 响应的合成用例，x86-64 四槽通用布局为 29,648 字节；固定
native packet 后 client 为 18,880、server 为 18,592 字节，分别减少 36.3% / 37.3%。
收益取决于消息上限、角色、槽数和平台对齐，不是固定节省比例。

## 回归入口

```sh
cargo test --manifest-path /path/to/wlc/Cargo.toml --test endpoint_layout -- --nocapture
ctest --test-dir build/maintainer-api -R wirelink_device_service --output-on-failure
```

第一项打印尺寸，运行三个封装 × 两种槽数，覆盖分片输入、大响应、遥测、自动回收、
关闭取消、重建和无效布局配置，并检查严格 C11 / C++20 消费。
第二项实际运行 UDP 双进程，包括同步、异步、延迟回复和新增第 13 个 RPC 的扩展验收。
生成 ABI 更新需完整重生成 codec/runtime 文件；线上格式、schema identity 不变，
profile identity 包含本端布局，但不是两端必须相等的握手字段。

## 本轮验证结果

| 检查 | 结果 |
| --- | --- |
| WLC 全量测试 | 142/142 |
| Rust fmt / Clippy 全 target、feature，警告视为错误 | 通过 |
| 布局、异步生命周期、managed RPC、规范化验证的 Clang ASan/UBSan | 10/10 |
| 主机 Release CTest，含 C/C++、Python FFI、UDP 故障和扩展验收 | 20/20 |
| 主机插桩 C/C++ CTest | 17/17；排除 Python 和单独重建的 extension |
| 插桩共享库的两项 Python FFI | 2/2；见下方环境说明 |
| Zephyr codec / application runtime / bulk / mixed traffic | 12/12 配置，39/39 用例，无警告 |

Python 初次直接加载插桩库因缺少 `__asan_report_store4` 失败；仅针对 FFI 测试预加载
匹配的 Clang ASan runtime，设置 `ASAN_OPTIONS=detect_leaks=0` 后通过。
这验证插桩桥接库，不代表 Python 解释器本身做了完整插桩或泄漏检查。
新增 RPC 的 extension 在 Release 下重建并验收，不计入上述插桩用例数。

Zephyr 使用 `unit_testing`、`native_sim`、`qemu_cortex_m3`、`qemu_riscv32`，
`-j2`，SDK 1.0.1，显式传入 ABI 30 的 `WIRELINK_WLC_EXECUTABLE`。
初次未传工具路径时，mixed-traffic 的生成步骤被开发 ABI 检查拒绝；补齐路径后完整重跑通过。
测试只借用已有 Zephyr 工作区，不改产品配置，也没有同时运行 CTest stress 或性能 benchmark。

本地日志、冻结基线工具、对象文件和矩阵输出位于
`build/endpoint-layout.wbBqMM/`（不入库）。当前 conformance fixture 完整重生成，
previous fixture、线上向量和业务 C 文件未修改。改动已整理为两个仓库的本地 dev 提交，
见[配套提交索引](optimization-commits-cn.md)；未推送、发布，也未更新
libflorid / Ragtime_Firmwares 的配套版本。
