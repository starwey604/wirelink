# 选择 connector

profile v2 为端点选择一种传输封装：

| Profile envelope | 封装与校验 | 传输 |
| --- | --- | --- |
| `native_packet` | Native packet，NONE | USB Bulk、UDP 等保留包边界的传输 |
| `cobs_stream` | COBS、CRC16-CCITT-FALSE、零分隔符 | UART 或串口字节流 |
| `bus_length16` | 两字节大端长度，NONE | 长度分隔字节流 |

两端必须约定相同的 connector、schema 和消息 ID。WLC 将静态路由、按消息确定的
存储容量以及选定的 stream connector 放入一个 `wirelink_wlc_generate` target。

[遥测示例](../examples/00_telemetry/README.md)提供完整 profile；
[包格式](packet-protocol.md)说明字节编码；[adapter 生命周期](adapters-cn.md)
定义物理 I/O 所有权与完成边界。
