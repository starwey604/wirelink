# 长任务改用应用 job

延期 reply token 已删除。RPC handler 必须在本次 owner callback 中填好响应并返回业务状态；不要保存响应视图、token 或 core 栈中的指针以便以后回复。

[设备服务示例](../examples/03_device_service/README.md) 展示当前做法：StartSelfTest（消息 132）接纳任务后立即返回 job_id（消息 133），应用 event loop 在自己的状态中推进任务，完成后发送 SelfTestProgress(job_id, observed) 普通消息（134）。这些新增 ID 属于示例应用协议；原 SelfTest 仍立即返回短计算结果，benchmark schema 未改。

应用应定义重复 Start 的幂等规则、job ID 的持久范围、状态查询和丢失完成消息的恢复。示例仅容纳一个 job，忙时返回 DEVICE_BUSY，并在普通消息背压时保存待发结果，不是通用 job service。取消/超时不能回滚已开始的远端任务，重新 RPC 可能再次执行。

用 `WIRELINK_BUILD_WLC_EXAMPLES=ON` 和本地 WLC 0.13 构建；运行 `wirelink_device_service_demo`，`ctest -R wirelink_device_service` 还验证四个业务文件新增 RPC 而基础设施不变。

[四路调用](tutorial-rpc-async-cn.md)
