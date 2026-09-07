# H1：H7 默认 RPC 功能验证

日期：2026-09-07。用户在 M0–M2 软件验收后授权实板验证。
**H1 完成：一槽、四槽镜像的首次运行、系统复位重跑和断电冷启动均通过。**
另有一次恢复探针连接后的一槽重烧运行通过，共保存七份通过记录，没有发现需要修改实现的问题。
本次不推进 M3–M5，不改产品依赖、main 或 tag。

## 镜像与环境

- Wirelink 实现 `ac9bd480ab9fcd82df1ace860b0f12c8467156ce`；执行时仓库
  `4e3e9ed691d94ea0fff2ea4e5f0ee9935c3db07a` 仅比实现多文档记录。
- WLC `26a07a49597cd06b455ea1060e5b7902d39ea061`，生成 ABI 23。
- 独立 [rpc_usability 样例](../samples/zephyr/rpc_usability/README.md)，
  `dm_mc02/stm32h723xx`；外部 Ragtime 板定义仅用于构建。
- Zephyr `v4.4.0-11610-gbd8c15382376`，SDK 1.0.1 / GCC 14.3.0。
  构建配置为 550 MHz、I/D cache 开启、速度优化、主线程栈 8192 B。
  此处频率、缓存和栈容量是构建配置，不是周期或栈高水位测量。
- Windows `moonf@192.168.8.5`；SEGGER Commander/DLL 9.72，J-Link
  `609799419`，探针固件 2021-05-07。100 kHz SWD 连接成功后切至 1 MHz；
  读到 Cortex-M7、DBGMCU `0x10016483`、Flash 容量寄存器 `0x0400`。

本地构建与 Windows 收到的 ELF SHA-256 一致：

| 容量 | ELF SHA-256 |
| --- | --- |
| 1 | `bc1938818c92d0f0c70c82ecf238f20fff7dcbedc500a4d5f80199cfa548da48` |
| 4 | `a8f962c4c9fb936244a518f054a0ca54d26f0182c72b1900fa087570e94a7bfd` |

## 执行与结果

11:36（UTC+8）开始，同一 Commander 连接中先烧一槽、再烧四槽，均完成 Flash
校验；每次仅影响 `0x08000000` 起的一段 128 KiB。直接覆盖旧测试固件，没有备份、
全片擦除、option byte 更改、驱动替换或 USB 设备重启。此批结束时保留四槽测试镜像；
11:48 为补验一槽冷启动，已换烧一槽镜像。

每次 reset/run 后等待 2 秒，暂停 CPU，读取 RTT RAM，再执行后续复位或恢复运行。
日志明确显示复位方式为 `AIRCR.SYSRESETREQ`，**不是断电上电**。
RTT 控制块位于 `0x24001010`，channel 0 缓冲区位于 `0x24000010`，容量 4096 B；
最初四份及后续三份记录均为读偏移 0、写偏移 174，含完整启动行和单个 PASS 标记，无 FAIL/abort。

| 运行 | endpoint 字节数 | 完成通知数 | Execute handler 次数 | 结果 |
| --- | ---: | ---: | ---: | --- |
| 一槽烧录后 | 16536 | 129 | 127 | PASS |
| 一槽系统复位 | 16536 | 129 | 127 | PASS |
| 四槽烧录后 | 29824 | 135 | 130 | PASS |
| 四槽系统复位 | 29824 | 135 | 130 | PASS |
| 四槽断电上电 | 29824 | 135 | 130 | PASS |
| 一槽恢复连接后重新烧录 | 16536 | 129 | 127 | PASS |
| 一槽断电上电 | 16536 | 129 | 127 | PASS |

用户确认 H7 断电上电后，11:39 重新连接并直接暂停/读取 RTT，再恢复执行；
采集脚本没有 reset/loadfile，Commander 日志也没有自动 connect-under-reset 或复位。
冷启动输出同四槽记录；RTT 写偏移仍为 174，无失败信息。

一槽重新烧录并通过后，用户再次确认断电上电。12:05 以同样的无复位方式采集，
Commander 正常连接，没有自动 connect-under-reset，也没有执行 reset/loadfile。
完整一槽启动记录与预期相符，读偏移 0、写偏移 174，最后恢复 CPU 运行并退出调试器。

一槽输出（复位、重烧及冷启动相同）：

```text
*** Booting Zephyr OS build v4.4.0-11610-gbd8c15382376 ***
RPC_H1 ABI=23 capacity=1 start
async endpoint: capacity=1 bytes=16536 completions=129 handlers=127
RPC_H1 ALL PASS
```

四槽输出（复位及冷启动相同）：

