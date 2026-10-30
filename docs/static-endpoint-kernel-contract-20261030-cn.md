# 静态端点内核：K0 合同与实施登记

日期：2026-10-30。对应 [执行计划](static-endpoint-kernel-refactor-plan-20261030-cn.md)。这是原型的约束，不表示 K1–K6 已通过。

## 基线和边界

旧 A 输入冻结在 `../wirelink-static-kernel-20261030/baseline/`，逐文件 SHA-256、Git HEAD 和 dirty 状态见 `input.json`。Wirelink HEAD 为 `a5f68008e885f344481c76c7a80a3cbc1839a66e`；WLC 独立仓库 HEAD 为 `c09c5f5`。A 包含既有 unit RX 修复及 `fix: validate TX commits against active claim capacity`，二者均不计入新内核收益。

`frozen/` 从归档解出，旧 A 和共享旧 host 仅使用此处源码及 `artifacts/old-wlc`，构建命令、ELF/bin/map/config 和 host 身份由 `tools/build_old.py` 登记。`platform.json` 登记 70 个 west 项目的 HEAD、manifest revision 和 dirty 文件；dirty 内容另行归档。生成器二进制和第三方输入也独立归档。已有研究目录只读，不作为本轮实测 A。

新代码先放 `experimental/static_kernel/`，单独 CMake 工程，不能链接旧 `wirelink` target。这个隔离只服务于 K1/K2；是否进入 K3–K5 由实测决定，不是产品的双引擎选择。暂不改默认安装包和未迁移的 adapter。

## 新合同版本与语义

内核合同版本 1；新生成部署合同和 profile 使用独立版本，旧配置必须显式迁移。保留托管 RPC 的 session、operation、状态元数据以及 managed tagged 私有业务 codec，普通 DATA 使用现有 native/NONE 格式。拒绝 reliable DATA/ACK，下一条普通 DATA 仍可接收。

保留：普通消息、双向 RPC、4 个非阻塞客户端调用、同步 typed handler、客户端 timeout/cancel、业务拒绝、明确本地错误、packet RX 借用、单 TX 物理租约、stop/close/reinit。

删除：replay/history/fingerprint/TTL、ARQ/ACK/retry、deferred reply token、服务端业务 deadline、managed/mapped 双模式和通信内核中的业务调度。旧声明必须报错，不能当作尽力交付配置继续生成。相同请求再次 admission 可以再次执行业务；取消仅结束本地等待。

## 对象与所有权

| 对象 | 写入者／借用者 | 回收条件 |
| --- | --- | --- |
| 2×2048 B RX 区域 | 单 I/O producer claim；owner borrow | owner release 后 producer 才可复用；abort 不发布数据 |
| RX claim | producer；实例 ID + 不回绕序号 | commit/abort 一次；容量以端口保存值为准 |
| 客户端表，4 槽 | owner；handle 含实例与 generation | 恰好一次 terminal callback 后逻辑槽失效 |
| 请求 TX 快照 | submit 成功前编码；端点持有 | 未提交且终态，或 sink 确认租约结束 |
| 响应 TX，至少 4 槽 | handler 前 reserve；生成代码直接编码 | BUSY 时保留；物理租约结束后释放 |
| 普通消息 TX | generated send 快照；端点持有 | 物理租约结束；不用 RPC 槽 |
| 单 TX lease | owner submit；I/O 仅发布 completion | consumed/drained 或确认 abort；timeout/cancel 不提前复用 |
| 解码 view 与结果 | owner；typed callback/handler 借用 | 回调返回立即失效；应用自行复制 |

RX 的 read/write cursor 使用 C11 acquire/release；producer 和 owner 各自串行。初始化／重新初始化只能在 I/O 已停止且没有借用／租约时执行。非零实例 ID 由外层提供，重建必须更换；core 不取随机数和系统时间。所有 claim、call 和 completion 的序号耗尽均显式报错，不回绕复用。

## 状态及重入

客户端：`free → snapshotted/queued → submitted/waiting → terminal → free`。与物理 TX 状态正交：`free → ready → leased → free`。终态后仍 leased 的字节必须保留；晚到响应不再触发 callback。

服务端：`RX held → reserve reply → decode → handler once → encode → ready → leased → free`。没有 reply 容量时保留 RX 并背压，不能先执行业务。BUSY 只重试提交已编码 frame。decode、encode、handler 错误有明确失败／拒绝路径，不生成伪成功。

