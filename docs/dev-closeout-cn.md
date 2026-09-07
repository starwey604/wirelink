# ABI 26 集成收尾

2026-09-07，接续 [合并前评估](dev-main-merge-assessment-cn.md)。
本轮只推进 dev；不合 main、不打 tag、不发布包，不替换长期测试版本。

## 已实现并固定的版本

| 仓库 | 提交 | 本轮内容 |
| --- | --- | --- |
| Wirelink | `4b650ba` | 固定源码 WLC bootstrap、host 空路径免锁检查、有界唤醒、可选插桩、Windows 路径测试修正 |
| WLC | `c6b6a8f` | 未改编译器；保持生成 ABI 26 |
| libflorid | `06510b6` | 固定上述 Wirelink，修复 sdist/wheel 构建入口、Windows 构建目录与 CRT 配对 |
| Ragtime_Firmwares | `008e298` | Dual 迁移、相同 Wirelink pin、HIL 统一工具解析与可选插桩 |

Wirelink 实现提交是 `c44b28e`；`4b650ba` 仅修正 Windows 测试中的路径比较。
两个产品均固定完整 SHA `4b650ba03f6d4d60fcbec76520a28757f834a1af`，不使用未提交源码
override。三仓 dev 已推送；WCH 的既存实验提交按用户确认随 Ragtime dev 保留并推送。
收尾时重新 fetch，四仓 `origin/main` 仍都是各自 dev 的祖先，没有新 main 独有提交需要同步。
FCI 仍为 `47d5323` 的 revision-7 显式映射，不是托管 RPC v2 迁移。

## 四项收尾

1. **Dual。** 产品 `ProtocolBridge` 的两条遥测发送使用现有 owner 批次时间；
   测试请求发送使用同一模拟时钟。更新 ABI 断言，新增首次 poll 前发送、空闲后发送、
   时间回绕测试，检查 ACK 重试在实际发送 5 ms 后开始，而非在旧 poll 时间开始。
2. **WLC。** 无匹配显式/PATH 工具时，CMake 获取固定提交源码并校验 SHA-256，
   以 host Rust/Cargo、锁定依赖构建；缓存按源码提交和主机 triple 隔离、有并发锁。
   显式指定错误 ABI 直接拒绝；`AUTO_DOWNLOAD=OFF` 仍要求已有工具。
   不再下载旧 ABI 的同版本 release。缺 Rust/网络会明确失败，没有隐式弱降级。
3. **Python。** sdist 包含 Wirelink；manylinux 容器内准备 Rust/libusb，Windows 使用
   host Rust。wheel/PyPI 两套工作流共用 `pyproject.toml` 的 Linux 配置，新增
   `scripts/ci/check-sdist.py` 从解包产物构建、在新 venv 隔离导入。最低 CMake 对齐为 3.21。
   构建目录按 Python ABI/platform 隔离并保留，避免 MSVC 工作目录被过早清理；
   Windows wheel 使用 `x64-windows-static-md`，静态 libusb、动态 CRT 与 CPython 配对。
   只验证构建产物，没有执行 PyPI publish 工作流。
4. **性能与范围。** host 改动已提交，默认 profiling OFF；产品已固定并重建。
   WCH 总开关依赖 `SOC_CH32V203`，子驱动和释放 SDI 都须显式启用；Willow 没有启用它们。
   85 个寄存器模型用例通过，不代表 USB 枚举、HSI 精度、DMA/串口实板时序已验收。

## 本地软件验证

| 检查 | 结果 |
| --- | --- |
| Wirelink 自动 bootstrap + Release 教程/host | 18/18 |
| profiling ON | 6/6 |
| 安装后的 codec/runtime/platform/storage 消费 | 5/5；自动构建时刻意设置固件 `CARGO_BUILD_TARGET`，仍生成 host WLC |
| 显式 / 错 ABI / 禁止自动获取的解析测试 | 通过 |
| TSan 并发专项 | 3 项各 10 次，30/30 |
| ASan/UBSan native | 15/15 |
| libflorid 默认 configure（不指定 WLC）+ Release/examples | 构建通过，4/4 |
| Arm + Upgrade + Dual，native/64 / QEMU | 5/5 配置、94/94 用例；Upgrade native/64 仍按既有条件过滤 |
| WCH DMA / UART TX / RX / UDC 寄存器模型 | 4/4 配置、85/85 用例 |
| manylinux_2_28，CPython 3.12 | sdist → wheel → auditwheel repair → 独立 venv import 通过；通用 sdist 检查脚本也通过 |
| `06510b6` 最终 sdist，Linux CPython 3.14 | 新建源码消费目录，自动 WLC → wheel → 独立 venv import 通过，包内无 build/target/.git 目录 |

