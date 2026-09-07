# dev → main 合并评估

这是收尾前的历史评估。文中阻塞的修复、固定提交及最新验证见
[ABI 26 集成收尾](dev-closeout-cn.md)；本页保留当时的失败与分支快照。

2026-09-07；评估 `dev/wirelink-p0-hardening`，已 fetch origin。
本轮只整理记录、检查远端状态及本地构建，**未提交、推送、合并、打 tag 或烧录**。

## 结论

**暂不建议整批合入 main。** Git 关系允许快进，Willow 主路径构建/功能证据充分，
但扩展到其他消费者的回归发现 PiperX Dual 未迁移完成，默认编译器获取和 Python 构建入口也未闭合。
这些是具体的构建问题，不需要继续扩大 API 重构来解决。

最新 host 性能改动还未提交，两个产品默认依赖仍是 Wirelink `10083e0`。
直接合入当前 dev HEAD **不会带入本轮性能优化**。内部主线集成、替换长期测试版本、
正式发布是三件不同的事；不能用其中一项通过代替其余两项。

## 分支与依赖快照

下表计数为本地 HEAD 相对刚 fetch 的远端分支，不含未提交文件：

| 仓库 | origin/main | 本地 dev HEAD | main 独有 / dev 独有提交 | 未推送提交 |
| --- | --- | --- | ---: | ---: |
| Wirelink | `423c608` | `4f2d98a` | 0 / 108 | 2 |
| WLC | `ad17a12` | `c6b6a8f` | 0 / 63 | 0 |
| libflorid | `2863e03` | `b2a21ce` | 0 / 20 | 1 |
| Ragtime_Firmwares | `10333dc` | `b3f1df3` | 0 / 32 | 14 |

四个 origin/main 都是 dev 的祖先，无需先 rebase；但这不是行为兼容或质量结论。
Ragtime 的 14 个未推送提交中，12 个在本轮产品同步前已存在，包含 WCH 相关工作。
Wirelink 性能代码/测试/CI 与 Ragtime HIL 可选插桩还在工作树，须单独审阅、提交。
用户的 `Dyn-Calib/`、acados 脏子模块、Ragtime `main/`、`stress/` 未改动，不能打包提交。

当前产品固定配对：Wirelink `10083e0`、WLC `c6b6a8f`（生成 ABI 26）、FCI `47d5323`
（revision-7 显式映射）。Ragtime manifest 与实际 checkout 均为 Zephyr `577e42ad1878`、
WCH `f70e14d`。本次所有产品重建均使用其固定 Wirelink，未传性能源码 override。

## 本次构建与测试

新建 `build/merge-assessment-20260907/`，没有复用旧生成目录或覆盖实板测试二进制。
本机 GCC 16.2.1；固件用 Zephyr SDK 1.0.1；显式传入上述配对 WLC。

| 检查 | 结果 |
| --- | --- |
| libflorid Release，tests + 非 MPC examples | 构建通过，CTest 4/4 |
| Willow H7 完整应用 | 通过；FLASH 353668 B，RAM 117424 B |
| Willow trigger H7 完整应用 | 通过；FLASH 361792 B，RAM 120408 B |
| Willow H5 完整应用 | 通过；FLASH 479288 B，RAM 189588 B（所选 RAM 区域 72.32%） |
| ArmProtocol，native/64 + Cortex-M3 QEMU | 2/2 配置、40/40 用例通过 |
| Upgrade，Cortex-M3 QEMU | 1/1 配置、20/20 用例通过；native/64 被现有条件过滤 |
| PiperX Dual protocol，native/64 + Cortex-M3 QEMU | 两个平台编译失败，28 个用例 blocked，未运行 |
| libflorid 不指定 WLC 的默认 configure | 失败：下载的 WLC 不提供 ABI 26 |
| Python sdist，当前工具链非隔离打包 | tar.gz 生成成功，但包内没有 `3rdparty/wirelink/`，不算可安装通过 |

Twister 总计 3/5 选中配置通过、2 个 build error；60 个用例通过、28 个 blocked。
不能写成“Ragtime 全部通过”。PiperX 测试直接编译产品 `ProtocolBridge.cpp`，
错误不仅在测试代码。H7 保留既存 IWDG EWI 配置警告，另有空库提示和 Ruckig ABI note，
未关闭警告以获得绿色结果。MPC、完整 PiperX 固件及全体其他应用没有完成本轮构建验收。

## 合并前必须完成

1. **补完 Dual 消费者迁移。** `firmware/apps/piperx_dual/src/ProtocolBridge.cpp:16`
   仍断言 ABI 18；`:816`、`:835` 及测试中的低层 `*_send()` 缺 `now_ms`。
   应检查实际调用层级并接入该 owner 的时间来源，不能只改 ABI 常量或填零消除编译错误。
   通过两平台 28 个测试用例后，再构建完整 PiperX H7 应用。
2. **统一源码/CI/Python 的 WLC 获取路径。** 本地 `b2a21ce` 的 Host CI 已源码构建配对 WLC，
   但 `wheels.yml`、`publish-pypi.yml` 未同步。`wheels.yml` 在 main push/PR 自动运行，
   因此并非“等发 tag 再处理”的问题。保持不发布 tag 也可通过固定提交源码构建解决；
   manylinux 容器须自行取得可运行的 host 编译器，不能引用宿主机绝对路径。
