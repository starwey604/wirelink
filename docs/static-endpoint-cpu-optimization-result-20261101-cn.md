# 静态端点 CPU 优化执行记录

日期：2026-11-01。按[CPU 优化计划](static-endpoint-cpu-optimization-plan-20261031-cn.md)执行，当前完成 O0，正在进行 O1。产品优化候选尚未进入测量。

## O0：本轮 A 已重新构建并通过资格检查

本轮证据目录为 `/home/ww/codings/wirelink-cpu-optimization-20261101/`，不向历史封存目录写入。本轮 A 从 root `28fb84b`、cross-benchmark `f2896f7` 及各仓库当时实际工作树构建；WLC 为 `7eabbf4`。完整 HEAD、dirty patch、未跟踪输入、Zephyr／模块和 matching WLC 身份见 `A-{off,on}/sources/inputs.json`、`build.json`。旧大内核不进入本轮对照。

`O0-baseline-audit.json` 记录 native core、packet port、USB adapter、生成代码和 observer 对象及 ELF sections。核对的 core、USB、应用、schema/profile 和 observer 源文件与已接受版本一致。入口清理后的新工具和本轮研究代码不改变产品路径。

新 A 的 off/on 均保持 240 MHz、16 KiB／8-way／32 B ICache、`-Os`、LTO off、FS/MPS64、2×2048 B RX、1 RX／1 TX in flight、双角色、4 calls／4 replies、budget16。固件、gated host 和完整生成物已重新构建，不能把历史 capture 作为本轮 A 的性能结果。

本轮已实际运行：

- GCC Release、Clang ASan/UBSan，各 47/47 CTest，含 core/port 并发、USB/UART/UDP host adapter、Executor、owner-pass、USB 应用、生成代码及通知模型。
- Zephyr `static_packet_port`（unit_testing）和 `static_usb_bulk`（native_sim），15/15 测试用例。
- A-off/on 各一份板上 smoke：RPC、idle、低频 RPC、1000 Sample、四种大小／三个方向 stream，业务、双端字节账本和 observer 检查通过。原始记录见 `captures/qualification-A-o{0,1}/`。

## O1：先测探针扰动，再选择候选

独立源快照 D 在 core、port、USB callback、owner 及 semaphore 内计数，host 记录 CoalescingEvent 合并／消费和 adapter service。研究工具生成带严格锚点检查的源补丁，不向产品或 SDK 工作树加入 hook，也不启用额外产品开关。

D 和正式 A 分开。接下来按预登记的三轮 A/D 交错比较 off/on，工作量为 1000 warmup＋10000 RPC、idle5s、200 次低频 RPC、1000 paced Sample，以及 32 B 三方向 stream。它回答计数与探针扰动，不代替 O5 的完整 A/B/C 验收。计数不换算成独占 CPU 时间或预计提速。

O2–O5 尚未开始；先用实际通知、推进、空转和扫描账本决定首个候选。
