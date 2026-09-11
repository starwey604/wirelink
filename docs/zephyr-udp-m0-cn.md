# Zephyr UDP：M0 socket 合同与验证

> 本页记录 M0 提交 `74d9503` 的范围和结果。当前公开适配器及后续发现见
> [M1 接入与验证](zephyr-udp-cn.md)，包括已复现的上游发送分配等待。

M0 用来回答：Zephyr 原生 socket 能否提供下一批适配器需要的等待、收发和退出语义？
本批实现位于 [`adapters/zephyr/udp/src/`](../adapters/zephyr/udp/src/)，
是**仅供测试编译的私有基础层**，不是已经可以接入生成端点的公开 UDP adapter。
不改 C11 核心、WLC、生成 ABI、线上帧格式或产品依赖；不创建后台接收线程。

## 1. 单一 owner 与联合等待

一个 owner 线程负责 open、receive、send、wait、close。其他业务线程只允许
`notify` 或 `request_stop`；它们不运行 Wirelink 状态机和业务回调。
对象使用 `WL_UDP_SOCKET_INITIALIZER` 初始化，不能复制或修改活跃对象的内部字段。

- 使用 `zsock_poll` 同时等待 UDP 可读和一个非阻塞 `zvfs_eventfd`。
- 通知先于 wait 到达也会保留；一次读取清除此前累计的通知，之后的新通知继续保留。
  通知表示“请再检查工作”，不是消息计数，也不代替业务队列的同步。
- 不循环清空通知，以免持续生产者让一次 wait 无法结束；目前每次 notify 仍写一次
  eventfd，尚未做“每一批只写一次”的原子合并优化。
- 等待超时为调用者给出的毫秒上限：0 仅检查，`UINT32_MAX` 无限等待；超过 `INT_MAX`
  的其他值提前截到 `INT_MAX`。返回 `WL_OK` 仅表示可能有工作，不消费 UDP 包。
  `WL_ERR_NO_DATA` 表示本次没有就绪工作，也包含被中断的等待。
- `receive_ready=false` 可以暂时屏蔽 socket 可读。当 core 接收槽已满时，避免同一个
  未消费包让 poll 立即返回、形成空转；业务通知和超时仍有效。

这里有一个明确限制：当前 Zephyr 的 eventfd write/read 内部使用 `k_mutex`，
**不是无锁，也不能从 ISR 调用**。私有入口会在进入 socket/eventfd 前拒绝 ISR 操作。
需要 ISR 触发时，应先唤醒业务线程；是否提供另一种桥接机制留给后续适配器设计。

## 2. 数据报与接收内存

本批仅支持数值 IPv4、固定对端 IP/端口；本地端口可设为 0 由系统选择。
不处理 DHCP、DNS、发现、多对端路由、IPv6 或 COBS 回退。
上层采用 native packet：一条 UDP 数据报承载一个完整 Wirelink 帧。

接收直接写入 core unit queue 已 claim 的槽，不经过适配器暂存再复制。
每个槽必须容纳“最大完整帧长度 + 1 字节”；这一字节用于识别截断，**不是线上字段**。

- `recvmsg(MSG_DONTWAIT)` 每次最多消费一条数据报。
- 超过帧上限，或带 `MSG_TRUNC`，返回 `WL_ERR_FRAME_TOO_LONG`；绝不提交截断前缀。
- 空包、来源 IP 或端口不匹配返回 `WL_ERR_BAD_FRAME`。
- 暂时无包返回 `WL_ERR_NO_DATA`。容量参数非法时不读取 socket。
- 只有 `WL_OK` 才发布长度并允许 commit；其他结果长度为 0，调用者必须 abort claim。
  丢弃的非法包也必须计入未来 service 的处理预算，防止垃圾流量独占 owner。

来源过滤不是认证，CRC 也不是认证。`65507` 只是本层 IPv4 UDP 长度的绝对上限，
不保证路径 MTU；正式 adapter 必须另外约束完整帧长度和避免 IP 分片。
“直接写入 claim”只消除了适配器额外复制，不代表网卡 DMA 到业务 value 的端到端零拷贝。

## 3. 发送与背压

发送使用 `zsock_sendto(MSG_DONTWAIT)`，不保留调用者缓冲区，不创建第二个发送队列。

| socket 结果 | Wirelink sink 结果 | 含义 |
| --- | --- | --- |
| 完整数据报被接受 | `WL_SINK_SENT` | socket 已接受，不是 DMA 完成或对端收到 |
| `EAGAIN` / `EWOULDBLOCK` / `ENOBUFS` / `ENOMEM` / `EINTR` | `WL_SINK_BUSY` | 本次未报告完成，可由上层有界重试 |
| 短写、非法长度、其他错误 | `WL_SINK_FAILED` | 不能报告整包成功 |

临时发送失败**不靠持续监听 UDP POLLOUT 重试**：可写不一定代表 net_pkt/net_buf 或
驱动描述符有空闲。M1 需给背压增加有界重试截止时间，和端点现有时钟/hint 合并，
不另建协议时间源、不强制平时每毫秒空轮询，也不等待凑批。

