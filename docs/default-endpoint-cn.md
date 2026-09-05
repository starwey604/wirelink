# 默认端点：设计与边界

状态：内部开发，生成 ABI 20。既有映射 RPC 与 codec 字节不变；新增托管 RPC
使用独立的元数据前缀。本轮不发布新包。
[English](default-endpoint.md)。入门使用见 [getting-started-cn.md](getting-started-cn.md)。

## 应用只组装业务对象

WLC 为具有有限消息上限的 profile 生成 `*_endpoint_t`。声明一个零初始化的
静态对象，调用 `endpoint_init()`，连接适配器，就能使用类型化的发送、读取、RPC
和 `endpoint_step()`。应用不再定义缓冲区容器或分别初始化 runtime、arena、pump。

| 层次 | 负责内容 | 对外入口 |
| --- | --- | --- |
| Wirelink 通用端点 | 连接状态、owner hooks、每轮推进与关闭 | `wirelink/endpoint.h`，主要供生成器和适配器 |
| WLC 默认端点 | 按消息/使用配置推导存储，组合 runtime，类型化操作 | `*_endpoint_t` 及 `*_endpoint_*()` |
| 适配器 | 驱动生命周期、输入发布、发送完成与唤醒 | `endpoint_handle()` → `wl_endpoint_link()` / `attach()` |
| 业务代码 | 消息内容、执行结果、时钟和运行调度 | send/read/RPC/step |

`private_state` 和通用端点中 `private_*` 成员只用于静态布局，不是可修改的应用契约。
公开类型使静态分配成为可能，不要求用户了解成员之间的指针关系。

## 存储推导与默认值

托管 RPC 的业务消息不含内部编号／状态字段。生成的 `*_call_t` 配合
`endpoint_*_call/inspect/release/cancel` 使用，查询直接得到类型化结果；
服务端 `complete/reject` 接收可以复制的回复 token。句柄与 token 检查端点归属及生命周期，
用户无需读取内部编号。旧显式字段映射保留为兼容模式，详见 [RPC 合同](rpc-runtime-cn.md)。

payload 上限取 profile 中 LATEST/FIFO 消息及 RPC 请求/响应的最大编码上限，
包含托管 RPC 的 12 字节前缀，最小为 1。
无关的大消息不计入。默认初始化使用 native-packet 和 CRC32C；静态缓冲区也预留
其他支持封装的最坏开销以及一包容量的串口接收缓冲区，切换封装不必重新猜数组大小。
这会给只用 packet 的端点保留少量未使用的 stream 存储；追求最小内存的部署可用高级组装。

默认 runtime 使用已有的有限槽位布局：FIFO 一槽、RPC 一客户端槽、一待处理服务端槽、
一缓存槽。RPC 角色默认关闭，不猜测产品重试/过期策略。通过
`endpoint_config_defaults()` 获得初始化参数，修改 `config.link`、`config.runtime`、
`event_budget`、`on_result`、`user_data` 后调用 `endpoint_init_config()`。
初始化参数可以是临时对象，回调上下文必须在使用期间有效。

选中消息无界或超过当前单帧 2048 字节能力时，`*_HAS_DEFAULT_ENDPOINT=0`，
不生成一个假装足够大的对象。用消息长度约束收敛定义，或继续使用高级外部存储接口。
已有的 codec/runtime 构建目标仍分开，多个命名端点可共享一份 codec。

## 执行、错误和关闭

端点不创建线程、时钟或堆。`endpoint_step(now_ms)` 在一个 owner 上执行一轮，默认预算
为 16 个事件；已连接适配器的 service、事件释放、发送终态回收和 RPC 推进都在其中。
返回成功不等于业务请求已经完成，应使用 RPC inspect；没有工作也是正常成功。
可靠发送失败与应用分发错误会向上返回，不会伪装成空闲。

`endpoint_result()` 保存本轮首个 runtime 错误，后续成功事件不覆盖它；下一轮重新开始。
core/service 的详细结果通过 `wl_endpoint_last_step(endpoint_handle(...))` 查询。
需要每个消息结果或会话变化通知时配置 `on_result`；正常的非 RPC 发送成功不通知成错误。
无确认的传输仍不承诺对端送达。

首次使用必须零初始化。已初始化时再次 init 返回错误，不能隐式清掉正在使用的状态。
`endpoint_close()` 停止已附加适配器并使端点失效，可以重复调用；随后允许重新初始化。
调用前应归还高级路径借出的视图、结束业务对 runtime 存储的使用。
连接、端点、runtime 存储均不能在活跃时复制/移动。

loopback 的 connect 自动连接两端并安装 service/close/hint。两端共享同一 owner；
关闭任意一端会停止整条模拟连接，必须关闭两端后才能释放 cable。
其他硬件适配器保留现有驱动入口，由集成代码安装适配器 hooks。
直接绕过 attach 绑定驱动时，调用方仍需自己停止驱动，close 无法替未知驱动释放资源。

## 复制与高级入口

`endpoint_read_*()` 适用于可保留的、无借用指针的消息，复制一次到调用方变量，
内部 acquire/release；无新值时不修改输出。需要免复制时通过 `endpoint_runtime()`
使用原有的借用 API。发送仍走原有的直接编码路径，不新增整包复制。

默认推进自动回收可靠发送终态。不要再手动 take 返回的 TX handle。
需要独占管理这些句柄、自定义存储位置、更多队列容量或手动事件分发时，
采用高级 link/runtime 组装，而不是修改端点内部字段。

## 验证范围

新增生成器用例用真实核心验证 LATEST 合并、可靠 FIFO、RPC 完成、
多 runtime 共用 codec、所有封装/校验组合的初始化容量、关闭/重开、非法配置、
未知路由后跟有效消息时的首错保留，以及可靠消息超时。
C11/C++20 头文件检查和已有 Cortex-M runtime 体积门限保留。
Zephyr pump 单测覆盖通用端点生命周期及适配器 service 错误传播。
实机验证仍待后续连接开发板。
