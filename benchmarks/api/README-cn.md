# 固定合同内核 API 诊断

[English](README.md)，构建／采集／stamp 命令见英文页。

当前 workload 为 `wirelink-kernel-api-v1`，真实生成的双角色四槽内核运行于 C 拥有的
port／C11 atomic／RAM loopback 中，C++ 只通过不透明 C 接口驱动 Google Benchmark。
外层显式提供 manual tick，不在包装层复制 RPC 关联或 deadline 表。

七组用例为两个 idle kernel step、每方向四个成功 RPC、每方向四个业务拒绝，以及
32／120／512／1024 字节的双向 ordinary Sample。一次测量含接纳、RPC 源清空、
owner step、真实 RAM 复制／完成和 callback 校验；Sample 校验每个 payload 字节。
它不是单独 submit API、C++ Session 或实板 USB／UART 延迟测量。

解析器校验调用／Sample 数、端点／fixture 字节数、完整 repetition 和源码／机器
身份；CPU 门槛可选，资源与 wall time 仅作诊断。单次共享 host smoke 不推出收益。
报告中的 fixture 大小含本诊断自身的存储，不能当成产品 RAM。