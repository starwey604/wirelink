# RPC 易用性演进实施记录

2026-09-07；基线 Wirelink `d245cbe` / WLC `b5c444a` / 生成 ABI 21。
本轮只在 dev 完成 M0–M2，H7 功能验证开始前停止。没有发布、main 合并或产品迁移。

## M0：确定的合同

- 普通数据是 `<message>_value_t`，有界数据内嵌，无析构函数；赋值是独立副本。
  `<message>_t` 保留为显式高级视图。多个 runtime 共享同一 codec 的类型和转换。
- 字符串使用 `field.length` 和 `field.data[]`；长度是 UTF-8 字节数，允许嵌入 NUL。
  解码附加的末尾 NUL 不参与线上编码。presence、默认值和 required 规则不变。
- 普通 handler 形状为 `(context, const request_value_t *, response_value_t *) -> int32_t`；
  返回 0 是成功，非零仅表示业务拒绝码，不能混入框架错误。延迟 handler 单独注册。
- 异步提交返回 `wl_err_t`：`WL_OK` 表示已快照并接受，`WL_ERR_BUSY` 表示本地满载，
  其他错误表示未接受。失败不回调；已接受调用通过唯一完成回调自动回收。
  回调结果只暴露成功、业务拒绝、超时、取消、通信失败及每调用诊断，不暴露发送中间态。
- 超时包含排队时间；取消不撤销远端业务。完成/关闭、重入、句柄失效按演进计划的合同测试。
- 默认四槽最近结果缓存，只淘汰 DELIVERED；可缩小静态容量。严格保留选择 REJECT_NEW。
  不新增远端 BUSY 报文，不改变托管 RPC 元数据或正常请求/响应的线上格式。

## 缓存基线

`cargo test --manifest-path wlc/Cargo.toml --test rpc_usability -- --nocapture`
在真实 `src/rpc.c` 上测试容量 1/4/8、响应容量 18/63/2046 字节、100 次连续操作。
全部请求在同一毫秒内，未跨过 10 秒 TTL；先把缓存填满为 READY，再验证不能淘汰，
转为 DELIVERED 后继续请求。

| 响应容量 | 1 槽池字节数 | 4 槽 | 8 槽 |
| --- | ---: | ---: | ---: |
| 18（Add 上限含元数据） | 162 | 648 | 1296 |
| 63（有界短响应预算） | 207 | 828 | 1656 |
| 2046（近单帧上限） | 2190 | 8760 | 17520 |

池大小只包括每槽 pending + cache 元数据及响应存储，不是整个 endpoint 大小，
也未计生成装配的区域对齐。REJECT_NEW 分别接受 1/4/8 次后返回 CACHE_FULL；
最近结果策略在三种容量下都完成 100 次。更多槽扩展短期重放保护和积压容量，
不会增加链路 TX 窗口。默认选择 4 而非 8，且大帧用户应显式审视这项 RAM 成本。

## M1：自持值（完成）

