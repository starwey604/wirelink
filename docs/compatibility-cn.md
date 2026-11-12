# 0.13 兼容性边界

0.13 破坏 core API/ABI、生成接口、profile、adapter 和 SDK binding API。所有消费者以匹配的本地 WLC 0.13/codegen 0.13 共同重建，旧 runtime 输出不是回退。pre-1.0 版本号不承诺 ABI 稳定。

保留 ordinary native DATA、managed v4 RPC 元数据、公开 tagged SchemaV1、显式 ordinary PositionalV1 完整值域及冻结向量。reliable DATA/ACK 执行、replay/TTL、deferred 语义已删除，独立 frame 工具保留旧字节并不恢复执行能力。schema/ID/connector 与业务重试幂等在带外约定，不得为性能暗缩 payload。当前可接收格式见[包格式](packet-protocol.md)。

[内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
