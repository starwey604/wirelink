# 链路构建配置

默认 `multi` 支持同一进程内初始化不同配置的端点：COBS stream／native packet／length16，各自可使用 NONE／CRC16／CRC32C。

```sh
cmake -S . -B build/fixed -DWIRELINK_LINK_PROFILE=cobs-crc16
cmake --build build/fixed --parallel
```

Zephyr 选择 `CONFIG_WIRELINK_LINK_PROFILE_COBS_CRC16=y`，默认 choice 为 `CONFIG_WIRELINK_LINK_PROFILE_MULTI=y`。能力定义作用于每个真实 core target 及其私有 framing 依赖。直接编译源文件默认使用 multi；固定构建应为全部 core 源文件一致定义 `WL_LINK_PROFILE_COBS_CRC16=1`。

固定构建要求端点配置 COBS＋CRC16。生成的 COBS 端点选择 `wl_link_defaults()` 时为 CRC16；USB NONE preset 会被拒绝；其他 envelope 默认值仍须显式选择 COBS。显式 CRC32C 覆盖仍被拒绝。`wl_config_requirements`、`wl_init` 和公共 frame 工具对合法但未编入的模式返回 `WL_ERR_NOT_SUPPORTED`，非法枚举返回 `WL_ERR_INVALID_ARG`；size helper 对不可用模式返回零。保留既有参数／packet 校验优先级，不静默改写请求配置。独立 CRC／COBS 工具继续保留完整工具能力。

安装后的 CMake 包通过 `Wirelink_LINK_PROFILE` 公布能力；pkg-config 可用 `pkg-config --variable=link_profile wirelink` 查询。公开结构、wire 字节及 WLC 0.12 合同不变。固定构建省略 native-packet 示例和 native 专用 loopback 测试，其 framing 诊断矩阵默认只包含 COBS＋CRC16。

两种构建共用完整 CRC16／COBS 内核，正常 TX 的不重叠、容量宽裕路径及 stream RX 融合遍历。精确容量和重叠 TX 走共享的有界 writer；RX 在 COBS、结构和完整性检查全部成功后才发布 view。可靠 DATA／ACK、BUSY、重试以及 consumed／drained 生命周期保持不变。

共用 owner 保留帧入口 guard。固定构建的拒绝分支只有“不支持”边界，没有其他 wire 引擎；成功分支直接调用编译期明确的 COBS＋CRC16 内核。逐字节循环没有模式判断或回调，不新增全局可变 scratch、packet buffer 或 heap。

Zephyr 将共用 framing／owner 与 core 放在一起，将独立／冷路径 framing 及可选模式放入其后的私有 archive。Xtensa GCC 为这些 framing／owner 编译单元使用 `-mtext-section-literals`，使 literal 随 text 放置；优化档、LTO、cache 容量、vendor linker script 与 IRAM 覆盖保持原样。性能仍需按目标和实际布局验证，实测配置见 Round 3 报告。