WLC 检查点 `b2789461929de1c687bf562e8636f5ad343a15b3`，ABI 22，已推 dev；
配对 Wirelink `738a274e3d16ce1fff97936b84e10c8b5125edac`。
远端 [WLC CI](https://github.com/starwey604/wlc/actions/runs/34046010688)、
[Host CI](https://github.com/starwey604/wirelink/actions/runs/34046147929) 与
[Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34046147931) 全部通过。
这是独立可消费的数据所有权检查点；M2 再改变默认端点接口/布局时将递增 ABI，
不复用 ABI 22 标识不兼容的生成代码。

已新增生成及针对性测试：空消息、空/最大字符串和 bytes、嵌套值、packed 数组、
默认值、缺失 required、非法 UTF-8、超界和失败输出不变；复制后改写原输入仍有效。
值编解码复用现有描述符 codec，交叉编码字节相同，没有第二套协议实现。

Linux x86_64，GCC `-O2` 的首轮尺寸：

| 测试对象 | 自持值 sizeof | 最大编码字节数（不含 RPC 元数据） |
| --- | ---: | ---: |
| DeviceInfo（31 字节名称等） | 104 | 50 |
| Snapshot（两个嵌套 DeviceInfo、3 个 float） | 240 | 118 |
| Large（bytes&lt;2031&gt;） | 2048 | 2034 |

这些是主机布局，不是 H7 RAM 或 CPU 测量。已有 ABI 21 的 H7 idle 基线见
[时钟演进记录](endpoint-clock-evolution.md)；本轮不把它当作新 API 性能结果。

新增独立 `<module>_values.h` 入口，不声明借用消息类型或 runtime 装配；高级转换
留在 `<module>.h`。Python ctypes 直接消费 C 自持业务结构的复制/编解码测试通过，
无需映射 endpoint 私有布局；针对性 ASan/UBSan 测试通过。

GCC `-O2 -fstack-usage` 静态报告中，`large_value_decode` 自身为 64 字节，
`snapshot_value_decode` 为 240 字节。这不是完整调用链峰值或 H7 栈高水位。
大 bytes 值的解码使用小视图暂存，没有再在函数栈上分配 2 KiB 自持临时对象。
输出成功后一次复制有效字节，初始化时还会清零目标对象；编码借用值字段，不复制大数组。

`WLC_TEST_BENCHMARK=1 cargo test --manifest-path wlc/Cargo.toml --test owned_values -- --nocapture`
的首轮 Linux x86_64/GCC `-O2` 结果：2031 字节 payload，100000 次，进程 CPU 时间
约为视图解码 33.31 ns/次、自持值解码 96.75 ns/次；这是热缓存主机观测，非硬性预算，
不代替 H7 周期测量。差异包含输出清零与拥有数据的复制，不能宣称所有权转换零开销。

已通过：WLC 全量 114 项、fmt/clippy；生成 C/C++/Python 值消费；主机 Release
10 项（含 UDP 故障注入）、Clang ASan/UBSan 原生 9 项及预加载 ASan 的 Python
桥接 1 项；Ztest/native_sim/QEMU 44 配置、281 用例全部通过、无警告。
本地日志为 `build/rpc-m1-twister/twister.json`，主机为 `build/clock-host` 和
`build/clock-sanitize`。针对性生成 C Sanitizer 覆盖 owned_values、rpc_usability、
managed_rpc 四种 delivery。板级时钟样例在 native_sim、Cortex-M3、RISC-V32、
x86_64 QEMU 的 4 配置也全部通过（`build/rpc-m1-clock-sim/twister.json`）；
远端 CI 也已通过。

M2 将补上完成通知中槽位复用、close/reinit 后仍保留业务副本的端到端测试。

## M2：默认 RPC（实现完成，交付验证中）

WLC `26a07a49597cd06b455ea1060e5b7902d39ea061` / ABI 23 已推 dev；
[WLC CI](https://github.com/starwey604/wlc/actions/runs/34049280583) 全部通过，
含 Rust/C、Windows 和两种 macOS 主机 smoke。
其核心测试依赖为 Wirelink `6ea75b7`；Wirelink 本轮配对实现提交和 CI 在最终交付记录中补齐。

普通入口为 `<runtime>_endpoint.h`，业务数据为 `<codec>_values.h`。
手动端点 call/inspect/release 和 complete/reject 已移到显式 `<runtime>_advanced.h`，
普通入口不包含这些助手；因静态 C 布局，runtime 的部分声明仍传递可见，不能声称完全不透明。

已实现：

- 即时 `config.on_<service>` handler：自持请求、填写预初始化响应、零成功/非零业务拒绝。
  注册 handler 自动启用 server；client 初始化即就绪。慢任务仍显式使用高级 deferred token。
- `endpoint_<service>_async(..., timeout, callback, context, optional_call)`：
  接受前编码快照；有界排队，截止时间包含排队；失败提交无回调。
  完成前形成自持结果并回收调用，再通知一次；普通业务不再 inspect/release。
- 每调用结果区分成功、拒绝、超时、取消和通信失败；本地响应过大等框架错误不伪装成业务拒绝。
  单 TX 槽与 RPC 调用资源独立排空，响应先到不提前复用 TX 借用。
- 回调内提交/取消、拒绝递归 step/close/reinit、关闭通知、旧取消句柄失效。
  生成 close 先 quiesce，再交付剩余通知；不能用通用 handle close 代替。
- 默认四槽、最近已送达结果淘汰；`config.advanced` 保留严格策略和容量覆盖，
  `<PREFIX>_ENDPOINT_RPC_CAPACITY` 可一致地缩小静态容量。队列按最大请求预留，
  各服务共用最大 request/response 暂存 union。
- UDP 分离 client/server、C++/Python 薄桥接与中英文教程已迁移；桥接不再导出 release。
  映射 conformance schema 不变，所有生成文件/manifest 与 WLC 配对更新。

### 已通过的软件检查

- WLC 全量 115 项、fmt/clippy；新真实核心 + loopback 夹具覆盖 4 种 delivery × 1/4 槽。
  含字符串、2031 字节 bytes、100 次 TTL 内连续调用、突发满载、单槽 20 次续调、
  请求改写、结果复制后 slot 复用/close/reinit、拒绝、取消、回绕超时、失败提交与关闭唯一通知。
- C11 严格告警、普通端点 C++20 消费、原生 C++/Python bridge；对新普通异步、
  owned_values、managed_rpc、缓存基线运行 ASan/UBSan 均通过。
- Host Release 10 项；Clang ASan/UBSan 原生 9 项和预加载 ASan 的 Python 1 项。
  UDP 测试包含丢请求、丢响应、重复、重排、黑洞、响应先于请求 ACK、
  以及已执行业务的 ACK/第一次响应同时丢失；后两项断言丢包确实发生、业务执行一次。
- Ztest/native_sim/QEMU：45 配置、285 用例、5 平台全部通过、无警告，
  记录 `build/rpc-m2-twister/twister.json`。
- 独立 H1/时钟样例的最终 simulator matrix、两种 H7 容量的构建与配对 Wirelink
  远端 CI 正在收尾，未进行 H7 执行。

### 内存与复制边界

| 对象 / 平台 | 1 槽 endpoint | 4 槽 endpoint |
| --- | ---: | ---: |
| 字符串 Execute + 2031-byte Download，Linux x86_64 | 16832 B | 30160 B |
| 同一压力 profile，H7 交叉编译 | 待补构建报告 | 29824 B |

纯 Add 教程在 x86_64 默认四槽为 3536 B；大消息测试不能代表所有 RPC 的 RAM 成本。
H7 四槽完整双端样例链接报告 Flash 70244 B、RAM 78592 B，包含 Zephyr、RTT、两个端点和测试保存值，
不是 Wirelink 库本体大小，也不是实际栈高水位。外部板定义有两个既有 `ragtime` vendor-prefix
提示，未修改产品板定义；C 编译通过。

请求有一次队列编码及一次向链路 payload 的复制；响应先保存在 core RPC 存储，再解码为自持值。
应用若长期保存，再做一次显式结构体赋值。不能把所有权简化宣传为零复制。
普通提交在 step 外取时一次，双端一轮各一次，回调内续调不额外取时，close/result 不取时。
H7 CPU 周期、实际延迟和栈高水位仍未测量。

64-KiB RAM 的 Cortex-M3 放不下该双端四槽大消息压力样例，最初链接超过 RAM 7816 B；
没有扩大模拟硬件 RAM。矩阵改为 M3 验证一槽，其他三个平台验证一/四槽。
高并发本机构建期间 x86_64 QEMU 曾在 BIOS 超时及真实 uptime 阶段触及 100-ms 测试期限；
最终矩阵降低并发，H1 uptime 功能项使用 2000 ms（不作为延迟预算），旧时钟专用 deadline
测试仍保留原约束。最初日志保留在 `build/rpc-m2-samples.1` / `build/rpc-m2-samples`。

## H1 交接（未执行）

独立样例：[samples/zephyr/rpc_usability](../samples/zephyr/rpc_usability/README.md)。
使用 dm_mc02/stm32h723xx、RTT 输出、同 owner 双端 loopback，无执行器或持久写入；
前半为确定性模拟时钟，末尾独立验证真实 Zephyr uptime。

本轮只交叉编译并运行模拟器，不 SSH、不烧录、不要求现在按 RESET。
下一步在 H7 上分别运行一槽/四槽镜像，保存启动记录、计数和 `RPC_H1 ALL PASS`。
这只验收目标 CPU 的功能和所有权；H2/H3 的等待器、分配器、CPU 测量及产品物理链路仍未做。
M3–M5 未开始，libflorid/Ragtime 产品依赖、main、tag 和长期测试均未改动。
