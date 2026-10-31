# 审核修复证据封存

日期：2026-10-31。对应[修复结果](static-endpoint-kernel-review-fixes-20261031-cn.md)与[完整指标](static-endpoint-kernel-review-fixes-metrics-20261031-cn.md)。本提交仅锚定实际封存结果，不改变测量源码或硬件状态。

证据根：`/home/ww/codings/wirelink-static-kernel-fixes-20261031/`。

| 项目 | 实际值 |
| --- | --- |
| 索引 | `final-evidence-index.json` / `.sha256` |
| 索引 SHA-256 | `fe08b808f6350daf93870fafe8c9558ec35d76a6dd57bcf9dd204c73966934f3` |
| 常规文件 | 19137，合计 789731242 bytes |
| symlink | 7525，核对链接文本，不遍历外部 target |
| 外部证据依赖 | 57，全部重新核对 size/SHA |
| 独立校验时间 | `2026-10-31T02:54:06.053179+00:00` |
| 校验报告 SHA-256 | `6f0b58cc9487e05d445d4a97f4ba924c96710b13f3d067f07ce67da751de48e8` |

[独立校验 JSON](static-endpoint-kernel-review-fixes-seal-verification-20261031.json)与外部原件 `/home/ww/codings/wirelink-static-kernel-fixes-20261031-seal-verification.json` 字节相同。入索引的常规文件和证据目录只读；未 chmod 外部 symlink target。校验工具自身在索引内，封存后独立逐项重读，不打开硬件、不覆盖 raw replay。索引/hash、自身stdout及外部校验结果避免递归自引用，由本 git 提交锚定。

测量代码根仓 `5453d03`，独立 WLC `7eabbf4`，benchmark `73ad75e`；完整 SHA 与 dirty 输入在 `final-input/input.json`。文档提交 `2908a97` 的五份文件、SHA与剩余原始工作区 delta 在 `closing-documents/`。本提交发生在封存后，只增加文档引用，不冒充固件构建 HEAD。

有效性能块唯一来自 `repair-environment-rebase/`，注册 SHA `44c9d6d556208bc7d282bf40becfe62de5ec49883cb02a88928008a999f4e959`，18/18完整capture及独立raw replay通过。旧 `final-validation/` 块在首份A的warmup放行前被优先级漂移门禁阻止，只有四个准备事件，零有效性能样本；预登记失败、资格验证及路径准备失败全部保留，不混入有效块。常驻controller的环境签名在整个有效块一致，没有手动调度／电源／服务设置修改。

索引包括复现前后、软件回归与失败、实际dirty源码归档、WLC/生成物、ELF/bin/map/config/DTS、compile/link命令、object/archive、原始采集、注册／validator／分析／独立重放工具、原始账本和最终交回。排除项仅为索引内明列的VCS元数据、Python cache/bytecode、`.d`、Ninja cache/log及自引用输出；不把symlink target视为源码快照。

外部控制和历史身份依赖原只读目录 `wirelink-static-kernel-20261030`、`wirelink-static-kernel-k3k4-20261031` 与 `wirelink-static-kernel-final-20261031`，应一并保留。旧最终索引 `12fcd9f873495e3b06dd31d351fe5115a438f4edcab193d76c9cf726e9daea5c` 及实际被引用的控制、工具身份另行核对；新证据不覆盖旧证据，也不是整台SDK/OS的自足镜像。

最终镜像为修复后的 **B-on**，native serial `10003B6DEC94`，build ID `kernel-usb-d63e0c5f6465c96b-wirelink-on`，bin SHA `8bf6481d13f0de8f5249fb36a11c14c14488ae6c01cd5ab0c691fedfe8da41c0`。独立最终smoke通过，USB/串口fuser exit1且stdout/stderr均空，host I/O关闭；没有恢复历史固件。封存／校验未重新打开通信。

三项审核发现及受影响应用已提交修复；主要A/B指标达到目标，15项保护观察仍不可判定，libcsp CPU／部分吞吐差距和Sample归因限制保持原结论。没有push、发布、部署、修改其他工程vendor或覆盖用户原有工作区改动。