`MSG_DONTWAIT` 不能证明整个网络栈和驱动不会等待：内部锁、网络线程和 DMA 描述符
仍可能影响返回时间。因此，本批既不承诺硬实时，也不据此承诺无锁或固定 CPU 上界。

## 4. 停止、失败回滚与重开

`request_stop` 锁存停止状态并唤醒无限等待，重复请求允许。
停止后 wait/receive/notify 返回 `WL_ERR_CANCELLED`，send 返回 `WL_SINK_FAILED`。

关闭顺序是：请求停止 → owner 退出工作 → 停止并 join 所有通知生产者 → owner close。
禁止其他线程并发 close；也不能在生产者仍可能使用旧地址时复用对象。
close 可重复执行；之后 notify 返回 `WL_ERR_NOT_INITIALIZED`。
重新 open 清除停止状态，并创建新的 eventfd，不继承旧通知。
这不是允许旧生产者跨重开访问的“代际保护”。

open 仅在 socket、bind、eventfd 均成功后发布状态；中途失败释放已取得资源。
重复 open 拒绝且不修改原连接。适配器本身不调用 malloc；socket/eventfd 和网络包
仍消耗 Zephyr 配置的有限资源池，不能宣称整条链路没有分配行为。

## 5. 验证与复现

测试位于 [`udp_socket_contract`](../tests/zephyr/integration/udp_socket_contract/)，
运行在 Zephyr 自己的 IPv4/UDP loopback 上；不是 Linux socket offload，不需要网卡、
TAP、root 权限或开发板。测试启用的随机源仅供模拟测试，不能用于产品会话随机数。

每个平台运行 18 个新增用例，覆盖：

- 超时、通知先到和计数器饱和、200 次 wait 入口交错、4 个生产者与读取竞争、停止唤醒。
- socket 与通知同时就绪、接收槽满时屏蔽可读、ISR 拒绝。
- 空包、错误 IP/端口、边界长度、超长及截断、写入边界 canary。
- 实际 socket 发送、TX packet 池耗尽时连续 20 次 BUSY 及释放后恢复、错误分类。
- 直接 claim/commit/abort、两槽饱和与恢复。
- bind 失败、真实 eventfd 池耗尽后的重复失败回滚、32 次关闭重开无残留。

TX 背压已通过真实 Zephyr packet 池耗尽触发；其余错误分类通过纯函数输入补齐，
并非声称所有错误都从实际网卡触发过。
通知测试验证线程交错，不代替未来 SMP 平台和真实硬件压力验证。

在初始化的 Zephyr workspace 中运行（把路径替换为实际 Wirelink 位置）：

```sh
west twister \
  -T /path/to/wirelink/tests/zephyr/integration/udp_socket_contract \
  -T /path/to/wirelink/tests/zephyr/integration/waiter \
  -T /path/to/wirelink/tests/zephyr/integration/protocol \
  -p native_sim -p native_sim/native/64 -p qemu_cortex_m3 \
  -j 2 --inline-logs --outdir /path/to/wirelink/build/udp-m0-matrix
```

2026-09-11 本地验证使用 Zephyr `e4e6910cc19b7f11eada127e54c0b5248f413799`
（4.4.99）、Zephyr SDK 1.0.1。原生 socket 类型采用该版本的 `net_*` 前缀，
不依赖 POSIX socket 名称兼容层。仓库 manifest 的 v4.4.0 也具有这些声明，
但这次运行结果只代表上述实际构建版本，不扩大为所有 Zephyr 版本支持承诺。

最终矩阵：7/7 个构建运行配置通过，76/76 次用例执行通过，无编译警告。

| 平台 | 新增 socket 合同 | 既有 protocol / waiter | 结果 |
| --- | --- | --- | --- |
| native_sim（32 位） | 18 | 8 / 3 | 全部通过 |
| native_sim/native/64 | 18 | 未选中（既有套件平台过滤） | 全部通过 |
| qemu_cortex_m3 | 18 | 8 / 3 | 全部通过 |

完整矩阵结果见本地 `build/udp-m0-matrix/twister.json` 和 `twister.log`。
没有运行性能 benchmark，没有 H5 Ethernet/DMA 测量，也没有烧录或更新 Willow。

## 6. 进入下一批的边界

M0 验证联合等待和直接 claim 接收路线可行。M1 再接入公开 adapter、端点生命周期、
静态存储装配、错误统计、有预算的 service 和背压截止时间，并验证 RPC/遥测混合流量。
业务代码不应照着本文件自行组装所有私有入口。

后续 USB/UDP 切换需走停止/重建连接的明确流程，处理未完成 RPC、旧包和会话身份，
不能只替换 socket 就沿用所有事务。H5 硬件上另测网络线程优先级、TX 路径等待、
控制线程受扰、CPU 时间和遥测数据年龄，再决定是否需要比 socket 更深的定点优化。
