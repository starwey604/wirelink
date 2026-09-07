# 初始化时配置一次时钟

Wirelink 需要知道“经过了多久”，才能决定何时重传未确认的数据、何时让 RPC 超时。
它不需要日历时间，两台设备的时钟也不必同步。ABI 21 将这个输入从日常调用移到了初始化。
[English](endpoint-clock.md)。

## 普通应用怎样使用

默认使用 `wl_platform_environment()`，其中包含时钟和自动身份来源。
主机链接 `Wirelink::platform`，Zephyr 启用 `CONFIG_WIRELINK_PLATFORM`。
已有产品时间源可仅覆盖环境中的时钟，保留自动身份：

```c
static wl_time_ms_t read_uptime(void *user) {
  (void)user;
  return k_uptime_get_32(); /* Zephyr；包含 <zephyr/kernel.h>。 */
}

static telemetry_endpoint_t endpoint;
wl_environment_t environment = wl_platform_environment(); /* wirelink/platform.h */
environment.clock = (wl_clock_t){read_uptime, NULL};
int result = telemetry_endpoint_init(&endpoint, environment);
```

需要详细配置时，在 `endpoint_config_defaults()` 后填写 `config.environment.clock`。
空时钟函数会被拒绝；裸机自定义完整环境见[自动会话](session-cn.md)。此后 `step(endpoint)`、
`*_async(..., timeout_ms, callback, context, optional_call)` 都不再传 `now_ms`；
即时 handler 直接填响应。高级 `call/complete/reject` 也复用端点时钟。
应用自己的发布频率仍由应用决定。

## 生命周期和调用成本

- 回调返回本机单调递增的毫秒数，按 32 位自然回绕。不要使用会被校时的日历时间，
  也不要在端点活跃时重置时间起点。计时间隔小于 2^31 毫秒，系统也应在这个
  可安全比较的时间窗口内继续观察、推进状态。
- 描述符会被复制；`user_data` 指向的对象要活到端点 close 完成。
  取时钟发生在通信 owner 上，不在接收中断中；回调必须短小、不阻塞、不重入，
  C++ 异常不能跨过 C 边界。
- 每轮 step 取一次时间，然后才处理适配器完成通知。handler 内立即回复复用该时间。
  handler 应尽快返回；异步工作完成后再回复，会取新的时间。step 外可靠提交也只取一次，
  同时用于链路和 RPC。无需先 step “刷新时钟”，提交也不会偷偷推进整个通信循环。
- step 外查询休眠提示会取一次时间，但不推进状态。inspect、release、读取保留值、
  初始化、关闭和 unreliable 类型化发送都不取时钟。没有全局时钟或运行中换时钟 API。

不分配堆内存的核心不调用操作系统时钟。可选 C++ executor 默认采用原生时钟，
也可在 `initialize()` 时覆盖；上下文必须活到 `stop()` 完成。

## 高级路径如何迁移

核心、生成代码和消费者一起重建，不要混用不同生成 ABI 的头文件或端点布局。
时钟注入于 ABI 21 引入，当时不改变编码；当前 ABI 26 还引入了
[自动会话和托管 RPC v2](session-cn.md)，托管 RPC 两端必须配套升级。

| 接口 | 当前用法 |
| --- | --- |
| 当前默认端点初始化 | `init(endpoint, environment)` 或填写 `config.environment.clock` |
| 默认 step、RPC、通用 hint | 去掉显式 `now_ms` |
| `wl_send_reliable`、`wl_tx_payload_commit` | 在 `out_handle` 前加 `now_ms` |
| codec binding 的 `*_send` | 在 `delivery` 后加 `now_ms`；unreliable 忽略它 |
| 高级 runtime、`wl_poll`、裸 pump | 保留显式时间，全部采用同一时钟来源和起点 |

不要同时让默认端点和另一个裸 pump 消费同一批事件、回收同一批发送句柄。
高级适配器在 owner 的 pump service 中上报异步完成，此时已设置本轮时间。
接收新 RPC 时也会回收已过期且已投递的响应缓存，空闲唤醒不必先 poll；
尚未完成投递的响应仍受保护，不会仅因经过 TTL 就被删除。

## bindings 和验证

[`tests/clock_bridge`](../tests/clock_bridge/) 用 C++ 和 Python `ctypes` 调用导出的
C 接口，里面由原生代码拥有生成端点。Python 选择原生时钟，或推进测试用的模拟时间，
不提供逐次取时间的 Python 回调。这是验证夹具，不是已发布的 Python SDK。

Release 教程构建中的 `wirelink_clock_benchmark` 对比裸 pump 空闲、默认端点的
模拟／原生时钟，以及 unreliable 类型化提交。记录编译器、CPU、实际耗时和进程 CPU 时间；
这些路径的工作量不同，不能把差值直接叫作函数指针开销，也不能当作端到端网络延迟。
取时钟次数另有明确断言。[独立 Zephyr 测试](../samples/zephyr/endpoint_clock/)
用于验证固件行为和空闲周期成本；进度见[演进记录](endpoint-clock-evolution.md)。
