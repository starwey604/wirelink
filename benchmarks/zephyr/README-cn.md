# 当前 Zephyr 固件诊断

[English](README.md).

固件与 host 使用同一 C workload。native/QEMU 检查正确性；已标识板卡上的 cycle 是独立 intrinsic 诊断，不等于 USB 延迟或全系统 active CPU。

| App | 当前格式 / 读取器 |
| --- | --- |
| codec_plan | `codec_plan_*_v1`，codec_plan/compare.py，独立冻结纯 codec |
| framing | `framing_cpu_*_v3`，framing/compare.py，独立 framing 工具 |
| mixed_traffic | `mixed_kernel_fw_*_v1`，42 traffic/21 cycle；compare_kernel_firmware.py |
| rpc_validation | `rpc_validation_*_v3`，rpc_validation/compare.py，公开 codec 操作 |

从匹配的 Zephyr workspace 使用新构建目录：

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/framing -d build/framing-hw
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/rpc_validation -d build/codec-validation-hw -- \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc-0.13
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/mixed_traffic -d build/mixed-hw -- \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc-0.13
```

codec_plan 要求 `CODEC_PLAN_CODEC_DIR` 中有另行生成的纯 `plan.c/.h/_values.h`；保留 baseline 冻结产物，candidate 在新目录生成并登记 compiler。当前 public codec validation 不再使用私有 fingerprint seam，不能设置 `RPC_VALIDATION_OPTIMIZED_CODEC=1`。

ESP32-S3 app 使用 USB Serial/JTAG console，启动后等两秒输出。先发现实际板卡、烧录桥、console 路径，不能假定稳定设备名。只在明确选择时刷相应固件，烧录桥与 console 分开占用，结束后关闭进程。临时诊断镜像不满足静态内核计划的最终合格新 B 交回要求。

采集脚本独占新输出文件，找不到结束标记则失败：

```sh
python benchmarks/zephyr/capture_serial.py --port DISCOVERED_CONSOLE \
  --out complete.log --until result=pass --seconds 120
```

保存源码/toolchain/生成 C、observer/LTO/cache、ELF/bin/map、config/命令、全部 raw 与失败尝试，使用对应格式读取器核对完整矩阵。IRQ-masked mixed 模型 cycle 包含 copy、故障注入和应用记账；模拟 ms 单独报告。诊断和 board build 都不等于物理 I/O 资格。
