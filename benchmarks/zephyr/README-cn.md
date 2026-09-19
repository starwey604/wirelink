# Zephyr 固件基准

[English](README.md)。

这些 app 在 ESP32-S3 DevKitC 上跑 `benchmarks/` 的负载，并通过板载 USB Serial/JTAG console
输出 cycle 或墙时。不需要 J-Link 或 RTT 探针。参考板是 `esp32s3_devkitc/esp32s3/procpu`。

| App | 主机对应 |
| --- | --- |
| `codec_plan` | [codec_plan](../codec_plan/README-cn.md) |
| `framing` | [framing](../framing/README-cn.md) |
| `mixed_traffic` | [mixed_traffic](../mixed_traffic/README-cn.md) |
| `rpc_validation` | [rpc_validation](../rpc_validation/README-cn.md) |
| `rx_backend` | 仅 Zephyr 的 RX ring 测试 |

每个 app 的 `esp32s3_devkitc.overlay` 都选择 `zephyr,console = &usb_serial`，
所以固件输出和主机 `compare.py` 日志使用同一文本格式。

## 构建与烧录

从已初始化的 Zephyr workspace，以 `framing` 为例：

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/framing -d build/framing-hw
west flash -d build/framing-hw
```

`rpc_validation` 和 `codec_plan` 链接冻结的生成产物：

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/rpc_validation -d build/rpc-hw -- \
  -DRPC_VALIDATION_CODEC_DIR=/path/to/frozen-codec \
  -DRPC_VALIDATION_OPTIMIZED_CODEC=1
```

改核心前先冻结基线的 ELF、map 和 `.config`；candidate 在另一个目录构建。
两侧必须使用相同板子、工具链和 LTO 设置。

## 采集

先启动采集，再复位板子，以记录启动横幅和全部样本：

```sh
python benchmarks/zephyr/capture_serial.py \
  --port /dev/ttyACM0 --out after.log --until result=pass --seconds 120
```

Linux 端口名是 `/dev/ttyACM*`，macOS 是 `/dev/cu.usbmodem*`，Windows 是 `COM*`。
脚本需要 `pyserial`，并在复位导致 USB 重新枚举时自动重开端口。
不用脚本时，Linux 上 `cat /dev/ttyACM0 > after.log` 也可以；看到 pass marker 后停止。

保留原始日志、ELF、SHA-256 和 `.config`。固件日志用对应主机基准的 `compare.py` 比较，
不要把固件数字与主机数字互比。`native_sim` 和 QEMU 仍是正确性门槛，不是计时时钟。
