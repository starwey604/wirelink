# 包与 payload 格式

Wirelink 使用 ordinary Compact-v1 DATA 包承载类型化消息。native 包由四字节包头
和 payload 组成。managed RPC 在 payload 内增加 client instance、operation ID
与业务状态元数据。

- [包与 RPC 格式](packet-protocol.md)：包头、元数据、校验与 connector 封装。
- [Schema-v1](schema-v1-cn.md)：公开 tagged payload 编码。
- [Positional-v1](positional-v1.md)：依赖 schema 的普通消息编码。
- [Conformance 向量](conformance-v1.md)：独立 frame 工具的逐字节测试向量。

USB Bulk 使用 native packet 与 NONE integrity；UART 使用 COBS 与 CRC16。
两端在交换消息前约定 schema、消息 ID 与 connector。
