# K5 默认产品迁移与删除记录

K4 的 18 份完整采集及独立重放通过，见 [结果与限制](static-endpoint-kernel-k4-result-20261031-cn.md)。开始 K5，尚未完成默认产品迁移或 K6。入口根仓 `cefa8aa`、WLC `af819f7`、benchmark `27d517b`，dirty 输入归档于 `/home/ww/codings/wirelink-static-kernel-final-20261031/entry/`。此前 K3/K4 索引不再追加。

## 逐项替换责任

| 旧文件／对象 | 正式路径与决定 |
| --- | --- |
| `src/protocol.c`、`init.c`、`transport.c`、`context.h`、`link_frame.h`、`link_profile.h` | 删除 link ARQ、ACK、event／TX handle 引擎；由 `src/packet_port.c` 与 `src/kernel.c` 承载 I/O 所有权和一次通信推进 |
| `src/rpc.c`、`rpc_async.c`、`rpc_internal.h` | 删除两层调用表、replay/cache/TTL、服务端 pending／deferred；统一四调用、四预构造回复；托管 wire 在 `src/rpc_wire.c` |
| `src/endpoint.c`、`pump.c`、`schedule.c` | 删除 policy/service/event 通用编排；WLC 静态路由进入 kernel，业务推进由应用 owner loop 管理 |
| `src/unit_frame.c`、`unit_rx.c/.h`、`rx_ring_bipbuf.c/.h` | 旧引擎 RX/TX 层删除；两个显式 RX unit 和物理 TX lease 归 packet port；stream 增量分帧独立 |
| `src/bulk_sender.c`、`bulk_receiver.c` | 删除依赖旧 owner／ARQ 的 bulk 执行状态机，普通分块消息在应用推进；不在新基础内核重建策略 |
| `src/cobs.c`、`crc.c`、`frame.c`、`frame_cobs_crc16.c`、相关内部头 | 保留为独立 framing／primitive 目标；完整 ACK／历史 wire 向量是工具能力，新的端点仍明确拒绝可靠 DATA／ACK |
| `src/fifo.c`、`latest.c`、`outbox.c` | 保留独立纯数据目标及其测试，不能依赖旧 pump 才使用；不把它们重新接成基础内核策略 |
| `experimental/static_kernel/src/*`、`include/wirelink/*` | 迁到正式 `src/`、`include/wirelink/`；实验目录不再保留第二套代码或永久 FAST 开关 |
| `experimental/static_kernel/adapters/astrial/*`、`asio/*`、`zephyr/*` | 替换正式 Astrial serial／USB、Asio UDP、Zephyr UART／USB／UDP adapter；保留底层驱动、borrowed API 和物理 drain 的真实合同 |
| `include/wirelink/link.h`、`endpoint.h`、`pump.h`、`rpc*.h`、`bulk.h`、`adapter.h`、`port.h` 等旧 owner 接口 | 移除安装和公开声明；新公开入口为 packet port、kernel 与生成 typed endpoint；不以同名 facade 包旧引擎 |
| WLC `managed_rpc_codegen*`、`mapped_rpc*`、`runtime_codegen*`、旧 endpoint／RPC runtime 模板 | 删除旧执行输出；默认 profile v2 生成静态 kernel。保留 schema/public codec 和纯类型／边界分析，不保留可达旧运行时 |
| `runtime/host`、`bindings/cpp` 与 WLC SDK | 迁移 owner／等待与同步异步外层包装；包装只排队 owner 操作并复制回调结果，不建第二套通信关联状态机 |
| 根／Zephyr CMake、Kconfig、cmake 包与生成 helper | 默认及安装仅新内核；旧生成器／旧 DSL 明确失败；可选 adapter 与纯工具分别导出 |
| examples、samples、benchmarks、fuzz、tests | 逐个迁移保留能力；旧 replay／ARQ／deferred 专用测试删除并记录原因；保留冻结 codec／wire vectors，补新合同与不支持诊断测试，不能批量 xfail |
| 默认说明、教程与 SDK 文档 | 更新公开承诺并提供中英文迁移清单：业务重试／幂等、重复执行、长任务 job ID、callback 借用期限和本地取消 |

上表先登记删除／替换边界；后续提交记录实际路径和验证。最终逐文件 diff 从入口 `cefa8aa` 导出，不能把“未默认链接”冒充旧执行源码已经删除。

## 迁移期间约束

默认 C11 core 不读取时钟、不创建线程、不分配 heap；只把 K3 合格实现迁到正式路径，不为已有结果调整缓存、IRAM、队列容量、优化或业务负载。八份原有 dirty 文档和既有未跟踪研究资料保留；必要的文档修改分别记录，不将用户改动整块提交。WLC 与 benchmark 独立提交。

最终源码必须重新构建并复核：旧执行符号／生成模板不可达、默认安装包 C/C++ 与 SDK、所有保留 core／adapter 合同、主要 RPC/Sample 与 32 B 保护。若 native core 机器码／队列或 host 工作量实质改变，扩大到受影响完整矩阵；不能拿删除前原型成绩替代最终确认。
