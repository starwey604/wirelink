# H7：可靠 RPC 与遥测共存验收

独立测试 app，不启动 Willow、电机、USB 或串口业务，不修改产品依赖。
同一套源码链接指定的新旧核心；基线必须来自优化前的冻结 Git 导出。

本轮结果与取舍见 [H7 实测记录](../../../docs/mixed-traffic-h7-cn.md)。

## 两类数据，不能混淆

- **功能与新鲜度**：复用 `benchmarks/mixed_traffic/workload.c` 的全部 42 条记录。
  两个真实生成端点在同一 H7 上运行，但通道与时钟仍受测试控制。年龄、更新间隔和
  RPC 完成时间仍为模拟协议毫秒，不是 USB/串口实测 RTT。
- **真实 CPU**：550 MHz H7 的 DWT 计数，频率由固件报告，不硬编码换算。
  每个 tick 测两个端点、业务回调、传输故障模型与校验的合计工作量；不包含初始化、
  输出或等待。它不是单端点纯 Wirelink CPU，也不是 Willow 整机占用率。

每个 tick 临时屏蔽中断并使用编译器屏障；仅适用于这个没有外围业务的测试 app。
正常保留 I/D cache，不主动制造冷缓存，不声称覆盖最坏抢占或 cache miss。

## 重传微基准

另外单独包围一次核心操作计时，初始化、数据准备和结果校验在计时外：

| `m` | 计时操作 | 解释 |
| --- | --- | --- |
| 0 | 空闲链路发送不可靠 DATA | 新旧实现相同有效工作量 |
| 1 | 未插入遥测的可靠重传 | 应继续使用已编码缓存 |
| 2 | 等待 ACK 时提交遥测 | 旧实现拒绝，新实现接受；不能直接按耗时判断退化 |
| 3 | 尝试插入遥测后的可靠重传 | 旧实现仍重用缓存，新实现重编码；明确报告新功能的代价 |

三种封装、CRC32C、32/512/2048 B payload、可靠复制/claim 两种来源。
共 72 组，每组预热 8 次，再测 5 × 16 次。`claim` 只描述可靠请求来源；
不可靠微基准统一调用 `wl_send_unreliable()`，不将其当作整个生成 codec 的成本。
每次重传必须与最初可靠帧逐字节相同；遥测另外解码校验长度、内容和完整性。

## 构建

需要开发版 WLC、初始化好的 Zephyr 工作区和 Ragtime 的 `dm_mc02` 板定义。
在 Zephyr 工作区配置，按实际路径替换：

```sh
west build --cmake-only -s /path/to/wirelink/benchmarks/zephyr/mixed_traffic \
  -d /path/to/wirelink/build/mixed-h7-after -b dm_mc02 -- \
  -DBOARD_ROOT=/path/to/Ragtime_Firmwares/firmware \
  '-DZEPHYR_MODULES=/path/to/Ragtime_Firmwares/modules/hal/cmsis_6;/path/to/Ragtime_Firmwares/modules/hal/stm32;/path/to/Ragtime_Firmwares/modules/debug/segger' \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
cmake --build /path/to/wirelink/build/mixed-h7-after --parallel 2
```

基线使用另一个构建目录，并增加 `-DWIRELINK_SOURCE_DIR=/path/to/frozen-core`
和 `-DMIXED_EXPECT_COEXIST=OFF`。这个开关只控制测试预期，不修改核心策略。
两侧都要求 `-O2`、LTO、断言、相同 WLC 与 schema。保存 `.config`、BIN/ELF 摘要。

## 烧录和采集

使用同一个 J-Link Commander（STM32H723ZG、SWD），不同时打开第二个探针客户端。
`loadfile` 烧录并 verify；从各自 ELF 查询 `_SEGGER_RTT` 地址。
每次独立启动前 reset/halt，清除该测试控制块的签名，并重新设置 `exec SetRTTAddr`。
先启动采集，确认 `RTT connected` 后再 `g`：

```sh
python3 benchmarks/zephyr/willow_cpu/capture.py --seconds 20 --out /new/path/h7.log
python3 benchmarks/mixed_traffic/compare_h7.py /new/path/h7.log
```

分析器要求唯一 begin/end、42 条流量记录、21 组 CPU 数据、360 条微基准样本、
完整成功与故障计数。损坏字节、缺行、重复启动或旧 RTT 残留一律拒绝整轮；
不要裁掉坏行或放宽校验来保留样本。重复运行时分别保存文件，并报告实际有效采集顺序。
所有构建、CTest、其他性能采集均应在板端采样窗口之外执行。
