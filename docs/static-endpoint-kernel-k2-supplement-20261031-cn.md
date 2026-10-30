# K2 唯一完整补充块登记

日期：2026-10-31。机器登记在首份补充捕获之前创建：`../wirelink-static-kernel-20261030/k2s-registration.json`，SHA-256 `7f404801598f3974615b87b14c818b95d56d74315cf0aa3bfb9002ddd6ae5f6e`。父登记为 [首轮 K2](static-endpoint-kernel-k2-registration-20261031-cn.md)，父结果完整保留，不能用补充块替换。

首轮十份均通过身份、工作量、pacing、帧／字节及环境检查。CPU-off RTT p50、CPU-on MCU RPC active/call 的三份 A 均稳定，两个 B 方向一致，配对中位降幅分别为 69.58%、74.38%；MCU Sample active/frame 下降 47.66%。但 A 的尾延迟、host CPU 和 Sample API p99 多项 max/min 跨 5%，相应保护项不可判定。

因此使用计划允许的**唯一一次**完整补充：CPU-off/on 各 A→B→A→B→A，合计十份，仍为 1000 warmup＋10000 RPC、5 秒 idle、100 Hz/200 calls、1000 帧至少 2 ms paced Sample、32 B 三方向各 1 秒准备＋5 秒窗口。全部设备和 host binary、编译器、代码、资源、USB、频率、cache、调度、采集间隔、阈值保持不变。采集器仅复制并替换登记文件名，补丁差异可核对；未做局部重测。

两块独立分析，不池化原始样本、不从中挑一个更好的结果。仍按相邻 A 均值配对，同时给单侧比值；每块分别检查三份 A 的稳定性。补充后若仍不可判定，明确报告限制，不追加第三块。

分析器初稿把 CPU-on RTT/low p99 也标作保护项，已改为机器登记的 off-only 口径；CPU-on 尾部仍描述性报告。原版源码及 JSON 保留，主要指标数值不变，原始捕获、计时和保护阈值未改动。

Sample API 的新路径在返回前编码快照，而 USB 提交在 step 中完成，因此其 API 降幅包含工作位置变化。整段 MCU active/frame 独立报告；不会把 on/off 的 API 绝对差异、相同 IRQ 数量或总 `.text` 大小单独作为计时／缓存归因证据。
