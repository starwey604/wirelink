# Binding 第三轮：原生异步 RPC 与 asyncio SDK

2026-09-12。本轮在 WLC 自动生成 SDK 的基础上增加异步调用、取消和关闭语义。
Wirelink 与独立的 `wlc/` 仓库分别记录提交，使用本机既有 Git 身份。
本轮只在本地构建和提交，没有 push、发布 wheel 或创建 release。

## 已实现的接口

WLC 继续从同一份 schema/profile 生成完整 SDK。同步 `Client` 保留，另生成
C++ `<service>_async()` 和 Python `AsyncClient`。Binding source API 变为 **2**；
C core、原有 C 生成结果、wire format 和 codegen ABI **32** 均不变。
需要用配套的 Wirelink host 支持重新构建 SDK；不承诺跨编译器的稳定 C++ 二进制 ABI。

```python
import asyncio
from calculator_sdk import AsyncClient, Udp

async def main():
    async with AsyncClient.connect(Udp(peer=("127.0.0.1", 49101))) as client:
        results = await asyncio.gather(
            client.add(left=20, right=22, timeout=1.0),
            client.add(left=-10, right=52, timeout=1.0),
        )
    print([result.sum for result in results])  # [42, 42]

asyncio.run(main())
```

示例端口需替换为运行中的 C 测试服务端口。`connect()` 在当前事件循环中同步打开本地
连接，不等待对端握手；RPC 使用 `await`，关闭使用 `async with` 或 `await close()`。
每个 AsyncClient 绑定创建它的事件循环，跨循环调用会抛出明确的 RuntimeError。

```cpp
auto submitted = client.add_async({20, 22}, std::chrono::seconds(1));
if (submitted) {
  auto operation = std::move(submitted).value();
  auto result = operation.result(); // 等待并取得拥有数据的 Result<AddResponse>
  if (result) { /* result.value().sum == 42 */ }
} else {
  // submitted.error()：参数、连接状态或 host admission 错误
}
```

`Operation<T>` 可以复制，提供 `done()`、`wait()`、`wait_for()`、`result()` 和
`cancel()`。提交返回前已复制请求；`result()` 返回拥有数据的副本。丢弃操作句柄不会
取消已接纳工作；连接负责将它完成。保留句柄时，结果可以在连接销毁后读取。

## 所有权与调度

- Host executor 的同步和异步 RPC 共用 8 个接纳槽位；默认生成 endpoint 有 4 个
  RPC 槽位，因此可能更早报告容量耗尽。SDK 保留 BUSY/QUEUE_FULL 等原始诊断，统一映射
  为 `QueueFullError`。超时从 host 接纳时计时，包括排队时间。
- Executor 持有每个已接纳的原生任务，owner 线程执行提交、取消和完成。队列拒绝不会
  触发完成回调；接纳的任务只终结一次。任务释放和结果转换在接纳互斥锁之外执行。
- Session 用弱引用提供取消入口；操作对象不延长连接寿命。关闭先拒绝新调用，再停止并
  join owner，完成所有已接纳请求，然后回收 adapter。同步调用仍受 active-call 门控保护。
- 生成的 `src/endpoint.c` 负责 typed C callback、deadline 提交和取消；C++ 通过
  不透明指针桥接，并在 callback 返回前复制 response。C runtime 不进入 C++ namespace。

## Python 完成通知与关闭

每个 AsyncClient 使用一个可关闭的通知线程，与 RPC 数量无关。它等待原生 condition
variable，通过 `loop.call_soon_threadsafe` 安排结果分发。Python 消息转换和 Future
完成都发生在事件循环中，owner 线程只处理原生任务和原生信号。

Python 最多保留 8 个尚未分发完成的操作，包括已完成但事件循环尚未处理的结果，以及
已取消 Python Task 但原生完成尚未到达的操作。这样事件循环暂时繁忙时，结果和完成通知
不会持续无限增长。信号可以合并，注册前已完成的操作也会立即发出通知，不依赖轮询定时器。

完成信号使用独立的纯 C++ shared_ptr。nanobind 的 Python 引用不会进入 owner 所持任务，
最后一次释放也不会触碰 Python。通知线程只弱引用 AsyncClient，闲置且被遗忘的连接可以回收。
确定性关闭仍使用 `async with` / `await close()`。

关闭由现有通知线程执行 native close，不依赖 Python 默认线程池。原生 owner 停止后，
事件循环回收通知线程并分发剩余结果，再完成关闭 Future。多个关闭等待者共享这一过程，
取消其中一个等待者不会打断回收；默认线程池已关闭时同样可关闭 SDK。

