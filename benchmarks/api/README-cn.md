# 生成 API 性能回归

多生产者 executor 争用另见 [EXECUTOR-cn.md](EXECUTOR-cn.md)。
单线程 CPU/loopback 用 Google Benchmark，端到端延迟用两个真实 UDP 进程。
Zephyr `native_sim` 仍是行为门槛，不是微基准的时钟。这些是主机测量，不能预测 MCU CPU 时间。

在 Release 构建里启用 `WIRELINK_BUILD_API_BENCHMARKS=ON`。需要配套 WLC、standalone Asio、Python 3，
以及已安装的 Google Benchmark 1.9+ CMake 包或 `WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR`。
验证过的源码：v1.9.5，提交 `192ef10025eb2c4cdd392bc502f0c852196baa48`。
不会自动下载任何东西，基准依赖也不是核心依赖。

```sh
cmake -S . -B build/api-bench -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_API_BENCHMARKS=ON -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/absolute/path/to/google-benchmark \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/include
cmake --build build/api-bench --config Release --parallel 2
ctest --test-dir build/api-bench -C Release -L benchmark --output-on-failure
python benchmarks/api/run.py --bin-dir build/api-bench/benchmarks/api --out before.json
# 修改、重新生成并构建后：
python benchmarks/api/run.py --bin-dir build/api-bench/benchmarks/api --out after.json
python benchmarks/api/compare.py before.json after.json
```

矩阵覆盖 1/12/64 个声明服务、1/4 个槽位、可靠/不可靠 RPC、0–1024 B body、idle/初始化、
4 次调用批次和 512 B 遥测。所有服务形状相同，Echo 是热服务。
这里测的是新增定义的成本，不是执行 64 个业务 handler。
`Rpc/N/B` 的时间是**每次 B 调用批次**，包含校验和 TX 回收。
端点和 arena 字节是主机 ABI 的静态大小，不是进程 RSS 或 MCU RAM。

UDP 报告每轮 p50/p95/p99/max、吞吐，以及客户端/服务端各自的进程 CPU/调用。
它使用真实普通 sync RPC、localhost socket、readiness 等待、预热和 payload 校验，
没有人为毫秒 sleep。服务端用开始/结束 RPC 标记测 CPU 窗口，包含少量边界处理，
与客户端窗口不完全相同。Python 只负责编排进程。

默认：`--repetitions 5 --min-time 0.05 --samples 2000 --warmup 200 --udp-rounds 3`。
用 `--skip-udp` 或 `--skip-micro` 隔离单层。输出文件不会被覆盖；省略 `--out` 只做不落盘的 smoke。
直接运行二进制时也可用 Google Benchmark 的 filter 和报告选项。

对比使用重复样本的中位数，拒绝环境、构建参数、负载、缺失用例或非法数值不一致的情况。
`max` 的对比是各轮最大值的中位数，不是全局最大。计时门槛是可选的：
`--max-regression-percent 10` 只应在专用稳定 runner 上对微基准 CPU 启用。
共享 CI 应只跑正确性 smoke；调频、ASLR 和其他进程都会带来噪声。
源码跨平台，正式测量请在空闲主机上进行。

计时说明见
[Google Benchmark 用户指南](https://google.github.io/benchmark/user_guide.html#cpu-timers)。
