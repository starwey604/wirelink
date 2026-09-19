# RPC 校验工作

[English](README.md)。可选的 CPU 回归负载，由 Google Benchmark 和独立的 ESP32-S3 Zephyr app 共用。
生产不加入 transport、分配、计时探针或基准依赖。
完整 RPC/UDP 测量另见 [API 基准](../api/README-cn.md)。

## 主机构建与运行

需要配套 WLC 和 Google Benchmark 1.9+：

```sh
cmake -S . -B build/rpc-validation -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_EXAMPLES=OFF -DWIRELINK_BUILD_RPC_VALIDATION_BENCHMARK=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/absolute/path/to/google-benchmark
cmake --build build/rpc-validation --parallel
ctest --test-dir build/rpc-validation --output-on-failure
build/rpc-validation/benchmarks/rpc_validation/wirelink_rpc_validation_benchmark \
  --benchmark_min_time=0.05s --benchmark_repetitions=7 \
  --benchmark_out=after.json --benchmark_out_format=json
python3 benchmarks/rpc_validation/compare.py before.json after.json
```

使用空闲主机、相同编译器/选项和各自冻结的二进制。跑 A/B/B/A；计时期间不要构建或运行其他基准。
CPU 时间和墙时分别报告。Smoke 只查正确性，不查速度；共享 CI 不设固定计时门槛。

要复现旧实现，用保留的编译器生成 `validation.wl`。把 `RPC_VALIDATION_CODEC_DIR` 指向该冻结输出目录，
并设 `RPC_VALIDATION_OPTIMIZED_CODEC=0`；基准不会重新生成它。
新旧 codec 产物和二进制分别保存，并附 SHA-256。

## 矩阵与判读

共有 65 组，每组有独立的规范字节/哈希 oracle：

- `k0`：string/bytes 容量 32，实际长度 0/16/32。
- `k1`：string/bytes 容量 512，长度 0/16/32/256/512。
- `k2`：嵌套 k1 加 128 个 packed float32，实际长度同 k1。
- `decode`：公开 view 解码；`canonical`：旧规范编码加种子哈希，或新的私有 canonical sink；
  `convert`：公开受检 view-to-owned。
- `pipeline`：decode + 规范指纹 + 普通 handler 的 owned 转换。
  **不**包含 cache 查找、handler 执行、链路 I/O 或 RPC RTT。
- `owned_decode`：公开 owned 解码，包含其内部临时 view。

`b` 是**每个** string 和 byte 字段的长度，不是总 payload 长度。主机 `encoded_bytes` 计数器记录总量。
v2 负载在拷贝 owned 值前，把预计算的 RPC domain seed 和指纹交给两个实现。
不要把 v1 探索性结果与 v2 比较。

私有函数只在这里用于隔离生成内部工作。应用必须继续使用公开 owned API。
外层 owned 清零仍完整保留；只移除重复校验/清零和规范字节存储。

## ESP32-S3

从已初始化的 Zephyr workspace 用相同板子、SDK、模块列表和 flags 构建
`benchmarks/zephyr/rpc_validation` 两次。按上面的方式通过 CMake 传入冻结 codec 目录和
对应的 optimized-codec 设置。参考板是 ESP32-S3 DevKitC（`esp32s3_devkitc/esp32s3/procpu`）。

app 按报告的计时频率记录 timer cycle。它先预热每组，再取 5 批、每批 32 次操作，
**只在测试批次内**屏蔽中断。断言、console 输出和 sleep 都在计时外。
这是热缓存 CPU 微基准，不是生产调度或固件总 CPU。

用[固件 harness](../zephyr/README-cn.md)采集原始 boot-to-pass 输出，
再用 `compare.py before.log after.log` 比较两份完整日志。
解析器要求全部 65 组 × 5 次重复、一个 begin/end 和 `result=pass`；
拒绝缺失/重复行、复位、错误和版本或频率不一致。

解析器契约测试见 `python3 benchmarks/rpc_validation/test_compare.py`。