```text
*** Booting Zephyr OS build v4.4.0-11610-gbd8c15382376 ***
RPC_H1 ABI=23 capacity=4 start
async endpoint: capacity=4 bytes=29824 completions=135 handlers=130
RPC_H1 ALL PASS
```

## 本轮证明了什么

所有检查均实际在 H7 执行，覆盖首次调用无需预先 step、提交时请求快照、有界字符串、
2031-byte 响应、TTL 内 100 次连续调用、突发满载 BUSY、单槽 20 次回调续调、
业务拒绝、失败提交无回调、取消、uint32 回绕及排队截止时间、关闭唯一通知、
禁止重入与旧句柄失效。复制的业务值在槽位复用和端点关闭/重建后仍有效；取时次数断言通过。
末尾独立使用 Zephyr uptime，在空闲 75 ms 后直接调用成功。

两个端点在同一 CPU、同一 owner 上通过内存 loopback 通信。这不是主机到 H7 的
USB/UART/DMA 验证，也不证明产品集成兼容性。2 秒是调试器采集等待时间，
2000 ms 是 uptime 功能项的超时余量，均不能当作 RPC 延迟测量。
CPU 周期、实际链路延迟、栈高水位和 H2/H3 仍待对应阶段验证。

## 证据与连接恢复记录

原始文件保留在 Windows
`C:\Users\moonf\codings\wirelink-rpc-h1-20260907`，本地副本位于忽略目录
`build/rpc-h1-20260907/`；运行脚本为 `run.ps1` / `flash-and-check.jlink`。
冷启动采集脚本分别为 `capture-four-cold.ps1` / `capture-one-cold.ps1` 及同名 `.jlink`。
每份 `.bin` 是从 `0x24000000` 读取的 4352 B RTT RAM，不是旧固件备份。

| 文件 | SHA-256 |
| --- | --- |
| `flash.log` | `dc337dd25e849e501bc861fd9b6f609019b550f5713fcfe15733778841978868` |
| `one-first.bin` | `29df9dbbecbe88cdf9ab99bcf8807065e6bdfa0ff4837793676a8f09989e48de` |
| `one-reset.bin` | `29df9dbbecbe88cdf9ab99bcf8807065e6bdfa0ff4837793676a8f09989e48de` |
| `four-first.bin` | `5471dd7630cc720df18d98eeae80fe458652190cc3b599743c406d2185a540e5` |
| `four-reset.bin` | `5471dd7630cc720df18d98eeae80fe458652190cc3b599743c406d2185a540e5` |
| `four-cold.bin` | `af2d5220527115f94fa80821ca2a4bc5140da43af57fd155b27cb6340afd8ab1` |
| `four-cold.log` | `d662fb2ce62ec67b89691c6671baf28b61dd37ae22b3b085318921c0cadebfa4` |
| `one-reflash.bin` | `a6094ee438459a914050b26c9565de706addc1c66ecb02a0fb24d3890244a40d` |
| `prepare-one-after-replug.log` | `756923ca0c46b95b7021a9abd5877b398963d9971288b28cd6bd1564ecc02ca6` |
| `one-cold.bin` | `b6943abffd162d30ca9771be146045d37d8b1a36c82917b73229d2ee3faa599a` |
| `one-cold.log` | `14ab9e44933355717ce2bf126b34346acdc0afd87deeef4a8b8fc1de290d9216` |

过程中曾遇到探针连接故障：四槽冷启动采集后，尝试换烧一槽时
J-Link 在 `InitTarget()` 连接阶段失败，含自动 connect-under-reset 重试；
尚未执行 loadfile，不影响已采集的通过证据，也不是 RPC 测试失败。
随后对已确认的 J-Link USB 实例执行一次 `pnputil /restart-device`，Windows
报告成功、设备恢复 OK，但再连接仍在 `InitTarget()` 失败。没有继续盲重试。
失败记录为 `prepare-one-cold.log` / `prepare-one-cold-retry1.log`，
设备重启记录为 `restart-jlink.log`；上述三个文件也保留在相同的本地/Windows 目录。
用户随后确认断电上电，11:48 正常连接恢复，无需 connect-under-reset。
再次核对 ELF 哈希后成功换烧一槽，Flash 校验通过；启动计数和 PASS 标记均符合预期。
此时的运行经过烧录/系统复位，不当作冷启动；最终另行断电上电并完成上述一槽冷启动采集。

H1 清单无剩余项。当前 Flash 保留一槽 ABI 23 独立测试镜像，没有恢复旧固件。
产品长期测试配置未动；M3–M5 和 H2/H3 仍未开始。
