# CPU研究收敛：保留正式实现与合同测试，删除实验入口

## 当前留下什么

产品继续使用已接受的静态端点内核和原生成器。此次CPU研究没有得到可推广的新优化，也没有最终定位Sample波动的cache／总线／调度机制；本轮停止继续尝试USB微优化。

保留的成果限于：

| 内容 | 位置／提交 | 用途 |
| --- | --- | --- |
| 已接受的core、adapter及正式消费者 | 原静态内核及审核修复后的唯一实现 | 不引入通知合并、空调用快路径或codec候选 |
| generation边界回归 | `tests/core/kernel.c`，`62eb97c` | 同到期、callback新generation等既有合同 |
| USB通知／callback最后访问回归 | `tests/zephyr/integration/static_usb_bulk/`，`702f3ce` | 发布与等待交错、stop／静默及最后访问安全 |
| required-fixed编码合同 | WLC `tests/required_fixed.rs`及fixture，`df02154` | wire、预检、失败状态与整数／浮点位模式 |
| varint reader合同 | WLC `tests/varint_reader.rs`及两个fixture，`559e2cc` | 三种生成模式的status、at、输出及部分解码状态 |
| 确定性通知／所有权census | `benchmarks/notification_progress/` | 真实生成代码下的双角色、lease、完成事件和关闭模型 |

census继续默认关闭，仅输出非计时账本；它不是硬件探针或CPU收益证明。构建入口和现有指标含义不变。

根仓`src/`、`include/`、`adapters/`、`runtime/`、`samples/`相对接受的代码边界`5453d03`没有研究候选残留。WLC `main`为`559e2cc`，相对`7eabbf4`的生成器／Cargo依赖不变，仅增加合同测试。

## 删除什么，为什么

删除仓库内59个一次性实验文件：`a_residency/`、`cache_phase/`、`fixed_encode/`、`hardware/`、`owner_census/`、`pmu_sample/`、`varint_product/`及`accepted_cpu.py`。其中包括临时探针、源补丁生成器、候选构建／采集入口、机器码／阶段分析器及其专用测试；不再将这些实验框架作为维护功能留下。

删除WLC两个未采用的本地分支：`experiment/required-fixed-20261101`、`experiment/single-byte-varint-20261111`。没有合并其优化，也没有改写已发布的提交或删除其他开发／release分支。现有合同测试提交单独保留。

本地Git整理采用追加收敛提交；已有研究提交多数已进入`origin/main`，不重写这些历史。WLC保留独立的reader合同测试提交，原有未提交内容单独保留。

## 保留结论，不把失败候选当成新基线

- 通知合并与空调用快路径没有通过性能保护，已经撤回；这里只保留其揭示的正确性测试。
- required-fixed编码虽减少Sample指令并出现局部收益，stream代价与不完整同状态比较阻止推广。
- 五字段诊断降低了“外层pass／Progress重试放大”为主因的优先级，但不能外推成原产品逐帧trace。
- [单字节varint产品筛查](static-endpoint-varint-product-result-20261111-cn.md)的on H2D每等效帧CPU下降5.74%；off两项吞吐约下降6.5%、on Sample API p99为原A的2.448倍，故不采用。

历史报告、原始观测、注册、机器码文本和源码来源记录继续保留供核对。报告中的实验命令和分支状态已标记为历史；需要核对当时工具时使用对应历史Git版本，不把已删除的当前路径或冻结controller当作现行执行入口。这里没有重新包装固件或实验源码归档。

## 验证与交接

保留的census用matching WLC重新做Release构建，CTest `notification_census`通过，八种模型均完成32个RPC、64个Sample及128包收发，源租约／唯一回调／关闭检查通过。日志与JSON在 `/home/ww/codings/wirelink-cpu-convergence-20261111/`。本次没有改变产品代码或WLC测试源码，其软件／硬件正确性沿用同一源码已取得的证据。

此前V-off／V-on、正式sample和Rust缓存的约1.81 GB编译产物已永久删除；本次census临时构建及残留Python缓存也直接删除，没有镜像／对象／可执行文件备份。必要的原A恢复与科学参照资产保留。

没有打开串口／USB或刷写。板上最后已知健康固件仍为未接受的`varint-20261111-on`，不是主线基线；其身份见 `/home/ww/codings/wirelink-varint-product-20261111/handoff.json`。按用户要求不自动回刷。
