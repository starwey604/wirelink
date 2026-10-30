# 静态端点内核 K2 筛查登记

日期：2026-10-31。在首份 K2 warmup 之前登记。对应 [计划](static-endpoint-kernel-refactor-plan-20261030-cn.md)。机器可重放登记位于 `../wirelink-static-kernel-20261030/k2-registration.json`，SHA-256：`572cc2f6ab9a67caa5af69a3d727533811dfe4a117a386134160056e4f27a711`。

K0/K1 软件、USB 产品绑定及两组 observer 的旧 host 互通、帧边界资格验证已通过；原始失败尝试仍保留。B 使用已封存的 K1 产品 ELF/bin，四 client slots、四待发回复、2×2048 B RX、单 RX/TX 在途、16 budget、双角色。旧 A 由独立冻结输入和旧生成器构建，包含独立 claim 修复；不使用历史 RTT 数字当作本轮 A。

每个 observer 固定 A→B→A→B→A，各五份、合计十份；每份重新 flash/reset。1000 warmup、10000 RPC、idle 5 秒、100 Hz/200 calls、1000 paced Sample（至少 2 ms），然后 32 B stream 三方向，各 1 秒准备＋5 秒窗口。StreamChunk/Progress 已进入 K1，因而同批实测；其余尺寸留后续完整矩阵。CPU-off/on 独立采集，不相减推导 residual。

各组共用一份冻结**旧执行引擎** host。相较原 K0 host，仅外围 harness 增加 warmup 前 stdin gate、精确 K2 工作量选择和测量结束标记，补丁完整封存；没有修改旧 core、codec 或身份校验。原 K0 host 和新 gated host 分别保留。两份 gated host 已通过边界资格验证，并在 gate 处冻结真实 main/all-worker policy、nice、affinity 及 governor/EPP。各次再次核对，期间每 50 ms 检查全部线程；不设置新的系统参数。

每份 B 与前后相邻 A 的指标均值配对，同时保留两个单侧比值；分别报告两个 B 的降幅及其中位数，不池化原始样本。主要目标为 CPU-off RTT p50、CPU-on MCU RPC active/call 各下降约 40%；稳定的 35–40% 属评审区间，指标不均衡分别说明。Sample active/frame 约 40% 是期望，不强迫等比例下降。

保护指标包括 RTT/低频 p99、Sample API p50/p99、MCU Sample active/frame、各 stream 分量 goodput、host 全线程 CPU/call 或 CPU/frame（5%），idle 的 host/MCU active 比例（0.5 个百分点）。每项先检查三份 A：相对 max/min 跨 5% 或 idle 跨 0.5pp 则该项不可判定，不能池化制造稳定。保留所有原始样本、分段趋势、IRQ、错误、帧／字节、pacing 和资源行。

正确性、账本、源码／产物或运行环境失败立即停下并保留尝试。单次尾部或 Sample 异常不自动删除原型；重复未解释的保护回退不能用主指标平均收益掩盖。必要时最多另登记一次完整补充块，不自动追加、不选择性重测。若收益仅 5–10%，或约 30% 等尚未到期望，停下扩大迁移并交回价值评估；不在同轮搜索 inline、布局、队列、cache、IRAM 或系统调度组合追数。

校验器已用明确标注为 synthetic 的工具测试确认：9999 RPC、不足 2 ms pacing、单槽、错误 stream 账本被拒绝，极端慢样本保留。工具测试不是性能测量，未放入捕获目录。采集工具、validator、host、设备、环境与结构审计的摘要均已登记。
