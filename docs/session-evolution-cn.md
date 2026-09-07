# 自动会话与 RPC 调用归属：P0

状态：实现、本地/远端回归和 H7 功能验证全部完成；2026-09-07。
仅 dev；不合并 main、不发布、不修改产品长期测试快照。

## 合同

- 普通端点初始化接收环境描述符（时钟、身份来源），不接收数值 session ID。
  主机/Zephyr 提供默认环境；裸机可注入 C 函数指针。生成器和核心不依赖 OS。
- 身份在每次 init/create 内生成，复用配置不复用身份；收发热路径不调用随机源。
  来源失败、返回零或同一对象重建时返回上一身份均拒绝初始化。不使用时间/PID/地址降级。
- 托管 RPC 元数据 v2：20 字节，在旧 12 字节结构后追加 BE64 客户端 session。
  所有 delivery 组合都携带它；可靠请求必须与链路发送者身份一致；响应必须匹配本地身份。
  不匹配响应只记诊断，不完成或破坏其他调用。拒绝、缓存重放、延迟回复同样回送身份。
- 随机身份不是可排序的代次，不提供认证、永久幂等或抵抗任意旧请求重放。
  本轮不引入握手、节点地址或连接管理器。
- 数字调用编号耗尽后明确拒绝，不静默回绕；已有调用继续排空。
  owner 安全点 close/reinit 自动取得新身份，不在业务回调中隐式重建端点或撤销在途业务。
- 生成 ABI 26；托管 RPC v1/v2 不互通，两端须成对升级。显式字段映射、普通 codec、
  Compact-v1 链路帧字节不变。

## 批次与验收

1. 实现身份来源合同、主机/Zephyr 后端、端点初始化减法及 RPC v2。
2. 迁移所有仓库内消费者、生成夹具、中英文教程；运行 WLC、host/package、
   C/C++/Python、Sanitizer 和 Zephyr sim/QEMU。补充重建旧响应、错误身份/版本、
   零/失败来源、共享配置、多端点、编号耗尽和热路径零随机读取测试。
3. 使用本机 H7/J-Link 运行独立 sample：实际随机源、多次初始化、旧响应隔离、
   所有可靠性组合和源码计数。记录日志/配对/资源代价；不把板内 loopback 称为 USB 延迟。

硬件已枚举：H7 `2fe3:574c`，J-Link `1366:0101`。不操作 Windows。

## 当前验证记录

原始日志位于本地忽略目录 `build/`。

- WLC 全套通过，fmt / Clippy 通过；新增请求头与链路身份不一致的定向测试 5/5 通过。
- GCC Release host：17/17；安装生成消费者：5/5（含真实平台默认初始化）；
  C/C++20 `-fshort-enums` 消费者：2/2；关闭平台后端的 core-only：2/2。
- Clang ASan/UBSan：15/15；Python 薄 FFI：2/2（预加载 ASan，解释器关闭泄漏检测）。
- 核心 Zephyr：首轮 49/50 配置、300/301 用例通过，FIFO command-flow 被运行器
  终止（rc=-15）；不改代码和超时，单独重跑 1/1 通过。保留首轮记录，不能称首次全绿。
- 生成样例：首轮 20/21 配置通过；x86 QEMU 单槽 H2 的线程 join 失败，
  不改代码和超时，单独重跑 1/1 通过。session 专项在 native_sim、M3、RISC-V、
  x86 QEMU 全通过，四种 delivery 组合均报告热路径身份读取为零。
  native_sim 平台来源和默认端点重建通过；其他模拟平台明确跳过不可用的真实 RNG。
- H7 交叉构建：STM32H723VG / dm_mc02，Zephyr `bd8c15382376`、SDK 1.0.1。
  Flash 94,904 B、RAM 48,768 B；启用 STM32 硬件 RNG，板端不得跳过来源检查。
  ELF SHA-256：`6b46e4413beb7fd9aad2f1c15b6c5048383b51212960ae8c82c469a8501bd7e6`。

