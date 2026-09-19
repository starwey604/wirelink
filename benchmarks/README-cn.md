# 基准

Wirelink 的可选、可复现测量。它们都不是正确性门槛，依赖都不进入核心，数值也不构成性能承诺。
每个目录只有一份简短说明：测什么、怎么跑、不测什么。

固件基准统一跑在 ESP32-S3 DevKitC 上。测试矩阵里的 `native_sim` 和 QEMU 只用于正确性，不用于计时。

| 基准 | 测量内容 | 运行位置 |
| --- | --- | --- |
| [api](api/README-cn.md) | 生成端点的 CPU 与内存、loopback RPC、双进程 UDP 延迟 | 主机 |
| [api/EXECUTOR-cn.md](api/EXECUTOR-cn.md) | 多生产者 executor 的交接、唤醒与 LATEST 替换 | 主机 |
| [rpc_validation](rpc_validation/README-cn.md) | 解码、规范指纹与 owned 转换成本 | 主机、ESP32-S3 |
| [codec_plan](codec_plan/README-cn.md) | 字段查找与定长 packed array 的 codec 入口成本 | 主机、ESP32-S3 |
| [mixed_traffic](mixed_traffic/README-cn.md) | ACK 丢失与背压下遥测年龄和 RPC 完成 | 主机、ESP32-S3 |
| [framing](framing/README-cn.md) | 单趟 COBS 编码与已编码重传复用 | 主机、ESP32-S3 |
| [owner_pass](owner_pass/README-cn.md) | 有界 dispatch、空检查和交接（靠计数而非计时） | 主机 |
| [rpc_peer](rpc_peer/README-cn.md) | 可靠 RPC 请求前生成的 steady-session 守卫 | 主机 |
| [bulk](../docs/bulk-performance-cn.md) | 顺序 bulk 状态机与配对 raw sink 的对照 | 主机 |
| [fifo](../docs/fifo-performance.md) | 有序 SPSC FIFO runtime | 主机 |
| `zephyr/rx_backend` | RX ring backend 行为 | ESP32-S3 |

`benchmarks/zephyr/` 下的固件变体与主机基准共用一套采集方式，两侧必须使用同一份冻结生成产物。

这里说明的是怎么复现。机器相关的实测数字发布在[文档站](https://docs.silkenkite.ink/wirelink/)，
由人工在实测后更新，不按次提交进仓库。
