# 调用方持有端点存储

生成端点按声明消息大小安排四份 request、四份 reply、一份 ordinary snapshot 和 codec scratch。packet port、恰好两个 RX unit、connector TX/staging、driver 与外层包装是另算预算；不要重复计算端点内部 snapshot，也不要漏算 driver。

core 不分配。可选 fixed-block allocator 可分配整个 C-owned owner block，以新身份初始化，真实 I/O 停止和 kernel quiescence 后才释放。C++/Python bridge 验证失败回滚及 close 后 poison/free。heap、fixed-pool 或 static 是外层存储选择，不进入 kernel 分配服务。

[内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