提交不内联 terminal callback。callback 可提交和取消其他调用；取消通知稍后由 owner 发出，不嵌套覆盖结果 scratch。step 开始固定 slot generation 快照，callback 创建的新调用不参加本轮旧 deadline 遍历。递归 step/close 返回重入错误。

step 先处理 completion，再在 16 事件预算内公平推进准备 TX、RX 和到期调用。持续 telemetry 不得饿死回复或 deadline。BUSY 必须等待端口资源通知，不能无条件 runnable 自旋。外层负责 latched 通知和等待；owner 查询工作后等待时仍须核对最新活动，覆盖 notify-before-query、查询期间及等待前的发布。

stop 阻止新 RX claim/TX submit 并唤醒 owner；已取得 claim 可 abort，旧 TX completion 仍可结束物理租约。close 发出各 accepted call 的 CLOSED 一次，并等待 adapter 确认 I/O 停止；未确认前不能 reinit 或复用存储。旧实例 claim/completion/handle 明确拒绝。

## 七种消息容量登记

下面容量由冻结 WLC 的 codec plan 给出，不修改业务 schema。RPC 元数据正常最多 13 B，拒绝最多 17 B，native DATA header 4 B。

| ID／类型 | 业务编码上界 B | 最终 frame 上界 B |
| --- | ---: | ---: |
| 100 SetpointRequest，managed tagged | 18 | 35 |
| 101 SetpointResponse，managed tagged | 18 | 35 |
| 102 Sample，Schema-v1 | 20 | 24 |
| 103 StreamChunk，Schema-v1 | 1032 | 1036 |
| 104 Progress，Schema-v1 | 19 | 23 |
| 105 ControlRequest，managed tagged | 36 | 53 |
| 106 ControlResponse，managed tagged | 527 | 544 |

请求池按 4×53 B、响应池按 4×544 B、普通 TX 按实际最大 1036 B 规划；不把 RPC 池扩到 StreamChunk 大小。RX 仍为 4096 B，单 RX 请求／单 TX 在途、4 call、16 budget 不变。最终大小以生成对象、adapter、栈和所有应用存储的编译实测总和登记，不能只比较 kernel struct。原 A 的 replay/pending 与新 B 的已完成待发响应 admission 分别报告。

## K1 验证清单

先验证产品 packet 端口的 claim/abort、容量、防旧 token、异步 completion、BUSY、关闭和游标回绕，再接 WLC 真正生成的七消息路由。生成端点验证 4 槽乱序、双向调用、单次 handler、副作用前背压、terminal 唯一性、晚到响应、错误 frame 恢复、最大合法业务值和 close/reinit。采用 strict C11、GCC/Clang 和 ASan/UBSan；纯 core 场景同时提供 Ztest unit 入口。

K2 只有在软件端到端通过后才登记和采集。当前尚无本轮 A/B 性能结论；不扩大默认产品迁移。

## K0 构建封存

旧 A 的 CPU-off/on 和两份 host 已从上述冻结源码独立构建成功。摘要如下（完整命令和其余产物见证据目录的 `old-builds.json`、`hosts/o*/runner.json`）：

| 证据目录内路径 | SHA-256 |
| --- | --- |
| `baseline/input.json` | `150e14bc0a0a3293bfa5266455138220ebcd99be7449002dbea6c0276884962d` |
| `baseline/platform.json` | `6d1a31eccfb9b283340d39cf4ba0692a5acb037ecce31a36aa078376847ef91b` |
| `artifacts/old-wlc` | `a2b5f4dce014b2a0f00f91668716e338e62a009ff9b8a0ac53ee7bc5d41b23e2` |
| `devices/A-o0/zephyr/zephyr.bin` | `cf7b3d84b891ac4863dde038414a58621a28eaedc37183baf3df43134560d5ca` |
| `devices/A-o1/zephyr/zephyr.bin` | `faf17a00539fd0e79ca16c1cf4555b53a152a37563ec9fb466952f83c595227b` |
| `hosts/o0/usb_fs_wirelink` | `2d78cb1ee9871b8e3edfd8aa956d107ebb055aa1fa3ed668c775536400a1ad1f` |
| `hosts/o1/usb_fs_wirelink` | `b6c1bc2ae4a7ed6f1ddf56cb7a7883e42faed5c069f35c91624baf56ab4bf404` |