3. **补齐可重建的分发包。** `pyproject.toml` 使用 explicit sdist 清单，遗漏 Wirelink；
   这是已有缺口，不能把 `python -m build --sdist` 成功当成包可用。
   补齐依赖和编译器策略后，从解包后的 sdist 构建 wheel 并测试 import。
4. **固定最终性能提交、更新产品 pin，再验最终组合。** 工作树 override 的结果仅证明
   那份源码；先保留性能关闭/开启、并发、Sanitizer 和安装消费回归，再让两个产品指向
   可获取的同一提交并重建。C++ executor 对象布局变化，需要完整重编译。
5. **确认整个 Ragtime 分支范围。** WCH UART DMA/USBFS 为 opt-in 实验功能，尚无硬件验收，
   H7 Willow 不覆盖它们。可独立评审并以实验状态合入，或另建集成分支选择 Wirelink 相关
   提交；不要重写/丢弃原 dev，也不要将整批提交自动认定为 Wirelink 验收范围。

### 远端 CI 证据

- libflorid 最新已运行的 [Host CI](https://github.com/Ragtime-LAB/libflorid/actions/runs/34091600872)
  对应旧 `aadcc56`，Windows 两种链接方式都在 configure 阶段因 WLC ABI 不匹配失败。
  本地修复 `b2a21ce` 未推送，尚无对应 Windows/Linux CI 结果。
- Wirelink 的 [Host CI](https://github.com/starwey604/wirelink/actions/runs/34108337901) 与
  [Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34108337872) 在 `18affe9` 通过；
  后续已提交改动为文档，**该绿色结果不覆盖未提交性能代码和 CI 修改**。
- WLC `c6b6a8f` 的 [CI](https://github.com/starwey604/wlc/actions/runs/34108341102) 通过。
  Ragtime 未找到 GitHub Actions 运行记录，使用本地报告，不声称有远端绿色门禁。

## 实板结论与后续顺序

最新数据统一在 [性能记录](host-performance-convergence-cn.md)，历史失败保留在
[原始基线](product-session-hil-cn.md)，产品初始化迁移见 [同步记录](product-session-sync-cn.md)。
当前最终 OFF host 的无探针/RTT 两组各 3/3 严格窗口通过；H7 owner 平均约 21 µs。
历史 host 长尾尚未捕获归因，不应据此替换长期测试基线或宣布稳定性问题彻底解决。

建议下一批按顺序执行：Dual 迁移 → WLC/Python 构建入口 → 整理性能提交及依赖 pin →
最终组合的本地测试与 Linux/Windows/macOS CI → H7 500 Hz smoke/重连回归 → 分仓合入。
依赖顺序是 WLC → Wirelink → libflorid / Ragtime；FCI 本轮没有待改 schema。
推送、触发远端运行及合并留待下一次授权，本轮未执行。

正式发布还需单独处理 WLC 版本/下载校验和与 release workflow 中陈旧的 ABI 12 断言；
当前没有必要为内部主线集成先发 tag。完整机械臂负载、H5 实板、1 kHz 与线程 CPU/锁持有
时间不在现有 HIL 覆盖内。ABI 25 的历史 NVS 断电通过记录，也不等于 ABI 26 已重验。

## 日志与复现

只收敛文档入口和摘要；未删除原始日志、隐藏失败、改变统计格式或减少运行时诊断。
本轮日志均在对应仓库的 `build/merge-assessment-20260907/`：

- libflorid：`configure.log`、`build.log`、`test.log`；失败证据
  `default-bootstrap.log`、`previous-ci-failed.log`；分发证据 `sdist.log`、`sdist-files.log`。
- Ragtime：`willow.log`、`willow_trigger.log`、`willow_h5.log`；
  `twister.log` 及 `twister/twister.json`，包含 Dual 编译错误和用例状态。
- 原始性能日志继续保留在 Ragtime `build/perf-round-*.log`、`build/h7-performance-*.log`，
  SHA-256 见性能记录。旧测试与本次构建不得合并为同一次“全绿”。

复现产品测试时用新的输出目录；`/path/to/wlc` 必须是 `c6b6a8f` 源码构建的 ABI 26：

```sh
# libflorid 根目录
cmake -S . -B build/merge-check -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_MPC=OFF \
  -DWLC_EXECUTABLE=/path/to/wlc -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF
cmake --build build/merge-check --parallel 2
ctest --test-dir build/merge-check --output-on-failure

# Ragtime_Firmwares 根目录；完整保留三个 suite，不排除失败项
.venv/bin/west twister -T firmware/tests/lib/arm_protocol \
  -T firmware/tests/lib/upgrade_wirelink -T firmware/tests/apps/piperx_dual_protocol \
  -p native_sim/native/64 -p qemu_cortex_m3 -j 1 --inline-logs \
  -O build/merge-check-twister -x WLC_EXECUTABLE=/path/to/wlc \
  -x WIRELINK_WLC_AUTO_DOWNLOAD=OFF
```
