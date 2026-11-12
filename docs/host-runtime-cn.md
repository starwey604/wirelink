# Host owner 与 C++ Session

[English](host-runtime.md) · [SDK 例程](../examples/06_bindings/GUIDE.md)

使用 `wirelink::host::Executor` 在唯一 owner 线程上运行 C kernel，或使用 `wirelink::Session` 拥有 UDP 连接并取得类型化结果。Executor 需要 `WIRELINK_BUILD_HOST_RUNTIME=ON`；`WIRELINK_BUILD_CPP_BINDINGS=ON` 加入 Session 及 Asio 依赖。见[构建命令](development-cn.md#构建-host-ownersession-和-native-sdk)。

## 向 owner 提交有界工作

[`Executor`](../runtime/host/include/wirelink/host/executor.hpp)绑定 C kernel、I/O 回调和 clock 描述符。`start` 成功前 kernel 归调用方；存储保留到 stop/join。`post` 接纳一条命令，其执行或关闭拒绝都在 owner 上发生；这与 kernel RPC 接纳是两个步骤。队列有 **32 项**，每批最多 **8 条命令**，随后推进 kernel。命令不能抛异常、阻塞或调用 `stop`。

I/O notify 必须非阻塞、锁存、线程安全，且不重入 owner 或业务回调。service/readiness/wait 合并 I/O、core 和应用 deadline。submitter 与 owner 共用的自定义 clock 必须线程安全；手动 clock 前进后还需 `notify`。

## 让 Session 管理连接生命周期

[`Session`](../bindings/cpp/include/wirelink/cpp/session.hpp)使用 C endpoint factory、同一 Executor 和 kernel。C 持有 packet/FIFO 原子存储，C++ 使用 opaque pointer。初始化通过显式平台随机源或持久 source 取得新的非零 instance；失败不回退到时钟或固定生产 ID。比较一个 previous 身份只能做局部保护。

submit 等待 owner 接纳，不等待远端响应。成功前完成输入快照；失败不创建调用或后续回调。同步/异步调用共用四个 kernel call slot。C++ timeout 使用 `1..INT32_MAX` 毫秒并计入命令排队。`Operation<T>` 拥有复制后的结果，可在 client 销毁后读取；丢弃它不取消调用。本地 cancel/timeout 不能撤回远端执行。见 [RPC 结果](kernel-api-cn.md#terminal-results)。

## 从 owner 回调外关闭

owner 回调可请求停止，但不能 join、销毁或替换自身 Executor/Session。owner 内的 Session close 只请求停止，由外部调用方 join。外部 Session close 排除/等待 submitter、关闭逻辑工作、停止实际 I/O、处理最终 completion 并 join owner，然后释放 C 存储。kernel quiescence 本身不证明外部 notifier 已退出。重启需重新初始化存储并取得新 instance。

[开发指南](development-cn.md)给出 host、Session、安装后消费和 Python wheel 检查；[adapter](adapters-cn.md)定义真实 I/O stop/drain。
