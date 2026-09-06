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

## M1：自持值（本地软件通过，远端验收中）

WLC 检查点 `b2789461929de1c687bf562e8636f5ad343a15b3`，ABI 22，已推 dev；
远端 [WLC CI](https://github.com/starwey604/wlc/actions/runs/34046010688) 验收中。
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
远端 CI 仍在验收。

M2 将补上完成通知中槽位复用、close/reinit 后仍保留业务副本的端到端测试。

## M2 / H1

M2 未开始验收。尚未实现默认即时 handler、排队及自动完成、精简配置及示例迁移。
H1 尚未进行；最终将准备独立 H7 loopback 样例及清单后交回用户，不自动烧录。
