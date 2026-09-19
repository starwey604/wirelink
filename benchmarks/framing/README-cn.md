# 帧编码与重试 CPU 基准

这个可选基准单独隔离无分配的 frame/link 工作。不需要 WLC、Asio、executor 或网络。
真实双进程延迟和 executor 争用见 [API 基准](../api/README-cn.md)。
[固件变体](../zephyr/README-cn.md)在 ESP32-S3 上复用同一份 C 负载。

## 构建与运行

安装 Google Benchmark 1.9+，或传入本地 checkout；本 target 不下载任何东西。

```sh
cmake -S . -B build/framing -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_EXAMPLES=OFF -DWIRELINK_BUILD_FRAMING_BENCHMARK=ON \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/path/to/google-benchmark
cmake --build build/framing --parallel 2
ctest --test-dir build/framing --output-on-failure
build/framing/benchmarks/framing/wirelink_framing_benchmark \
  '--benchmark_filter=^(spare|exact|overlap|send|claim|busy3|retry3)/e[01]/i[02]/p[012]/b(32|120|512|2048)$' \
  --benchmark_min_time=0.01s --benchmark_repetitions=5 \
  --benchmark_enable_random_interleaving=true \
  --benchmark_out=before.json --benchmark_out_format=json
```

**在修改核心源码之前**冻结基线二进制。在另一个目录构建 candidate，用 `after.json` 重复。
把二进制 SHA-256、编译器/版本、CMake cache/flags、机器和源码 diff 与 JSON 一起记录。
改完源码后不要重建基线目录。

```sh
python3 benchmarks/framing/compare.py before.json after.json
python3 benchmarks/framing/test_compare.py
```

对比会拒绝失败的负载、缺失/重复的重复、变化的 sink 数、context 大小或测量集合。
报告每操作的中位 CPU 和 wall 纳秒。编译器/flags 需另外检查；JSON 不含完整构建身份。
`--max-regression-percent 10` 是专用 runner 的可选 CPU 门槛，**不是**共享 runner 默认值。
用更长、交替的 A/B/B/A 采集排查疑似回归。主机在跑其他 CPU 密集任务时，
不要做性能验收；正确性 smoke 仍可运行，但其耗时不是性能证据。

## 负载与判读

| 模式 | 一次被测量的操作 |
| --- | --- |
| `spare` | 把一帧编码进按最坏情况大小、互不重叠的存储 |
| `exact` | 编码进恰好等于独立计算出的编码长度 |
| `overlap` | 先把输入拷进输出区域，再用重叠方式编码 |
| `send` | 基于拷贝的不可靠发送与完成轮询 |
| `claim` | claim、把字节拷进 claim、commit 并轮询完成 |
| `busy3` | 不可靠发送，三次 BUSY，再同步 SENT |
| `retry3` | 可靠发送，三次 ACK 超时重试，再 cancel/take 清理 |

`e0/e1/e2` 选择 COBS/native packet/length16。`i0/i1/i2` 选择 NONE/CRC16/CRC32C。
`p0/p1/p2` 选择全零/非零/每 17 字节一个零的 payload。`bN` 是 payload 大小，不含链路头和完整性字节。
完整 smoke 矩阵有 1,134 组，包含空 payload 和 254 字节边界。

紧缓冲在恰好等于最坏边界时仍可走快路径。`claim` 含一次 setup 拷贝，不是直接 codec 的零拷贝测量。
`busy3` 和 `retry3` 各做四次 sink 提交；它们测的是工作量，**不是**网络 RTT、丢包率或等待时间。
时钟是模拟的；操作内部没有 sleep、adapter 或真实 I/O。

初始化、预热和最终字节校验都在 Google Benchmark 计时循环外。
oracle 是 native frame 加独立的 standalone COBS 编码器。每操作返回/事件检查和常数时间计数 sink 计入计时。
协议单元测试另查每次重试的字节、实际编码调用次数、I/O token 新鲜度和 ACK 存储隔离。

sanitizer smoke 另配一个 Clang 构建，使用
`-DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` 和相同的 `CMAKE_CXX_FLAGS`。
不要把它与 Release 结果比时间。`WIRELINK_BUILD_FUZZERS=ON` 增加 `wirelink_fuzz_frame_encode`，
对任意 payload、容量、别名和输出对齐检查三种封装。COBS 用 standalone 编码器作独立 oracle；
packet 封装把重叠/未对齐输出与互不重叠的 native 参考对比。