| 情况 | Python 行为 |
| --- | --- |
| `task.cancel()` / 外部 `asyncio.wait_for` 取消 | 保持 asyncio 的标准取消语义，同时请求取消对应原生 RPC |
| RPC 自身 deadline 到期 | `RpcTimeoutError` |
| 连接关闭时尚未完成的已接纳 RPC | SDK 的 `CancelledError` |
| 关闭后提交 | `ClosedError` |
| Host、endpoint 或 Python 待分发容量耗尽 | `QueueFullError` |
| 业务拒绝、codec、transport 错误 | 沿用同步 SDK 的独立错误域和原始诊断 |

取消只作用于本地调用，不能撤销对端已经执行的业务效果。C++ `cancel()` 返回 true
表示已排入取消请求；若响应先完成，最终结果仍可能成功。超时、取消和响应竞争均保留一个终态。

## 本地验收

Linux x86_64，GCC / Clang，CPython 3.14；最终两套 wheel 已安装到工作区 `.venv`。

| 验收 | 结果 / 证据 |
| --- | --- |
| WLC 完整回归 | 156 项通过，`build/async-wlc-tests.log` |
| 最终 SDK / CLI 专项 | 8 + 13 项通过；增加 AsyncClient / async 方法 / C bridge 名称冲突诊断，编译真实 C/C++ façade；`build/async-wlc-final-sdk-tests.log` |
| Rust fmt / Clippy | 通过，all-targets / all-features / warnings as errors；`build/async-wlc-clippy.log` |
| WLC SDK ASan/UBSan | 8 项通过，`build/async-wlc-sdk-sanitizers.log` |
| 基础 host / adapter CTest | `build/bindings-core` 的 5 项通过 |
| Calculator C++ | 同步及异步测试通过；50 批、每批 4 个并发 RPC、80 轮 response/cancel/close 竞争、接纳队列满、排队超时、析构取消、结果独立存活 |
| Device C++ | 嵌套、bytes、presence/default、未知 enum 与异步 codec 错误通过 |
| 安装后的 C++ 消费 | `build/device-consumer` 同时消费两个 SDK，包含异步返回类型验证 |
| Clang ASan/UBSan | Calculator 同步/异步、Device 通过，开启 `UBSAN_OPTIONS=halt_on_error=1` |
| Clang TSan | Calculator 同步/异步通过，开启 `TSAN_OPTIONS=halt_on_error=1` |
| Python | 59 项通过，`build/async-python-tests.xml`；包括两个 AsyncClient 同时使用、取消/关闭、弱引用回收及默认线程池关闭 |
| 标准 sdist → wheel | 两套均从隔离环境提取的 sdist 构建；`build/async-final-*-build.log` |
| 干净环境离线 wheel | 移除编译工具 PATH，安装两套 wheel，运行同步和异步 C peer RPC；`build/async-clean-wheels.log` |
| 生成一致性 | 两个 SDK 共 68 个工件逐字一致；既有 generated C 文件无 diff |

复查入口：

```sh
.venv/bin/python tests/package/check_generated_sdks.py --wlc wlc/target/debug/wlc
ctest --test-dir build/bindings-sdk --output-on-failure
ctest --test-dir build/device-sdk --output-on-failure
WIRELINK_CALCULATOR_SERVER="$PWD/build/bindings-sdk/tests/calculator_c_server" \
WIRELINK_DEVICE_SERVER="$PWD/build/device-sdk/tests/device_c_server" \
  .venv/bin/python -m pytest examples/06_bindings/tests examples/07_device_sdk/tests -q
```

本轮尚未运行远程 Linux/macOS/Windows CI，没有执行硬件或 Zephyr 回归。
现有 binding CI 自动纳入新增的 CTest 和 Python 测试。WLC 的 CI/release 测试依赖配对
本轮 Wirelink 提交；推送时需先使该 Wirelink 提交在远程可用，再推送 WLC。
本地 Linux wheel 仍需 manylinux 构建/repair 才适合更广泛分发。

## 后续边界

本轮完成原计划 P3 中的异步 RPC 部分。类型化 latest/stream 订阅、消息背压策略、
Python 服务端、Serial/USB 和 Bulk 尚未包含在此 SDK 中。下一轮可以复用本轮的
owner 调度和有界完成通知基础，优先定义并实现遥测订阅的合并、丢弃计数和取消行为。
