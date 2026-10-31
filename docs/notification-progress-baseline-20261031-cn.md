# 通知与推进研究：接受的当前基线

日期：2026-10-31。用户已接受[审核修复后的版本](static-endpoint-kernel-review-fixes-20261031-cn.md)。从本点起，当前静态端点内核是后续研究的 **A_current**；旧 link/RPC/pump 的历史 A 不再作为下一轮优化基线。

## 版本与实际输入

三个独立仓库均建立本地 annotated tag `baseline/static-kernel-accepted-20261031`：Wirelink `d6ac418`、WLC `7eabbf4`、cross-benchmark `73ad75e`。它们是本地研究标记，不是版本发布，也未 push。实际测量核心代码为 `5453d03`，其后的根仓提交只改交付文档。

HEAD/tag 不包含原有 dirty 输入。完整实际工作树、逐文件 SHA 和 patch 已保存在 `/home/ww/codings/wirelink-notification-progress-20261031/baseline/`；机器可读身份见[基线清单](notification-progress-baseline-20261031.json)。Astrial 仍使用原有 dirty 输入，底层 driver 不在本次清理／研究中重写。

接受时完整证据索引 SHA 为 `fe08b808f6350daf93870fafe8c9558ec35d76a6dd57bcf9dd204c73966934f3`，修复后注册 SHA 为 `44c9d6d556208bc7d282bf40becfe62de5ec49883cb02a88928008a999f4e959`。18 份完整数据及独立重放在接受时通过。用户随后明确要求直接清理不需要的旧构建缓存／镜像，不另留档；清理后的可用产物范围、历史索引限制须另行登记，不能继续声称所有旧构建文件都在。

当前必要基线为 CPU-off/on 的修复 B 镜像、对应 frozen gated host、matching WLC、源码/生成物/构建身份和原始测量。板卡保留新 B-on，native serial `10003B6DEC94`，build ID `kernel-usb-d63e0c5f6465c96b-wirelink-on`，bin SHA `8bf6481d13f0de8f5249fb36a11c14c14488ae6c01cd5ab0c691fedfe8da41c0`。

## 已接受的合同及限制

四 calls、四 replies、九 TX snapshot、两个 RX unit、一个物理 TX、budget1–16 公平推进、generation 快照及 deadline-before-RX、唯一终态、真实 consumed/drained/stop/join 均保持。请求接纳前编码快照；重复请求可以再次执行，应用负责重试／幂等／长任务。ARQ、replay/cache、deferred 和通用 service 不恢复。

RPC RTT p50、MCU RPC CPU、MCU Sample CPU 相对旧引擎的配对降幅为72.003%、68.156%、59.462%。这只是已完成的结构和语义裁剪结果，不是后续通知优化的预期收益。15 项保护观察仍不可判定；当前 MCU RPC/C≈1.460、MCU Sample/C≈1.217，部分小包 goodput 未达 C/1.2。相同 USB IRQ 数量不能定位通知、缓存、observer 或计时问题。

## 后续范围

先做当前通知链、owner 推进链、实际 publication/notify/ready/step/wait 的分开账本，再决定是否有可证优化。初期不改 core/adapter/runtime，不搜索 inline、padding、cache、频率或调度组合。软件通知计数不等于 OS 唤醒次数；软件模型不等于硬件时延；inclusive IRQ/thread cycles 不相加或直接当作纯框架成本。

任何候选变更都相对 A_current，同样的工作量／资源／observer／环境，先过 deadline、公平性、背压、逻辑与物理生命周期和 lost-wake 回归，再预登记实际性能比较。旧资料只作历史证据；新的研究结果不改变旧采集标签。

旧构建已按用户要求永久清理；实际保留集合和历史索引限制见[清理记录](notification-progress-retention-20261031-cn.md)。
