# 多业务 handler 与应用长任务

这是纯 C11 验收例程：两个生成端点由同一个确定性的应用 owner loop 推进。设备信息、名称、寄存器、计数器、算术、范围及原 SelfTest 保留已有 message 类型／ID，短 handler 填 owned response 后返回业务 status。使用 matching WLC 0.13.0 配置 `WIRELINK_BUILD_WLC_EXAMPLES=ON`，运行 `wirelink_device_service_demo` 或 `ctest -R wirelink_device_service`。这里使用真实 packet loopback，不访问硬件；UDP 互通另由 UDP／SDK 例程验证。

共享 RPC 声明和 ordinary send/direct route 组合为一个 profile v2 kernel，两端均保留 RPC 发起／处理能力。100 通道 telemetry 纳入 TX 容量计算；没有框架 LATEST mailbox、通用 service scheduler 或独立 RPC 表。业务状态显式放在 `device_service_t`，业务模块只注册 typed handler。

旧 deferred SelfTest reply token 删除。原 SelfTest 继续立即返回计算结果；新增应用 message 132–134：StartSelfTest(pattern)、即时 job_id 确认、普通 SelfTestProgress(job_id, observed) 完成消息。这是例程应用协议的显式扩展，没有更改 benchmark payload 或内核托管 RPC wire。单个活动任务由应用状态几个字段表示，应用循环推进；ordinary TX 满时保留完成结果，直到接纳成功。任务忙时返回 DEVICE_BUSY，没有在 core 中重建 job service。

应用自行定义重试、幂等和状态丢失恢复。本地 cancel／timeout 不能回滚已启动的任务，重复请求可能重复执行；生产业务需要规定持久 job 身份、状态查询和去重规则。callback 的 response view 仅借用到返回，例程立即复制 owned value；包含 NUL 的有界名称在 close／物理 quiescent 后仍能读取，不需要 inspect/release 终态槽。

扩展测试在临时副本中只改四份应用文件：schema、共享 binding、业务 handler／注册、client 用例，然后用原 CMake／owner 构建并执行 GetBuildLabel。测试还校验原树不变，没有要求修改传输、通用 dispatcher 或通信状态表。
