# 默认端点：设计与边界

状态：内部开发，生成 ABI 30；不发布、不合并 main。托管 RPC 元数据使用 v2，
Compact-v1 帧格式和显式映射消息不变；两端须成对升级。
入门顺序：[安装](installation-cn.md) → [遥测](getting-started-cn.md) →
[RPC](tutorial-rpc-cn.md) → [集成](tutorial-integration-cn.md)。[English](default-endpoint.md)。

## 普通业务入口

WLC 为有界、单帧 profile 生成 `<runtime>_endpoint.h`。
声明一个零初始化且地址稳定的 `*_endpoint_t`，初始化时选择平台环境，自动生成 session，
连接适配器后使用 send/read、异步 RPC 和 step。不要访问 `private_state`。

该头文件因静态 C 布局而包含 runtime 头；它是推荐的阅读入口，不声称隐藏了所有
传递声明。手动端点 call/inspect/release、complete/reject 助手需显式包含
`<runtime>_advanced.h`；高级存储装配和借用视图位于 runtime/codec 头中。
只需要业务数据时包含独立的 `<codec>_values.h`，没有 runtime 装配依赖。

| 层次 | 责任 |
| --- | --- |
| 通用端点 | owner、时钟采样、适配器挂载、事件推进和关闭 |
| WLC 默认端点 | 静态存储推导、类型化业务转换、RPC 提交和通知 |
| 适配器 | 输入发布、发送完成、唤醒和停止驱动 |
| 应用 | 消息内容、handler、平台环境选择、调度时机 |

默认使用 `wl_platform_environment()`，自定义来源见[自动会话](session-cn.md)。

## 普通 RPC 合同

`endpoint_<service>_async(endpoint, request, timeout, callback, context, optional_call)`
在接受前编码请求快照；返回 `WL_OK` 表示接受，`WL_ERR_BUSY` 表示本地满载。
失败提交无回调。timeout 必须在 1..2³¹−1 毫秒内，包含排队等待，不在重试时重新计时。

框架先解码为自持结果、回收调用槽，再通知 callback；链路 TX 的借用和终态独立排空。
回调得到 `wl_rpc_completion_t`，仅成功时得到 typed response。
指针只在回调期间有效，结构体赋值则产生独立副本，无需 `release` 或析构。
有界 string/bytes 用 `length` 和内嵌 `data[]`，字符串长度是字节数。

`config.on_<service>` 注册即时 handler：接收 const request、填写 response，返回 0
表示成功，非零仅表示业务拒绝。普通 handler 和诊断共用 `config.user_data`；
非 NULL 的 `config.<service>_user_data` 可覆盖单个服务，NULL 表示继承。
这些上下文不承担框架回包职责。高级 handler 和单次完成回调仍显式指定上下文，不隐式继承。
需要慢任务时显式选择 `config.advanced.<service>_request_handler` 的延迟 token 模式；
同一个服务不能同时注册两种 handler。

## 随业务增加 RPC

RPC 定义只保留在一份共享 profile 中，通过 CMake `PROFILES` 或重复的 WLC `--profile`
与端侧路由组合。文件顺序不形成覆盖关系；重复声明和冲突直接报错。
`send Message` 生成发送助手并参与 payload 容量推导，不创建接收邮箱；
对端独立选择 `latest` 或 `fifo` 接收。

[12 个设备服务示例](../examples/03_device_service/README-cn.md)展示按业务模块注册 handler，
并实际验证新增第 13 个服务无需修改传输、主循环、构建规则或其他 handler。
ABI 27 保持 ABI 26 的 codec 和线上格式；生成消费者须使用配套 WLC 重新构建。

## 配置与 RAM

内存敏感的部署在端侧 profile 中声明一次
`endpoint { envelope = native_packet; rpc_role = server; }`。
WLC 移除不需要的传输缓冲和另一侧 RPC 存储，业务 handler 不加开关。
不声明时保留 `any` / `both`；见[布局配置与尺寸验收](endpoint-layout-cn.md)。

保留的 client 能力初始化时就绪；注册 handler 自动启用 server。常规代码无需 enable 角色。
默认 native-packet、CRC32C、ACK 等待 100 ms、最多重传 4 次、每轮事件预算 16。
这些是可覆盖的起点，不是所有设备/链路的最佳参数。

