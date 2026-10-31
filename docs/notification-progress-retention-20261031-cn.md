# 基线保留与旧构建清理记录

日期：2026-10-31。按用户要求，直接删除不再使用的旧构建缓存、镜像和混合构建归档，不为删除内容另留档。机器可读结果见[清理与保留清单](notification-progress-retention-20261031.json)。

## 清理结果

删除 1,239,704 个文件／链接，覆盖 Wirelink 各历史研究目录和仓库输出中的 CMake 编译缓存、Rust target、node_modules、测试环境、对象文件、旧执行文件／固件、本轮临时验证构建，以及 `/tmp/wirelink-*` 中的旧 review 构建产物。144 个混合旧构建压缩包在分离必要源码和原始记录后删除。源码快照、原始测量、报告和日志保留；没有把已删缓存／镜像重新压缩、备份或移入回收站。一次性大体积删除扫描清单在核验后也删除，仅保留结果收据。

本机 `/home` 文件系统观察到的净空闲空间增加约 **15.6 GiB**。文件有硬链接，不能将逻辑文件大小之和称为实际回收空间。Docker 中没有闲置的 Wirelink 镜像／构建缓存；正在使用的 ROS 和 lidar buildkit 资源未属于本次清理范围。Zephyr SDK、工具链、驱动源码和其他项目资源保留。

## 当前仍需要的参照产物

CPU-off/on 的修复 B 固件、实际 gated host、matching WLC、生成合同及构建身份保留，详细位置和 SHA 见机器清单。两套 device 的可丢弃对象文件、编译器探测缓存和生成 include 链接也已删除；保留 `west flash --no-rebuild` 需要的上下文及镜像。默认 nongated off host 的重复构建目录删除，实际 off runner 位于 `hosts/fix-B-o0/usb_fs_wirelink`。

保留的当前集合为 1119 个普通文件，另有独立 matching WLC。逐项清单位于 `/home/ww/codings/wirelink-notification-progress-20261031/retained-current-artifacts.json`。清理后两套实际 builder artifact 校验／篡改拒绝测试各 2/2 通过；两套 `west flash --context --no-rebuild` 均能读取 runner 上下文。这些命令只查询上下文，本轮没有刷板或打开设备，板上仍为已接受 B-on。

原索引中的 96 份 JSONL／console 原始记录逐项 SHA 校验通过；当前 CPU 复算结果不变。数据工具 37/37 通过。原始输入核验：根仓 962、WLC 117、cross-benchmark 206、Astrial 685 个文件内容保持；例外仅为根仓本轮可选 census 的 CMake 接线，以及 cross-benchmark 退役清单中的 48 个授权变更。用户原有文档及其他在途修改保持原样。

## 历史证据的可用范围

接受时的完整索引及其 SHA、注册与原始数据保持原样。接受时已完成的 18 份完整测量和重放仍是历史事实；旧 A/C、修复前固件和对应编译树已经主动清理。**不能再声称这些历史索引可以完整逐项重验，或直接用旧镜像重新执行。** 不修改旧索引来掩盖删除，也不将当前 runner 冒充旧 runner。

当前保留集合、原始记录和 matching WLC 可单独核验；`accepted_cpu.py` 明确只验证所用原始记录。后续通知／推进研究以 A_current 新登记，并与候选在同一新测量块比较。重建历史版本需从记录的源码和工具身份重新构建，其新镜像身份须另登记，不能继承旧采集身份。
