# 多 RPC 维护体验：本轮实现与验收

日期：2026-09-08。范围仅 Wirelink 和独立 WLC 开发工作区；未迁移 libflorid、
Ragtime_Firmwares，未提交、推送、合并 main 或发布 tag。目标是减少业务维护步骤，
本记录不以主机测试推断固件性能。

这是 ABI 27 维护体验迭代的历史记录；后续 ABI 28 的实现和测量见
[API 性能回归记录](api-performance-progress-cn.md)。

## 已完成

- **一份 RPC 合同供两端使用**：WLC 重复 `--profile`，CMake 使用 `PROFILES`，
  组合共享服务和端侧路由。每份文件独立验证；重复声明或跨文件冲突报错，顺序不产生覆盖。
- **只发送也能推导容量**：`send Message { delivery = ...; }` 生成发送助手并参与
  endpoint 上限计算，不创建接收邮箱。支持没有 RPC/接收路由的纯发送端点。
  现有 latest/fifo 对称发送助手保留；显式 send 可单独指定出站可靠性。
- **公共业务上下文只配置一次**：普通 handler 继承 `config.user_data`；非 NULL 的
  `<service>_user_data` 覆盖单个服务。高级 deferred 和每次调用完成回调仍显式传上下文。

默认端点继续负责编号、可靠传输、请求快照、结果回收和响应编码。
业务模块只注册类型安全的函数，不增加另一套分发表或包装框架。

## 验收入口

先读 [03_device_service 中文说明](../examples/03_device_service/README-cn.md)，
再按其中顺序看普通 handler → 服务端组装 → 共享定义 → 客户端 → 延迟自检。

示例有 12 个 RPC，按设备状态、计算和延迟任务组织；client/server 为独立 C11 UDP 程序。
覆盖业务拒绝、自持字符串、三个不同 RPC 的异步批次、接受后改写请求，以及 100 通道单向遥测。

[扩展验收脚本](../tests/tutorials/device_service_extension.py)在临时副本增加
`GetBuildLabel` 并重新编译、运行新旧用例，检查恰好四个业务文件改变：

1. `device.wl`：消息数据与唯一编号。
2. `services.bind.wl`：一条共享 RPC 声明。
3. `device_service.c`：handler 与注册语句。
4. `client.c`：新调用及断言。

无需改传输、owner loop、CMake、手工存储或已有服务。
仅新增服务实现而不补演示客户端调用时，需要前三个文件。

## 已运行的软件验证

| 检查 | 结果 |
| --- | --- |
| WLC 全套 Rust/生成 C 消费测试 | 119/119；fmt、Clippy `-D warnings` 通过 |
| 新 send/profile 测试 | 3/3；含可靠/不可靠实核通信、时钟读取、无接收存储、C++ 头文件 |
| Release 主机 CTest | 20/20；含新例子、实际新增 RPC、现有 UDP 故障和宿主集成 |
| 安装后的独立消费者 | 6/6；含安装后 CMake `PROFILES` 组合 |
| Clang ASan/UBSan 主机 CTest | 17/17；排除 Python bridge 和单独 Release 构建的扩展测试 |
| 新 send/profile ASan/UBSan 测试 | 3/3 |
| Zephyr generated_codec / application_runtime | 4/4 配置、19/19 用例；unit_testing、native_sim、Cortex-M3 QEMU |
| 工具解析 | 显式工具、错误 ABI、离线缺工具、未发布源码配对的诊断均通过 |

本机日志位于 `build/maintainer-api/`，主要是 `wlc-tests-final.log`、`ctest-final.log`、
`package-test.log`、`ctest-sanitize-final.log` 与 `twister-isolated/twister.json`。
第一次 Twister 载入了 Ragtime 产品模块，遇到 C++ feature 配置错误；最终运行显式设置
`ZEPHYR_MODULES` 为工作区 `modules/hal/cmsis_6`，测试自身追加 Wirelink，隔离后无警告通过。
本轮没有运行 Windows/macOS 或 H7 实板测试。

## 版本与后续边界

生成 ABI 升至 **27**，主要新增语义是普通 handler 的 NULL 上下文由“直接传 NULL”
改为“继承公共上下文”。生成消费者需配套重建；业务 codec、Compact-v1、托管 RPC v2
线上格式不变，既有不含 send 的 profile identity 不变，冻结 fixture 未改消息 schema。

匹配的 WLC ABI 27 仍是本地开发源码，必须显式提供工具或放入 PATH。
最近的分发快照仍为 ABI 26；自动下载会明确诊断缺少配对，不会误取旧版本。
远程 CI 的旧 WLC pin 尚未升级，不能将本轮本地通过等同于远程 CI 已通过。
配对提交/推送及更新源码 pin、SHA-256 留在后续发布集成步骤。

本轮未做端侧角色裁剪、槽数优化或产品端 RPC 迁移。12 个服务也不等于 12 个并发槽；
示例保留默认四槽和单帧限制。产品的调度、公平性、反馈健康判定仍由产品定义。