托管 RPC 默认四个 client/pending/cache 槽、pending 1000 ms、cache TTL 10000 ms，
FIFO 默认仍一槽。缓存策略 EVICT_OLDEST 仅淘汰已送达的响应，保护待处理、待发和在途结果。
TTL 是最长保留时间，不保证整个窗口不淘汰。严格保留使用
`config.advanced.rpc_server_cache_policy = WL_RPC_CACHE_REJECT_NEW`；
缓存不足在服务端诊断，客户端按原截止时间结束，不伪造业务拒绝或新的远端 BUSY 报文。

通过构建定义 `<PREFIX>_ENDPOINT_RPC_CAPACITY=1` 等缩小静态槽数；必须对所有使用
该端点的翻译单元一致设置，不能仅给某个 .c 定义。运行时 count 不得大于静态容量。
`config.advanced`、`config.link` 是专家覆盖入口，默认响应存储和队列不能无限扩张。

payload 上限包含托管 RPC 20 字节元数据；仅 profile 选中消息参与计算。
队列按最大请求而非最大响应预留；多个服务共用最大请求/响应暂存 union。
ABI 29 让有界 profile 的 runtime 解码暂存跨服务共用，规范化指纹直接计算，不再预留
规范化字节缓冲及容量配置。分发不能重入；延迟 handler 保存自持输入和 token，不能保存暂存区指针。
含无界消息的高级 runtime 保留逐服务解码对象，避免覆盖用户配置的 repeated backing。
可靠事务仍为单窗口；等待 ACK 时不再独占物理 DATA 发送资源。
近 2 KiB 响应会显著增大 endpoint，见[布局裁剪](endpoint-layout-cn.md)。
选中消息无界或超过单帧能力时 `HAS_DEFAULT_ENDPOINT=0`，应收敛 schema 或使用高级装配。

## 调度、诊断与关闭

端点没有堆或线程，core 不选择 OS 时钟。一轮 step 采样一次时钟，回调内提交复用它；
高级 runtime/link 仍显式传时间，不能并行驱动同一 owner。
step 成功只代表推进正常，不代表某个 RPC 已完成。
`endpoint_result()` 保存本轮首个分发/运行错误；详细推进信息见
`wl_endpoint_last_step(endpoint_handle(...))`。`config.on_result` 接收诊断，
每调用结果则走自己的 callback。缓存回复遇到普通 TX 背压会保留并重试。

回调允许提交或取消调用，不允许递归 step、同步 close 或重建本端点。
在 owner 安全点调用 `endpoint_close()`，它先停止适配器，再完成剩余通知。
返回后没有框架回调或已附加适配器的借用访问，可以重建；重复 close 安全。
直接用通用 `wl_endpoint_close(handle)` 会跳过生成层通知，
普通应用必须调用生成的 close，再释放适配器对象。不能移动活跃 endpoint。

loopback 的两端共享一个 owner，关闭任一端会停止连接；两端都关闭后才可释放 cable。
绕过 attach 私自绑定的驱动仍由集成者停止。高级借出视图须先归还。
取消和超时不撤销远端副作用，最近结果缓存也不是无限期 exactly-once 保证。

## 高级路径与验证

原 `call/inspect/release`、deferred token、value/view 转换保留供明确的高级需求，
不与普通异步调用混用同一次调用的资源。零复制大数据、定制 arena、手动事件分发使用高级装配。
LATEST/FIFO 当前仍限制可保留的无借用消息；本轮没有顺便扩展 IDL 或流式传输。

普通 `endpoint_<service>_sync()` 在 owner 线程推进/等待，绑定 host executor 后则通过代理提交。
UDP 自动安装等待方式；自定义平台初始化时提供 waiter，缺少时明确报错，不忙等。
异步仍为事件循环入口。时钟、停止和存活期见[平台接入](rpc-platform-cn.md)。

H1 证据及 M3/M4 软件验收见[实施记录](rpc-usability-progress-cn.md)。
[M4 分配器创建层](endpoint-storage-cn.md)沿用静态运行逻辑，软件检查和 H2 实板验证均已通过；
M5 产品迁移尚未做。