关键日志：`session-p0-wlc-final.log`、`session-p0-host-test.log`、
`session-p0-package.log`、`session-p0-sanitize-test.log`、`session-p0-sanitize-python.log`、
`session-p0-core-sim.log`、`session-p0-fifo-retry.log`、`session-p0-samples-final.log`、
`session-p0-h2-x86-retry.log`。

## H7 实板结果

J-Link 609799419 / Commander 9.70，SWD 1 MHz。起初无法 attach，普通/降速/自动
复位下连接和软件 USB reset 均未恢复。用户拔插并按住、松开 RESET 后，在同一
Commander 会话中重新 connect 成功，识别 Cortex-M7 r1p2 / little endian。
直接烧录上述 ELF 并校验成功；没有备份旧固件，也没有操作产品仓库或 Windows。

首次运行与 SYSRESETREQ 复位后复测均得到：

```text
SESSION_P0 ABI=26 start
SESSION_P0 platform RNG 128 PASS
SESSION_P0 platform endpoint reinit PASS
SESSION_P0 rr rebuild/success/reject/deferred source_reads=11 hot_reads=0 endpoint_bytes=3376 PASS
SESSION_P0 ru rebuild/success/reject/deferred source_reads=11 hot_reads=0 endpoint_bytes=3376 PASS
SESSION_P0 ur rebuild/success/reject/deferred source_reads=11 hot_reads=0 endpoint_bytes=3376 PASS
SESSION_P0 uu rebuild/success/reject/deferred source_reads=11 hot_reads=0 endpoint_bytes=3376 PASS
SESSION_P0 ALL PASS
```

日志：`session-p0-jlink-held-reset.log`、`session-p0-h7-rtt-first.log`、
`session-p0-h7-rtt-reset.log`。真实来源检查使用 STM32 RNG；随后确定性矩阵验证包括
旧成功/拒绝响应、延迟回复、编号耗尽/重建恢复，来源计数覆盖发送/处理路径。
这只是板内队列功能测试，不是产品 USB 性能、固件 CPU 基准或随机质量证明；
本轮没有声称验证断电后的全局唯一性。该轮结束时板上保留这个独立测试固件；
后续镜像状态见 [产品实板记录](product-session-hil-cn.md)。

## 配对收尾

- Wirelink 实现：`0e9391e8a3b83e386ca530f9f13237109e7dd61e`。
- WLC 实现/文档：`c6b6a8fa560a15c45d564aad0afd197b13682de8`，生成 ABI 26。
- CI 和安装篇固定此 WLC；WLC 的生成 C 测试反向固定上述 Wirelink 实现提交。
  两仓库都只推送 `dev/wirelink-p0-hardening`，不变更 main/tag。
- [Wirelink Host CI](https://github.com/starwey604/wirelink/actions/runs/34108337901)：
  配对提交 `18affe9`，9/9 jobs 通过，包括 Linux/macOS/Windows core、平台环境、
  安装包、UDP/故障注入、薄 FFI、Astrial、Sanitizer 和 fuzz smoke。
- [WLC CI](https://github.com/starwey604/wlc/actions/runs/34108341102)：4/4 jobs 通过，
  含 Rust/生成 C 质量，以及 Windows、Intel/ARM macOS CLI smoke。
- [Zephyr CI](https://github.com/starwey604/wirelink/actions/runs/34108337872)：2/2 jobs 通过，
  包括核心 unit/native/QEMU、生成端点样例和 ESP32-S3 USB sample 交叉构建。
  本轮未进行 ESP32-S3 实测。

本轮 P0 无未完成实现项。后续产品迁移、USB/固件 CPU 性能验证或发布应另行安排；
这里的 H7 两次板内功能通过，不替代这些验收。

后续产品装配、Asio 后端收敛及软件回归见 [ABI 26 产品同步记录](product-session-sync-cn.md)；
该记录单独区分已通过的构建/模拟测试和待进行的物理 USB 验证。