完整交叉构建：

| 固件 | FLASH | RAM |
| --- | ---: | ---: |
| Willow H7 | 353668 B | 117424 B |
| Willow trigger H7 | 361792 B | 120408 B |
| Willow H5 | 479288 B | 189588 B |
| PiperX Dual H7 | 180824 B | 83196 B |
| Willow HIL H7 | 167096 B | 76360 B |

PiperX 初次修复构建使用 `10083e0` core，收尾又以最终 pin `4b650ba` 重新 configure/build
通过，FLASH/RAM 不变。其余最终重建用 `c44b28e`，之后仅测试修正到 `4b650ba`，
没有更改 core/适配器/生成器实现。H7 原有 IWDG EWI 警告、空库提示和
Ruckig ABI note 保留，不把构建成功写成无警告。MPC 和所有其他产品应用未全面重验。

### 保留的失败与修正

- 首轮 Windows CI `34123031307`：自动 bootstrap 已通过，新增测试把 `D:\...` 与
  CMake 规范化后的 `D:/...` 直接比较而误报。`4b650ba` 同样规范化预期路径；不改工具解析行为。
- 一次全量 TSan 命令：15 项通过，Python ctypes 无法装载未预加载 TSan runtime 的 `.so`
  （`__tsan_read4`）。不是竞态报告，也不能记为全量 TSan 通过；并发专项另跑 30/30。
- WCH 首次命令遗漏该工作区的 C++20 配置，在 CMake 阶段失败；补齐 `CONFIG_CPP=y`、
  `CONFIG_STD_CPP20=y` 后 85/85。仓库提供的等价配置片段是
  `modules/drivers/wch/tests/configs/ragtime-cpp20.conf`。未改 WCH 驱动来隐藏失败。
