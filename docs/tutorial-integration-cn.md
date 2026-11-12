# 接入端点

使用匹配的本地 WLC 和显式 profile v2 构建类型化端点。
一个 `wirelink_wlc_generate` target 提供 codec、路由与存储容量。

1. 分配生成的端点、C 持有的 packet port 和两个 RX unit，以新的非零 instance
   identity 初始化 port。
2. 准备好 adapter 存储与 wake context 后，绑定并启动 adapter。
3. 单 owner 推进 adapter，并向 `wl_kernel_step` 提供单调时间。等待前合并 kernel、
   adapter 与应用 deadline，使用可锁存的唤醒事件。
4. 关闭 kernel，停止并 join I/O producer，继续处理 completion，直到 kernel
   quiescent，再释放存储。

[安装说明](installation-cn.md)提供 CMake 配置；[遥测教程](getting-started-cn.md)
提供可运行端点。[内核 API](static-endpoint-kernel-api.md)定义接纳、回调与关闭规则；
[adapter 生命周期](adapters-cn.md)定义物理缓冲区所有权。
