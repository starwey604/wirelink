# 可选的端点创建与销毁

静态 `calculator_endpoint_t client = {0}` 仍是默认方式。只有不想由调用者保存大结构体，
或需要绑定层统一管理对象时，才使用本篇的创建层。[English](endpoint-storage.md)。
生成 ABI 26；创建层复用[自动会话](session-cn.md)，托管 RPC 采用元数据 v2。

## 分配器只决定存储来自哪里

```c
calculator_endpoint_t *client = NULL;
calculator_endpoint_config_t config;
calculator_endpoint_config_defaults(&config, wl_platform_environment());
wl_err_t error = calculator_endpoint_create(&client, &config, &allocator);
```

`wl_allocator_t` 包含 allocate、deallocate 和 context。创建只申请一次：整个端点、
请求快照、响应缓存和编解码暂存都在该对象里。后续 init/step/sync/async 的运行逻辑与
静态端点相同，不逐字段 malloc，也不逐消息分配。没有默认全局 allocator 或隐式堆回退。
这里的零分配合同只覆盖生成端点/协议，不代表操作系统或第三方适配器内部完全不分配。

allocate 接收 size 和 alignment，返回满足对齐的可写内存或 NULL；无需预先清零。
deallocate 收到相同指针、大小和对齐。描述符会复制，context 仍须活到 destroy 结束。
回调不能抛异常或重入同一对象；共享分配器需要自行同步。
普通 allocator 不保证内存适用于任何 DMA；驱动专用存储仍由适配器管理。

## 不使用堆：固定池

CMake 启用 `WIRELINK_BUILD_STORAGE=ON` 并链接 `Wirelink::storage`；
Zephyr 启用 `CONFIG_WIRELINK_STORAGE=y`。包含 `wirelink/storage/fixed_pool.h`：

```c
static union {
  calculator_endpoint_t alignment;
  uint8_t bytes[2 * sizeof(calculator_endpoint_t)];
} memory;
static wl_fixed_pool_t pool;
wl_fixed_pool_init(&pool, memory.bytes, sizeof(memory.bytes),
    sizeof(calculator_endpoint_t), CALCULATOR_ENDPOINT_ALIGNMENT, 2);
wl_allocator_t allocator = wl_fixed_pool_allocator(&pool);
```

示例为两个端点预留空间；应检查每个初始化返回值。池最多 64 块，每块大小向 alignment
向上取整。初始化检查对齐、乘法/取整溢出和总空间；失败不修改 pool。
池元数据与端点内存分开，释放后可复用块；池耗尽时 create 返回 NO_MEM，不挪用别的存储。
池自身是单 owner 的，不在内部加锁；所有对象释放后才能重建池或回收 memory。

## 生命周期与失败

create 要求 `*out == NULL`，失败保持 NULL；已申请内存在初始化失败时配对释放。
config 可为临时变量，但 handler、clock、waiter 的上下文仍须满足各自合同。
大小和对齐跟随目标平台及容量宏，所有翻译单元必须使用一致配置。

```c
/* 停止并 join 后台 owner、所有调用线程和外部 producer 后： */
wl_err_t error = calculator_endpoint_destroy(&client);
/* 成功：client == NULL，完成通知已结束、适配器已 quiesce、存储已归还。 */
```

destroy 会先运行生成的 close，再释放对象。NULL 是空操作；静态对象须用 close，不能 destroy。
从自身回调销毁返回 REENTRANT，指针保持有效，待回到 owner 安全点再关闭。
绑定后台 executor 时先 stop/join，再 destroy；destroy 本身不是跨线程停止协议。
调用者保存的指针别名、旧取消句柄和 token 在 destroy 后无效；不能拿它们访问复用地址的新对象。
close 后可以在原地址 reinit，再 destroy；重建会话仍须使用正确的新 session ID。
自持响应副本不受销毁影响，不需要分配器或析构。

## C++ / Python 的窄绑定

[`tests/endpoint_storage`](../tests/endpoint_storage/) 给出可运行的 C 导出层、C++ RAII 和
Python context manager。Python 不需要传回调给收发热路径，也不需要知道 C 结构体布局。
绑定对象负责停止、排空和配对销毁；它不是完整 SDK，也不使用 GC 时机代替明确关闭。
两种消费者各执行 2000 次同步调用，检查只有创建/销毁调用分配器、关闭后结果仍有效，
并注入第二个端点创建失败以验证整个绑定对象回滚。