- 首轮 wheel CI `34123691098`：Linux 五个 Python 版本与 sdist 消费通过；Windows
  CPython 3.10 完成编译、安装后，清理临时 `build/pyflorid` 遇到 `WinError 32`，
  Python 清理逻辑递归失败。日志另有静态 libusb CRT 与扩展 CRT 冲突警告。
  `06510b6` 改用 [scikit-build-core 支持的持久构建目录](https://scikit-build-core.readthedocs.io/en/stable/configuration/formatted.html)，
  并使用 [vcpkg static-md triplet](https://github.com/microsoft/vcpkg/blob/master/triplets/x64-windows-static-md.cmake)
  对齐 CRT；不杀编译器进程、不忽略清理错误、不禁用 import 测试。

## 最终 H7 与远端 CI

新 HIL `.bin` 与板上 ABI 26 镜像逐字节相同，SHA-256 为
`eb9abe52ce059726c82dc9b8e84f59e1584510c243098ac878d9e79ee074a2fd`，无需再烧录。
新 host 二进制与上轮最终 OFF 产物也相同，SHA-256 为
`b6533cc00a2190cc27bbeeaf80e5b244d858a84f4ae9de95735757d4d3d849a1`。
基于产品 pin 重建验证了产物不再依赖未提交工作树。

本机 Linux、无活动调试器、profiling OFF、本地构建任务结束后运行；
每窗 1000 warmup + 600 ms idle + 10000 唯一回显，原严格门槛不变：

| 窗口 | Hz | Host CPU | RTT p99 / max（ms） | 状态 gap | 结果 |
| --- | ---: | ---: | --- | --- | --- |
| 0 | 499.88 | 4.56% | 2.087 / 3.116 | 0 | PASS |
| 1 | 499.69 | 4.39% | 2.085 / 3.932 | 0 | PASS |
| 2 | 499.21 | 4.69% | 2.100 / 4.326 | 1，host LATEST 合并，约 0.01% | PASS |

三窗共 30000 次正确唯一回显，无 foreign echo、payload mismatch、upstream missing；
open 最大 34.861 ms、close 最大 0.264 ms。既有完整身份/设置 smoke 与恢复均执行。
LATEST 合并不是可靠 RPC 丢失，但不能从摘要中抹掉。此处没有新测 H7 线程 CPU；
板端约 21 µs owner pass 的口径见 [此前 RTT 观测](host-performance-convergence-cn.md)。

普通重连 **100/100** 通过，100 个 session ID 全部非零且互不相同，每轮 cleanup 成功。
open 最大 9.101 ms、close 最大 0.821 ms、回显 max 3.928 ms。每轮仅 10 warmup +
100 回显，不启用严格性能判定；2 个短窗性能 warning 保留，不能当成 100 个严格性能窗口。
异常重连 **2/2** 通过：session 7698/47016 刻意复用首调用编号 3315986515，
遗留租约/RPC 后重新取得租约，probe 3315986517 完成并清理。此项仍是映射 FCI 测试，
不是托管 RPC v2 的完整旧响应隔离证明。

远端结果：

- Wirelink `4b650ba` 的 [Host CI](https://github.com/starwey604/wirelink/actions/runs/34123547890)
  9/9 任务通过，覆盖 Windows/macOS/Linux、profiling、自动 bootstrap、安装消费和适配器。
- 同提交 [Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34123547913)
  通过：核心/集成 50 配置、301 用例，生成消费者 21 配置、21 用例；ESP32-S3 两种 sample
  仅 build 通过，不是实板运行。
- libflorid `06510b6` 的 [Host CI](https://github.com/Ragtime-LAB/libflorid/actions/runs/34124808142)
  4/4 通过：Windows 静态/动态 Release + Debug，Linux Sanitizer 的 io_uring ON/OFF。
- 同提交 [Build Wheels](https://github.com/Ragtime-LAB/libflorid/actions/runs/34124808981)
  **3/3 任务通过**：Windows 和 manylinux_2_28 各完成 CPython 3.10–3.14 五个 wheel 的
  构建及导入测试，另有 sdist 构建、从解包源码构建 wheel 和隔离导入通过。产物仅作为
  Actions artifact 保存，没有发布到 PyPI。

## 主线集成判断

本轮识别的 Dual、默认 WLC、Python 分发、未提交性能实现与产品 pin 不一致等阻塞已收敛。
现有证据支持进入内部主线合并步骤，顺序仍是 **WLC → Wirelink → libflorid / Ragtime**。
WCH 可随 dev 保留，但仍按 opt-in 实验功能处理，不能标为已通过硬件验收。
本轮已完成 dev 推送和验证，**未执行 main 合并、tag、正式发布或长期测试版本替换**。

## 记录与剩余边界

所有原始记录在各仓库 `build/merge-closeout/`，未覆盖上一轮
`build/merge-assessment-20260907/` 或 `build/perf-round-*` 的失败记录。

- Wirelink：`bootstrap-*`、`installed-*`、`profile-*`、`tsan.log`（失败）、
  `tsan-focused.log`、`asan-native.log`、`ci-windows-failed.log`、`ci-zephyr-final.log`。
- libflorid：`release-*`、`manylinux-setup.log`、`manylinux-wheel.log`、
  `manylinux-import.log`、`final-sdist-consumer.log`、`verified-sdist-consumer.log`、
  `ci-wheels-windows-failed.log`、`ci-final-linux-wheels.log`、`ci-final-windows-wheels.log`；
  远端最终产物保存在 `ci-final-sdist/`、`ci-final-linux-wheels/`、`ci-final-windows-wheels/`。
- Ragtime：`dual.log`、`piperx-h7.log`、`piperx-h7-final-pin.log`、`final-twister.log`、`final-*.log`、
  `wch-scope.log`（失败）及 `wch-workspace.log`；Twister JSON 位于同名输出目录。
  最终实板记录为 `h7-final-performance.log`、`h7-final-lifecycle.log`、`h7-final-adversarial.log`。

最终实板原始日志的 SHA-256：

| 日志 | SHA-256 |
| --- | --- |
| `h7-final-performance.log` | `f0195d3a3313e999f467d145a189063d86bcdcc3fbef9780eb643d9e28cd701e` |
| `h7-final-lifecycle.log` | `d45f99b2200a500fa38dc28c99d9b3a7aa086d24e842ba671154b2af10175bcf` |
| `h7-final-adversarial.log` | `7dd97152f35b46a98898488e023daacd364ccb914190926e6c53c10f26e0e508` |

本轮不宣称已根治历史 Linux 偶发长尾，不替换长期测试基线。
完整机械臂负载、H5 实板、1 kHz、板端线程 CPU/锁持有时间及 WCH 实板仍是独立关卡。
正式发版还需决定语义化版本并修正 WLC release workflow 的陈旧 ABI 12 smoke 断言；
不需要为了内部主线集成先发 tag。是否合 main 留待本轮最终证据后的明确操作。
