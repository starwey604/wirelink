# K6 最终证据封存与独立校验

日期：2026-10-31。配合[最终结果](static-endpoint-kernel-refactor-result-20261031-cn.md)、[指标附录](static-endpoint-kernel-final-metrics-20261031-cn.md)及[删除清单](static-endpoint-kernel-retirement-20261031.csv)交付，K0–K6 已完成。本提交仅记录实际封存结果与完成状态，不改测量源码，也不重跑硬件。

证据根目录：`/home/ww/codings/wirelink-static-kernel-final-20261031/`。

| 产物／范围 | 值 |
| --- | --- |
| 索引 | `final-evidence-index.json`、`final-evidence-index.sha256` |
| 索引 SHA-256 | `12fcd9f873495e3b06dd31d351fe5115a438f4edcab193d76c9cf726e9daea5c` |
| 常规文件 | 120790，共 4306400558 bytes |
| symlink | 44556，校验 link text，不遍历外部 target |
| 外部证据依赖 | 47，全部重新读取并核对 size/SHA |
| 权限 | 所有入索引的常规文件及证据目录均只读，未 chmod 外部 symlink target |
| 独立校验时间 | `2026-10-31T01:18:03.266228+00:00` |
| 校验报告 SHA-256 | `079c06345b06f0ccadca98d92af1d2f773d4055ca09b7982426d54626caa89b9` |

[独立校验 JSON](static-endpoint-kernel-final-seal-verification-20261031.json)与外部原件 `/home/ww/codings/wirelink-static-kernel-final-20261031-seal-verification.json` 字节相同。校验工具 `tools/verify_final_seal.py` 自身在索引内；它在封存后独立逐项重读全部文件、链接与外部依赖，不覆盖既有 raw replay 或打开硬件。索引、自身 hash、seal stdout 与校验输出不互相递归纳入，最终索引及校验报告 hash 由本 git 提交锚定。

最终代码边界：Wirelink `68db8235771f6d5d14e74007b79c48917351d3a7`，独立 WLC `7eabbf4dd767f968123917ba02431fa0a0ef184e`，benchmark `cd8b9cab0732d446128215c0daee2ce5fb84ce69`。`closing-documents/` 保存文档提交 `bc922c1` 的六份文件及其 SHA、原有用户工作区状态和 delta；本次 seal-reference 提交发生在其后，只增加交付文档／完成状态，不冒充固件的构建 HEAD。

索引包括入口／最终源码归档、生成 C、实际 ELF/bin/map/config/compile/link 命令、object/archive、raw capture、失败日志、各阶段测试、注册／validator／分析／独立重放源码及产物。仅排除 VCS 元数据、Python 缓存／bytecode、`.d`、Ninja cache/log 和前述自引用产物。`excluded` 和 `external_inputs` 列出实际范围；不把 symlink target 的文本当作源码快照。

旧 A/C 控制与部分历史资格输入依赖前两份只读证据目录：`/home/ww/codings/wirelink-static-kernel-20261030/`、`/home/ww/codings/wirelink-static-kernel-k3k4-20261031/`。应一并保留；当前 F 不是把整个 Zephyr SDK、系统运行库和所有外部 symlink target 打包的自足机器镜像。实际源码／dirty 输入、外部参考 hash、工具链版本及构建命令均另有登记，旧证据未追加或覆盖。

最终完整性能注册 SHA 为 `4ff395a808448838ead312f0a0ea0b4955ec2d1a2fe873c99a0f5f1dd33a492a`。18/18 完整采集与独立 raw arithmetic replay 均通过；原型 K2/K4 不混入最终数字。主要目标达到，15 项保护观察仍不可判定，libcsp MCU RPC 与部分吞吐目标未达，Sample path/cache/observer 定量归因未解决；封存不会改变这些限制。

最后镜像为新 **B-on**：`static-kernel-final-20261031-B-o1`，bin SHA `d9dc8892dce5fcbf850e2a39ab13c4c9b93a36fa9508336c7ac0bca46db8ef22`，native serial `10003B6DEC94`。`final-handoff.json` 与末次独立 smoke 记录身份及串口／USB 无打开句柄。封存和校验未重新打开通信；板卡保留该合格镜像，未恢复历史固件。

本轮只做本地 scoped git 提交，没有 push、发布、部署或修改外部工程 vendor 副本。原有八份用户文档正文、92 份入口未跟踪文件、benchmark 222 份 dirty 输入和 Astrial 685 份输入的 hash 核对通过；独立 WLC 工作区干净。未执行的远程／平台验证与当前 CDC drain 限制见最终报告，不作已通过声明。
