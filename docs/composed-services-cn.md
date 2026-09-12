# Wirelink / RPC 静态多服务迭代（ABI 32）

本轮只修改 Wirelink 和独立 WLC 仓库，不修改 Ragtime_Firmwares、libflorid 或 FCI
现有业务协议。开发版本为 `0.8.0` / codegen ABI 32，须配套重建生成代码和核心。
compact-v1 帧、现有 codec 字节、managed/mapped RPC payload 均不改变。

## 已实现

- 根 `.wl` 使用相对路径 `import` 组合独立 schema；全局类型、ID、reserved 冲突拒绝，
  共享依赖去重、循环拒绝。根版本表示组合协议版本，导入路径/顺序不进入 identity。
- `wlc dependencies` 及 CMake 传递依赖跟踪，导入文件或导入关系变化都会重新生成。
- profile `direct` 路由：typed borrowed callback，bytes/string 不复制入 mailbox，
  dispatcher 统一释放 RX（含错误路径），静态 payload 上界纳入 endpoint 容量。
- `wl_endpoint_set_services()`：初始化期绑定静态数组，progress/deadline/session/close
  与生成 RPC 组合。关闭先 quiesce adapter，再逆序清理服务；不引入线程、堆或动态注册表。
- RPC `on_response_terminal`：带请求 identity 和业务状态的可靠响应 ACK/timeout/failure
  通知。回调内只记录意图，owner pass 返回后再决定是否重启；缓存重放需幂等处理。
- [可运行组合样例](../examples/04_composed_services/README.md)：真实 bulk sender/receiver、
  RAM sink、RPC、telemetry 共用 endpoint，包含 BUSY/丢失 Status/重试/关闭 abort。

## Ragtime / libflorid 下一轮接入方式

产品层新增根 schema，导入现有 arm 与 upgrade 定义；使用多 profile 生成一个产品
endpoint。USB Bulk adapter 只绑定它的一个 `wl_ctx_t`，一个 owner 负责 step。
把 UpgradeControlProtocol 的服务逻辑和 flash sink 从独占链路包装中拆出；BulkBegin、
Chunk、End、Abort/Status 通过 direct 路由接入。保留原消息 ID 和 payload，尤其不要
把 mapped operation_id RPC 默默改为 managed RPC。

StartUpgrade 由产品状态机暂停机械臂业务，并限制 telemetry/普通 RPC 入场，升级管理
RPC 始终可达。当前调度是 RPC 优先、服务回调轮转，不提供拥塞下的严格带宽保证；
无限 RPC 生产者仍可能饿死 bulk，所以业务准入策略不能省略。

可靠 RX 的非零 peer session 会在 direct 分发前通知服务；回调中既要 reset bulk，
也应调用生成的 `*_runtime_peer_observe()` 清理旧 RPC session。unreliable compact-v1
消息没有 session，不能单靠它检测主机重启。断线/显式取消也应触发应用清理。

flash sink 的 BUSY 必须表示未消费数据、未做持久改变；不能以 BUSY 表示“已排入后台
写队列”。本轮没有新增异步持有 RX 的机制。flash erase/program 延迟较大时，需要应用
自己的有界拷贝队列及明确的持久化确认语义，不能提前推进 bulk offset。

Reboot 必须匹配正确的请求/session、业务成功且可靠响应 `WL_EVT_TX_SUCCESS`，在
安全点执行。ACK 仅证明链路接收，不证明 host 业务已处理；错误/超时不可当成功。
MCUboot secondary slot、签名/完整性校验、test/confirm/revert 和断电恢复仍是产品层工作。

## 本轮验证（2026-09-09）

- WLC 全量 Rust/generated C/C++ 测试、Clippy `-D warnings` 通过。
- direct 路由及默认 endpoint 的 ASan/UBSan 定向测试通过。
- Zephyr `unit_testing` + `native_sim`：34 个场景、254 个用例通过；使用隔离模块列表，
  避开本机 Ragtime 自定义模块对独立 C 测试引入的 C++ 配置依赖。
- 含组合样例、增量导入依赖、host executor、UDP、session matrix 在内的 22 个主机
  回归通过；组合样例还通过 native_sim 和 qemu_cortex_m3。
- 安装包消费者通过：3 个核心/C++/host 用例、6 个 WLC 生成消费者用例。
- 使用工作区真实 FCI arm/upgrade schema 及现有三份服务/端侧 profile，加上临时 direct
  bulk profile，成功生成并严格 C11 编译一个 `fci_product` runtime（40 个 binding），
  默认 endpoint payload 上界自动得到 2048 B；未改动原 FCI 文件。
- J-Link PLUS（609799419）连接 STM32H723VG，直接烧录独立 Zephyr 测试镜像，未备份
  或恢复旧镜像（按用户要求），未 mass erase/修改 option bytes。RTT 输出：

```text
COMPOSED_SERVICES PASS bytes=4096 rpc=1 telemetry=40 busy=1 dropped_status=1 reboot_ack=1 abort=1
```

实机镜像 FLASH 90724 B、RAM 35328 B，含两端 endpoint、4096 字节源/目标缓冲和测试
基础设施；不是单个业务端点的 footprint。时钟为确定性测试 tick，不是传输性能测量。
RTT 记录位于本地构建目录 `build/composed-h7-isolated/rtt.log`。目标板留在测试镜像上。
本轮实机使用板内 loopback，**未验证真实 USB Bulk 上传、flash sink 或 MCUboot 换槽**。
