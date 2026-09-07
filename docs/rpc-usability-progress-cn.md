# RPC 易用性演进实施记录

2026-09-07；基线 Wirelink `d245cbe` / WLC `b5c444a` / 生成 ABI 21。
M0–M2 实现目标在 H7 功能验证开始前结束；用户随后单独授权 H1 实板验证。
H1 已完成：一槽/四槽首次运行、系统复位重跑和断电冷启动均通过。
没有发布、main 合并或产品迁移。

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

## M2：默认 RPC（完成）

WLC `26a07a49597cd06b455ea1060e5b7902d39ea061` / ABI 23 已推 dev；
[WLC CI](https://github.com/starwey604/wlc/actions/runs/34049280583) 全部通过，
含 Rust/C、Windows 和两种 macOS 主机 smoke。
其核心测试依赖为 Wirelink `6ea75b7`；配对 Wirelink 实现为
`ac9bd480ab9fcd82df1ace860b0f12c8467156ce`。
[Host CI](https://github.com/starwey604/wirelink/actions/runs/34049458449) 全部通过，
包括三平台 core/adapter、安装包 ABI、Sanitizer 与 fuzz；
[Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34049458442) 也全部通过，
包括核心矩阵、生成时钟/所有权样例，以及 ESP32-S3 USB sample 构建。

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
- 独立 H1/时钟样例最终矩阵：11 配置、4 平台全部通过，无警告，记录
  `build/rpc-m2-samples-final/twister.json`；H1 一槽/四槽的两个 H7 ELF 均构建成功。
  完整中英文 RPC 文章中的 C 代码与实际 client/server 文件逐段比对一致。
  此软件检查点未进行 H7 执行；后续独立授权的实板记录见文末。

### 内存与复制边界

| 对象 / 平台 | 1 槽 endpoint | 4 槽 endpoint |
| --- | ---: | ---: |
| 字符串 Execute + 2031-byte Download，Linux x86_64 | 16832 B | 30160 B |
| 同一压力 profile，H7 交叉编译 | 16536 B | 29824 B |

纯 Add 教程在 x86_64 默认四槽为 3536 B；大消息测试不能代表所有 RPC 的 RAM 成本。
H7 四槽完整双端样例链接报告 Flash 70244 B、RAM 78592 B；一槽为 Flash 70100 B、RAM 52096 B。
这些包含 Zephyr、RTT、两个端点和测试保存值，
不是 Wirelink 库本体大小，也不是实际栈高水位。外部板定义有两个既有 `ragtime` vendor-prefix
提示，未修改产品板定义；C 编译通过。

请求有一次队列编码及一次向链路 payload 的复制；响应先保存在 core RPC 存储，再解码为自持值。
应用若长期保存，再做一次显式结构体赋值。不能把所有权简化宣传为零复制。
普通提交在 step 外取时一次，双端一轮各一次，回调内续调不额外取时，close/result 不取时。
H7 CPU 周期、实际延迟和栈高水位仍未测量。

64-KiB RAM 的 Cortex-M3 放不下该双端四槽大消息压力样例，最初链接超过 RAM 7816 B；
没有扩大模拟硬件 RAM。矩阵改为 M3 验证一槽，其他三个平台验证一/四槽。
高并发本机构建期间 x86_64 QEMU 曾在 BIOS 超时，另一项在真实 uptime 阶段未得到预期成功；
后者原功能项期限为 100 ms，日志未记录具体失败状态，不能仅据此确定根因。
最终矩阵降低并发，H1 uptime 功能项使用 2000 ms（不作为延迟预算），旧时钟专用 deadline
测试仍保留原约束。最初日志保留在 `build/rpc-m2-samples.1` / `build/rpc-m2-samples`。

## H1 实板验证（2026-09-07）

独立样例：[samples/zephyr/rpc_usability](../samples/zephyr/rpc_usability/README.md)。
使用 dm_mc02/stm32h723xx、RTT 输出、同 owner 双端 loopback，无执行器或持久写入；
前半为确定性模拟时钟，末尾独立验证真实 Zephyr uptime。

构建使用 Zephyr `v4.4.0-11610-gbd8c15382376`、SDK 1.0.1/GCC 14.3.0，
板配置 550 MHz、I/D cache 开启、速度优化、main stack 8192 B；这些是构建配置而非板上测量。
验证 ELF（SHA-256，本地与 Windows 一致）：

- 四槽 `build/rpc-m2-h7/zephyr/zephyr.elf`：
  `a8f962c4c9fb936244a518f054a0ca54d26f0182c72b1900fa087570e94a7bfd`
- 一槽 `build/rpc-m2-h7-one/zephyr/zephyr.elf`：
  `bc1938818c92d0f0c70c82ecf238f20fff7dcbedc500a4d5f80199cfa548da48`

用户在原目标结束后授权实板执行。已通过 Windows/J-Link 直接烧录两种容量，
各完成首次运行和一次系统复位重跑，四次均有完整启动记录及 `RPC_H1 ALL PASS`。
一槽每次 129 个完成通知 / 127 次 Execute handler；四槽为 135 / 130，均符合预期。
本轮没有修改实现；完整结果及原始证据见 [H1 验证记录](rpc-h1-h7-validation-cn.md)。
四槽经用户断电上电后的无复位采集也通过。换烧一槽时连接失败，一次 USB 设备绑定重启
未恢复连接；用户断电上电后连接恢复，已成功换烧一槽并再次运行通过。
用户再次断电上电后，一槽的无复位采集也通过。共保存七份通过记录（含额外的一次一槽重烧运行），
H1 清单无剩余项。当前保留一槽 ABI 23 独立测试镜像，调试器已退出。
这只验收目标 CPU 的功能和所有权；H2/H3 的等待器、分配器、CPU 测量及产品物理链路仍未做。
以上为 H1 完成时的边界；用户随后授权继续 M3–M5，进展见下节。

M0–M2 原实现目标在实板执行前结束；随后单独授权的 H1 实板功能验收也已完成。
上述远端 CI 对应实现提交 `ac9bd48`；H1 仅补充文档证据，没有修改已验收代码。

## M3：同步等待与单 owner 代理（ABI 24）

实现 `endpoint_<service>_sync`，复用有界 async 提交、绝对截止时间及唯一完成；
响应直接复制到调用者的自持值，返回后没有引用调用栈的通知。等待失败只结束当前调用。
新增 `local_error` 区分本地提交/等待失败，未改变线上格式。

UDP endpoint 接入自动提供 readiness waiter；新增锁存 notify，使先通知后等待不丢事件。
host Executor 绑定生成的 driver，在单 owner 上提交/step/close。业务线程经固定八位代理队列
阻塞等待，原超时包含排队；过期且未发出的请求不执行远端 handler。
关闭通知所有在途/排队调用，并排空 producer/latest 队列。同 owner 回调同步调用拒绝重入。
后台入队需要线程安全的同一时钟，不另设墙钟 RPC 计时器。

Zephyr 新增可选静态信号量 waiter：ISR 可 notify，stop 锁存并唤醒无限等待，
无内部线程/heap。同期修复安装包遗漏普通 RPC 头文件，加入真实生成消费者。
RPC 教程改为同步主路径，异步客户端单独保留；中英文[平台文档](rpc-platform-cn.md)列出合同。

本地软件验证（2026-09-07，未拿主机数据替代 H7）：

- `build/rpc-m3-host`：11 个 CTest 全通过，每项重复五次；包括 UDP 故障注入、
  C/C++/Python bridge、多业务线程、队列满、排队超时/回绕、在途 stop/IO 错误及 idle 不轮询。
- `build/rpc-m3-twister`：五个平台 49/49 配置、298/298 用例通过，无警告。
- `build/rpc-m3-waiter`：native_sim、M3、RV32、x86_64 四配置 12 个等待器用例通过。
- Clang ASan/UBSan：10 个原生 CTest 通过；Python bridge 单独预加载同版 ASan 后通过
  （Python 宿主不做 leak 检查，原生测试启用）。
- `build/rpc-m3-package`：安装包三个生成 C 消费测试通过。
- `build/rpc-m3-tsan`：UDP 锁存唤醒和 RPC executor 并发用例通过 Clang ThreadSanitizer。

- 完整 WLC 115 项通过；fmt/clippy 通过。针对性生成 C ASan/UBSan 覆盖
  async/sync 八种容量×delivery、managed RPC 四种 delivery、自持值与缓存基线，全部通过。

M3 实现配对：Wirelink `39316f3`，消费者锁定提交 `a225fa8` / WLC `ddbddef`（ABI 24）。
远端 [Host CI](https://github.com/starwey604/wirelink/actions/runs/34086113328)、
[Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34086113324) 和
[WLC CI](https://github.com/starwey604/wlc/actions/runs/34086082342) 全部通过，M3 完成。
Wirelink 覆盖三种桌面 OS、安装包、UDP、FFI、Astrial、Sanitizer、fuzz、Twister 和 ESP32-S3 构建。
M4 随后实现（本地门槛后与最后远端任务重叠，未提前进行实板）；H2、M5/H3 尚未做。
libflorid/Ragtime 产品依赖、main、tag 和长期测试未改动。

## M4：可选创建层（ABI 25，软件与 H2 验收完成）

生成 `endpoint_create(&pointer, &config, &allocator)` / `endpoint_destroy(&pointer)`：
创建一次申请完整端点，失败回滚；关闭/quiesce/完成通知后才配对释放。静态入口保留，
所有端点共享重入保护，分配式 close/reinit 保留分配器所有权。无全局 allocator、
逐消息分配或隐式 heap 回退；context 生命周期、旧别名失效和后台先 stop/join 明确写入合同。
可选 `Wirelink::storage` / `CONFIG_WIRELINK_STORAGE` 提供最多 64 块的单 owner 固定池。
中英文[存储文档](endpoint-storage-cn.md)及 C/C++/Python 消费实例已补充。

本地门槛已通过：

- WLC 115 项、fmt/clippy；静态和分配式端点重跑一/四槽×四种 delivery 的 sync/async 测试。
  覆盖 misalignment、NULL 分配、初始化阶段失败、池耗尽、描述符复制、回调销毁拒绝及 close/reinit。
- `build/rpc-m4-twister`：50/50 配置、301/301 用例通过，无编译器警告。
- `build/rpc-m4-host`：13 个 CTest 通过；其中 C++/Python 各 2000 次同步调用，
  初始化两次分配，热路径零端点分配器调用。后台在途 stop/IO 错误后 join/destroy 通过。
- `build/rpc-m4-sanitize`：11 个原生 ASan/UBSan 用例；两个 Python 用例预加载 ASan 后通过。
  WLC 针对性生成 C Sanitizer 全通过，覆盖大值、取消、关闭及池释放后的调用栈清理。
- `build/rpc-m4-package`：四个安装包消费者通过，包括已安装的固定池和真实生成 create/destroy。
- `build/rpc-m4-tsan`：UDP 通知、分配式后台 RPC stop/IO 错误后 join/destroy、
  C++ 固定池消费者三项通过 ThreadSanitizer。

独立样例首轮矩阵 `build/rpc-m4-samples` 16/17 通过，包含全部六项 H2 和七项 H1；
旧时钟样例的 x86_64 项超时。诊断显示强制休眠插入立即可处理的 loopback 工作之间，
QEMU 上短 RPC 预算耗尽；改为先排空立即工作，未增加超时/重试预算。
修正后 `build/rpc-m4-clock-final` 四个平台 4/4 通过。
最终 H2 源码在 `build/rpc-m4-h2-final` 一/四槽、三个模拟平台 6/6 通过。
macOS CI 另发现测试桥接函数 `wait` 与 POSIX 声明冲突，改名后该平台通过。

M4 配对为 Wirelink `3df748826ad3a3b0dbdf642b68fe98221310343d` / WLC `afa5dfd`。
远端 [Host CI](https://github.com/starwey604/wirelink/actions/runs/34088879031)、
[Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34088879037) 和
[WLC CI](https://github.com/starwey604/wlc/actions/runs/34088018415) 全部通过。
远端核心矩阵 50/50、301/301，完整生成样例矩阵 17/17 通过，无警告。
H2 样例在 `samples/zephyr/rpc_platform`，一槽/四槽 H7 已构建，开始独立实板验证。
IRQ-off CPU 探针统一使用 DWT，IRQ-on 往返使用包含休眠的系统周期钟，不混用两个计数器。
实板证据与剩余项另见 [H2 验证记录](rpc-h2-h7-validation-cn.md)，不把软件通过当作 H2 完成。
14:11 两次尝试无法 attach H7 CPU，中间重启一次 J-Link 设备绑定；14:15 按住 RESET
时仍停机超时。用户松开 RESET 后，14:16 正常连接恢复，一/四槽首次运行与系统复位重跑
四次全部通过，H2 完成。无实现修改，当前保留四槽独立测试固件。
H7 idle 为 1275/1358 周期（约 2.32/2.47 μs），2031 B 自持值解码 4038/3853 周期，
RAM 双任务 RPC p50 为 35559/36630 周期（约 64.65/66.60 μs，含调度）。
热路径端点分配器调用为零，关闭后池占用为零；主/服务线程未用栈 6236/2732 B。
这些不是产品 USB/UART 延迟，也没有同方法旧 ABI 基线；完整测量条件和捕获哈希见 H2 记录。
WLC 实现/二进制配对固定为 `afa5dfd`；随后 `9314249` 只修正中英文 README 的旧 ABI 数字，
无编译器源码变化。指南链接指向此文档修正，构建及安装命令仍固定已验收实现 SHA。

### M5 迁移预检（尚未修改产品）

已 fetch libflorid main：`2863e03ee09bc108c0fb543c73104e268e33c05b`，
dev `37a1d89c3dc5be359ab44a8cc2e829c3f9df1d8d` 已包含它，没有需同步的 main 提交。
libflorid 的 acados/Dyn-Calib 本地改动、Ragtime dev 已有的八个未推提交及 main worktree 均保留。
FCI 仍为显式 operation/status 字段映射；升级编译器不等于切换托管 RPC，
后续须分清本地 API 迁移与产品线上合同变更，并成对测试主机/固件。
M5 的实际修改与 H3 仍在 H2 之后进行。

## M5：产品 dev 与可编译教程（软件完成，H3 部分通过）

H2 通过后，libflorid dev `aadcc56` 与 Ragtime dev `5098c5b` 固定到已验证的
Wirelink `3df7488` / WLC `afa5dfd` / ABI 25。FCI schema、字段编号和显式映射模式不变；
底层 codec 发送助手改用已有 owner pass 时间，不引入第二个时钟。
libflorid main 已包含，无需同步；长期测试构建、main/tag 均未修改。
Ragtime 原有八个未推提交未顺带推送，Windows 用 Git bundle + 独立 worktree 消费快照；
libflorid 的本地 acados/Dyn-Calib 改动及 Windows 原工作区保留。

- libflorid `build/rpc-m5-florid`：库和全部默认示例构建通过，4 项 CTest 各重复 3 次通过。
  `build/rpc-m5-florid-sanitize`：Clang ASan/UBSan 四项全过，原生 leak 检查开启。
- Ragtime 自身 Zephyr `577e42ad1878`：`build/rpc-m5-firmware-final` 的 ArmProtocol / upgrade
  native_sim 32/64 与 Cortex-M3 QEMU 共 4/4 配置、80/80 用例通过，无警告。
  初次编译遗漏发送时间参数的失败日志保留，修复后完整重跑，不将构建失败当作执行结果。
- 产品 H7 USB HIL 构建通过：Flash 187088 B，RAM 75976 B；未启用电机/CAN/持久化。
  Windows VS 18/MSVC 19.51 与 libusb 1.0.30 的独立 host HIL 构建通过。
  persistence 辅助目标仍有既有 Duration.hpp 数值转换警告，本次不运行持久化测试。
- 新增 `02_device_info` 双程序，展示字符串赋值、再次调用、清理原值和关闭端点后的保存结果。
  `01_rpc` 保留 sync/async 客户端，新增独立 deferred 服务端；三个组合跑成功、拒绝、
  请求 ACK 与首个响应同时丢失及黑洞，断言业务执行次数和实际丢包。
- Host Release 最终 16/16 项通过（含双语源码一致性检查）。安装包 4 项通过；
  ASan/UBSan 原生 13 项、预加载 ASan 的 Python 2 项通过。
  C++/Python 存储消费者各重复 20 次，每次 2000 RPC，共各 40000 次；
  配对释放、关闭后池回到基线、热路径零端点分配器调用的断言全部通过。
- 六篇中英文进阶文章分开讲字符串、async、deferred；完整 C 代码逐字匹配编译源文件。
  安装说明不再要求产品 HIL 的编译器位于嵌套 WLC worktree。

Wirelink 教程提交 `609ff8d` 的
[Host CI](https://github.com/starwey604/wirelink/actions/runs/34092154160) 与
[Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34092154052) 已全部通过，
含 Windows/macOS/Linux 新双进程测试与安装包；Zephyr 主套件 50 配置 / 301 用例、
生成示例 17 配置 / 17 用例，ESP32-S3 两配置仅构建。

H3 14:44 首次换烧失败，用户重新拔插/松开后，15:09 普通 USB HIL 成功烧录并校验。
真实 USB 对抗性会话重连通过；产品生命周期首轮第 50 次打开失败，补充 host USB
错误诊断后重跑 100/100 通过，首次故障根因仍未确定。诊断提交 Ragtime `07a2998`
不改变固件、API 或重试行为，仅经 bundle 同步到 Windows 独立工作区。
Astrial 底层启停/关闭 100/100 通过，但不含物理重枚举。

无调试进程的 3 × 10000 次命令—遥测回显均无错配，主机 CPU 7.57–7.88%；
严格性能仅 2/3 通过，第三轮 11 个遥测 gap 超过 0.1% 门槛，不能宣布 H3 完成。
J-Link 再 attach 仍失败，尚缺有效板端周期/最终窗口统计及物理重枚举验证。
两次长测的失败、已通过项、配对、日志哈希和下一步均保存在
[H3 产品物理链路记录](rpc-h3-product-validation-cn.md)。当前保留普通 HIL，无 main 合并或发布。
