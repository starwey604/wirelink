# 自动会话身份与平台环境

普通业务不需要选择或保存 session ID。生成 ABI 26 在每次端点初始化时自动取得新身份。
[English](session.md)。本轮未发布；托管 RPC 两端必须使用配套版本。

## 默认用法

```c
#include "calculator_endpoint.h"
#include "wirelink/platform.h"

static calculator_endpoint_t client;
int error = calculator_endpoint_init(&client, wl_platform_environment());
```

桌面链接 `Wirelink::platform`（`WIRELINK_BUILD_PLATFORM=ON`）；Zephyr 启用
`CONFIG_WIRELINK_PLATFORM=y`。该环境同时提供单调毫秒时钟和身份来源。Windows 使用
系统 RNG，Linux 使用非阻塞 getrandom，macOS 使用系统随机接口；Zephyr 使用真实
CSPRNG，缺少该能力或选中测试随机源时返回 NOT_SUPPORTED，不隐式降级。
业务收发不读取随机数，不创建线程或堆。初始化应在普通线程/owner 上执行，不在 ISR 中执行。
Zephyr 的随机驱动可能在初始化阶段等待硬件；不承诺初始化是硬实时操作。

需要注册服务时用 `endpoint_config_defaults(&config, wl_platform_environment())`，
然后填写 `config.on_execute` 等业务 handler，再 `endpoint_init_config()`。
配置可以复用：每次 init/create 才生成身份，不会因为复制配置而复制同一个 session。
自定义时钟覆盖 `config.environment.clock`，所有 RPC/链路截止时间仍使用该时钟。

## 裸机或自定义身份来源

```c
static wl_err_t next_identity(void *context, uint64_t *out) {
  /* 板级代码：从硬件随机源或可靠的持久身份分配器取得新非零值。
   * 必须检查驱动/持久化错误；不要每次重启都返回同一个计数起点。 */
  return board_next_identity(context, out);
}

wl_environment_t environment = {
  .clock = {board_uptime_ms, board},
  .session = {next_identity, board},
};
```

这是板级集成入口，不是每个业务服务的责任。`session.next` 返回 `wl_err_t`，成功时写入
非零 `uint64_t`；描述符可复制，context 必须在使用期间有效。共享来源需自行保证并发安全。
来源失败、零值或同一对象重建时重复上一身份会使初始化失败。后者只是一项局部检查，
不是全球唯一性证明。随机碰撞概率不为零；无随机源的持久方案需保证断电安全和实例间不重复。
测试可以注入确定性来源，不应把这种测试配置用于真实重启。

## 生命周期与 RPC 保护

session 标识一次通信实例，不是设备地址、密钥或可比较大小的版本号。
它在同一初始化生命周期中保持不变，重传沿用原身份；普通断连不自动重建端点。
关闭/停止 producer 后可重新初始化，Wirelink 自动取得新身份。不能复制活跃端点。

托管 RPC v2 在请求/成功响应/拒绝中携带原客户端身份，并与调用编号共同关联响应。
因此旧客户端响应不能完成新实例的同号调用；可靠/不可靠请求响应均覆盖。
可靠请求的元数据身份还必须与链路头一致。旧响应只留下 SESSION_MISMATCH 诊断，
不终止其他调用；这不是认证，也不保证任意旧请求不会再次执行或持久 exactly-once。

调用编号耗尽返回 ID_EXHAUSTED，已有调用继续排空。应用可在 owner 安全点 close/reinit；
无需自行计算新 ID。库不在业务回调中隐式重建、丢弃在途调用或移动适配器资源。
高级裸 link/runtime 仍可显式配置身份，它们的重建/唯一性合同由集成者负责。

## 迁移

普通 `init(endpoint, session_id, clock)` 改为 `init(endpoint, environment)`；
`config.clock` 改为 `config.environment.clock`，普通配置的 `link.session_id` 保持零，
由初始化时的来源填入内部副本。不要在创建配置时自己生成随机 ID。

ABI 26 要求重生成全部消费者。托管 RPC 元数据版本 2 从 12 增至 20 字节；旧版本不自动
探测或回退，两端须成对升级，最大单帧业务容量减少 8 字节。显式字段映射 RPC、普通
codec 和 Compact-v1 帧格式不变；字段映射模式不会自动获得托管响应的新保护。
