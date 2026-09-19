# 编译期 codec 规划

独立的 CPU 负载，测字段查找与定长 packed array 特化。不引入 Google Benchmark 依赖，
诊断代码也不进入 Wirelink 核心。同一份 C fixture 在主机和 ESP32-S3 上运行，两侧各有独立的编码字节 oracle。

## 主机对比

在修改 WLC **之前**冻结旧编译器并生成其产物。新产物单独生成；本实验两侧使用同一 codegen 契约，
因为公开接口、布局和线上格式都不变。

```sh
/path/to/old-wlc compile benchmarks/codec_plan/plan.wl --out-dir build/codec-old
/path/to/new-wlc compile benchmarks/codec_plan/plan.wl --out-dir build/codec-new
cmake -S benchmarks/codec_plan -B build/codec-bench-old -DCMAKE_BUILD_TYPE=Release \
  -DCODEC_PLAN_CODEC_DIR="$PWD/build/codec-old" \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/path/to/google-benchmark
cmake --build build/codec-bench-old --parallel
ctest --test-dir build/codec-bench-old --output-on-failure
build/codec-bench-old/wirelink_codec_plan_benchmark \
  --benchmark_min_time=0.03s --benchmark_repetitions=7 \
  --benchmark_out=before.json --benchmark_out_format=json
```

换用 `codec-new` 在另一个构建目录重复，保存 `after.json`。
Google Benchmark 1.9+ 也可用 `find_package(benchmark)` 提供。
使用相同编译器/参数、空闲主机和 A/B/B/A 顺序；计时期间不要编译或运行其他测试。对比：

```sh
python3 benchmarks/codec_plan/compare.py before.json after.json
python3 benchmarks/codec_plan/test_compare.py
```

保留编译器、codec、可执行文件和原始报告的哈希。解析器拒绝不完整矩阵、重复重复、错误、
编码长度变化和机器/负载不一致。编译器/参数需要另外做来源核验。
正确性 smoke 不是计时门槛；共享 CI 不设固定阈值。

### 正确性与计时分开

CTest 的短基准只检查正确性，打印的耗时不是回归测量。正式计时前先完成所有构建。
一次只跑一个基准进程，不要与 CTest、Twister、编译器、sanitizer 或其他基准并行。
在混合频率主机上，把**两个**冻结可执行文件都 pin 到同一个合适 CPU，例如 Linux 上检查
`lscpu -e=CPU,CORE,MAXMHZ` 后用 `taskset -c 1`。记录 affinity；不要把 pin 过的运行与未 pin 的混在同一数据集。
即使 pin 了，调频和其他系统工作仍会带来噪声。

使用重复样本的 A/B/B/A 顺序，计时完成后再单独跑正确性 CTest 和 Zephyr Twister。
固件采集也属于验证主机性能之后的独立阶段。保留 exploratory、smoke 和正式日志的区别，
不要把每个打印耗时都当成性能证据。

## 矩阵

| 类型（`k`） | 形状 |
| --- | --- |
| 0–4 | 2/8/16/32/64 个连续 ID 的 uint32 字段 |
| 5–8 | 8/16/32/64 个稀疏 ID 的 uint32 字段（步长 997） |
| 9 | optional packed float32[30]，字段 ID 1 |
| 10 | required packed float32[128]，字段 ID 21 |
| 11 | optional packed float64[64]，字段 ID 65535 |

pattern（`p`）：0 为全部字段按规范顺序，1 为标量字段逆序，2 为只保留最后一个标量字段并带前后未知字段。
packed array 在所有 pattern 中都存在；pattern 1 故意与 pattern 0 有相同的单字段内容。
stage（`s`）：0 公开 view 解码，1 公开 view 编码，2 公开 encoded-size 计算。合计 **108 个主机组**。

这些是 codec 入口成本，不是完整 RPC 延迟或 owned 转换成本。
[API 基准](../api/README-cn.md)另行通过真实端点/链路处理测 RPC 和遥测。

## ESP32-S3 与指令缓存

从已初始化的 Zephyr workspace 构建 `benchmarks/zephyr/codec_plan`，指定冻结的 `CODEC_PLAN_CODEC_DIR`。
两侧配置必须相同。参考板是 ESP32-S3 DevKitC（`esp32s3_devkitc/esp32s3/procpu`）。

主配置启用速度优化和缓存。ESP32-S3 的 Xtensa 工具链不支持 link-time optimization，
参考构建不启用 LTO。CMake 会检查请求值与实际 LTO 设置是否一致，不一致就失败。
要在支持 LTO 的主机工具链上构建 LTO 变体，传
`-DEXTRA_CONF_FILE=/absolute/path/to/benchmarks/zephyr/codec_plan/lto.conf`
和 `-DCODEC_PLAN_EXPECT_LTO=ON`。不要把不同 LTO 设置当作 A/B，
并确认 `.config` 的 `CONFIG_LTO` 与实际构建命令里的 `-flto`。

固件矩阵有三种模式（`m`）：

- 0：预热的同构消息，每样本 32 次操作。
- 1：每次计时操作前 invalidate I-cache，每样本 32 次操作。
- 2：轮转全部十二种消息形状，每样本 36 次操作（各三次）。

每组 5 个样本：**每次启动 225 组 / 1,125 个样本**。计时读取包围每次操作，
并用编译器内存屏障防止 LTO 把工作提出计时区。IRQ mask 只覆盖被测操作；
cache invalidate、校验、日志和 sleep 都在计时外。两侧保留相同的计时/调用开销，不估算也不扣除。

用[固件 harness](../zephyr/README-cn.md)采集原始 boot-to-pass 输出。
`compare.py before.log after.log` 要求完整 boot-to-pass 记录、计时屏障 marker，以及相同的 LTO/频率。

模式 1 是人为冷入口压力测试，**不是** I-cache miss 计数器；模式 2 是受控混合负载，
不证明覆盖所有业务工作集。热、冷、混合要分别报告，并附总链接 FLASH/RAM。
这个 app 的空闲 FLASH 不代表产品余量。
