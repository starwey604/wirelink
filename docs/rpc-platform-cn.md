# RPC 同步调用与平台等待

先读[加法教程](tutorial-rpc-cn.md)。这里面向接入平台、后台线程或自定义时钟的开发者。
[English](rpc-platform.md)。本接口从生成 ABI 24 引入，当前配对为 ABI 26；
只改变本地接口，不改变线上帧或 RPC 格式。

## 一个调用入口，两种执行方式

```c
wl_rpc_completion_t result = calculator_endpoint_add_sync(
    &client, &request, &response, 1500U);
```

没有绑定后台执行器时，调用线程就是端点的 owner：它执行生成的 `step`，
等待事件或最近截止时间，再执行下一轮。UDP 接入自动安装等待器，应用无需再配置。
其他平台初始化时用 `wl_endpoint_set_waiter(handle, &waiter)` 安装一次。
没有等待器立即返回 FAILED / `local_error=WL_ERR_NOT_SUPPORTED`，不会偷偷忙等。

等待器只等待，不解析消息、不调用业务、不推进端点。返回 WL_OK 或 NO_DATA 后重新检查状态；
其他错误终止等待并移除该调用的内部通知，返回后不再访问调用者栈，也不关闭其他调用。
成功才改写响应；保存的响应不借用端点内存。

## 已有后台线程：使用代理，不增加 owner

```cpp
wirelink::host::Executor executor; // 必须比端点和调用线程活得久
calculator_endpoint_t client{};
// 初始化 client，接入 UDP；clock 使用线程安全的 monotonic_clock()。
CHECK(executor.initialize(calculator_endpoint_driver(&client)) == WL_OK);
CHECK(executor.start() == WL_OK);
// 业务线程：仍调用 calculator_endpoint_add_sync(...)。
// 不在这些线程上调用 step、async、cancel 或 close。
executor.stop(); // 停止接收，owner 关闭端点，通知调用者，并 join owner。
// join 所有业务调用线程，然后才释放 endpoint / adapter / executor。
```

生成的同步入口自动使用代理。固定八个代理提交位置保存阻塞调用者的引用；
实际 RPC 容量仍由端点决定，满时返回 BUSY，不建立无限队列。
只有 owner 提交、推进、完成或关闭 RPC；同一 owner 回调中的 sync 返回 REENTRANT。
配置、handler、等待器和代理绑定在 start 前完成，运行期间不更换。
执行器与适配器必须保持存活直到停止及所有调用线程退出；不要并发销毁。
关闭后保留代理绑定，使尚未退出的调用线程得到 CANCELLED，而不是碰已关闭的端点状态。

## 一个时钟，一个截止时间

同步等待不再开始一个新 RPC 计时器：排队、发送和重传都沿用同一端点时钟。
超时须在 1…INT32_MAX 毫秒内；代理从入队起计时，过期但未发出的任务不发送。
平台等待参数是最多允许阻塞的相对毫秒数；UINT32_MAX 表示等待事件、不设定时唤醒。
通知必须锁存，先通知后等待也不能丢失；UDP 和 Zephyr 实现均遵守这一规则。

后台代理会在业务线程入队时读取同一个时钟，因此 provider 必须线程安全。
测试用手动时钟应使用原子存储；推进时间后 notify 唤醒 owner。
没有事件/截止时间时不固定每 1 ms 轮询。时钟停止、owner 被阻塞或调度延迟，
均可能延长真实墙钟等待；预算不是硬实时抢占保证。

## Zephyr 线程

启用 `CONFIG_WIRELINK_ZEPHYR_WAIT=y`，使用 `wirelink/zephyr/wait.h`：

```c
static wl_zephyr_waiter_t activity;
wl_zephyr_waiter_init(&activity);
wl_waiter_t waiter = wl_zephyr_waiter_descriptor(&activity);
wl_endpoint_set_waiter(calculator_endpoint_handle(&client), &waiter);
```

时钟可用 `wl_zephyr_monotonic_clock()`。收包/发送完成事件调用
`wl_zephyr_waiter_notify(&activity)`，允许从 ISR 通知；sync 只能在线程中执行。
`wl_zephyr_waiter_stop` 锁存停止并唤醒无限等待，返回 CANCELLED；随后由 owner 关闭端点。
等待器不创建任务、不申请堆内存。适配器自己的 DMA 缓冲区和 IRQ quiesce 仍归适配器。

## 结果与退出

`status` 是五种终态之一；`rejection` 只表示业务拒绝。
`local_error` 区分提交、等待或后台停止错误；`transport_error`、`runtime_error`、
`codec_error` 保留协议路径诊断，不借用业务拒绝码。正常超时不是本地平台错误。
取消/超时不撤销已经在远端发生的副作用。

owner 模式可以用等待器的 CANCELLED 实现停止；后台模式用 `requestStop()`，
再从非 owner 线程 `stop()`。不要在回调中同步销毁自己。
教程的阻塞客户端使用有限 1500 ms 预算；它不承诺 Ctrl+C 立即中断平台等待。
