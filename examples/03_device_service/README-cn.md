# 业务 handler 与应用任务

[English](README.md)

使用 `WIRELINK_BUILD_WLC_EXAMPLES=ON` 和匹配 WLC 0.13.0 / 契约 0.13 构建 `wirelink_device_service_demo`。两个 C11 端点共用确定性 owner 和 RAM-copy loopback。运行 `ctest -R wirelink_device_service` 检查 demo 与扩展测试。例程不访问硬件；UDP 互通由 UDP/SDK 例程覆盖。

`services.bind.wl` 保存共享 RPC 声明；CMake 将普通接收/发送 route 组合为 profile-v2 kernel。两种 RPC 角色均启用。业务状态位于 `device_service_t`，模块注册短类型化 handler，填写自持响应并返回有符号业务 status。100 通道普通 telemetry 消息参与 TX 字节容量推导。

StartSelfTest（消息 132）立即返回 job_id（133）。应用轮询等待 20 个 owner tick，发送普通 SelfTestProgress（134）。一次只运行一个 job；第二个 Start 返回 DEVICE_BUSY。普通 TX 背压时保留结果直到接纳成功。现有 SelfTest RPC 直接返回计算结果。源码和 owner 流程见[任务教程](../../docs/tutorial-jobs-cn.md)。

例程跟踪本地 job 状态，按 job ID 匹配收到的 progress；没有远程状态查询 RPC 或完成历史。生产协议需要定义查询/保留/重启行为以恢复丢失的普通 progress。本地 cancel/timeout 不能撤回任务，重复请求可能再次执行。

完成回调在返回前复制借用 view。保存的值，包括含 NUL 的 UTF-8 名称，在 close 和物理 quiescent 后仍可读取。`device_service_extension.py` 在临时副本的四个应用文件中加入 GetBuildLabel，通过相同 CMake/owner 构建运行，并检查原源码未变；它不修改传输或通用 RPC 表。
