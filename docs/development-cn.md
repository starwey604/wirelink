# 开发与验证 kernel

[English](development.md) · [执行模型](kernel-cn.md) · [API 参考](kernel-api-cn.md)

修改 deadline 时，从 `src/kernel.c` 入手，用 `kernel` CTest fixture 复现；如果影响所有权边界，再运行 packet-port 和生成 route 检查。修改 UART completion 时，从对应 adapter 入手，运行 `static_uart`。按所修改的合同选择检查。

[按修改查找检查](#从修改找到决定性检查)，或从下文构建命令开始。

## 构建并运行 core

以下 POSIX shell 命令均在 Wirelink 仓库根目录执行。需要 CMake 3.21 或更新版本、支持 lock-free 原子的 C11 编译器、用于并发 fixture 的 C++17 编译器，以及用于工具测试的 Python 3.11+。Core GCC/Clang target 将警告视为错误；C++ 集成需要 C++20。

```sh
cmake -S . -B /tmp/wirelink-dev-core -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/wirelink-dev-core --parallel
ctest --test-dir /tmp/wirelink-dev-core --output-on-failure
/tmp/wirelink-dev-core/examples/wirelink_bare_metal_loopback
```

这会构建生产 kernel、packet port、wire/stream、独立工具和已检入生成代码的 loopback。预期得到八次双向 RPC completion、两条遥测消息，以及两个 quiescent 端点，不需要 WLC 或硬件。只有 WLC 可执行文件可用时才加入动态生成 fixture；缺少时 CMake 会报告省略。仅构建 C 库可以使用 `-DBUILD_TESTING=OFF -DWIRELINK_BUILD_EXAMPLES=OFF`。

多配置生成器在 build/install 命令上添加 `--config Release`，CTest 添加 `-C Release`；Windows 可执行路径使用 `.exe` 后缀。CMake target 导出 C11 语言要求，并为 MSVC 的 C11 原子向 C 和 C++ 编译均传递 `/experimental:c11atomics`，以确保 Visual Studio 混合语言 target 的 C 源文件保留该选项。C++ 消费者仍使用不透明的 packet/FIFO 存储。

## 将修改放在负责该合同的层

| 层与源码 | 职责 / 修改例子 |
| --- | --- |
| [`src/kernel.c`](../src/kernel.c)、[`kernel.h`](../include/wirelink/kernel.h) | 接纳、deadline、事件预算、调用终态、snapshot 调度；timeout 优先关系在这里修改 |
| [`src/packet_port.c`](../src/packet_port.c)、[`packet_port.h`](../include/wirelink/packet_port.h) | 原子 RX 发布、borrow、带 token 的 TX 租约；过期 completion 验证在这里修改 |
| [`src/native_none.c`](../src/native_none.c)、[`src/rpc_wire.c`](../src/rpc_wire.c) | 规范 DATA 与 RPC metadata 验证；字节解析在这里修改 |
| 独立 WLC 的 `src/kernel_codegen.rs`、`src/kernel_profile.rs`、`src/codec_plan.rs` | 带类型 route、codec 选择、有界存储；handler/codec 生成在编译器中修改并重新生成 |
| [`packet_stream.h`](../include/wirelink/packet_stream.h)、`src/cobs_crc16_stream.c`、`src/length16_none_stream.c` | 选定的流式分帧、校验和 partial feed；分隔符恢复在这里修改 |
| [`byte_fifo.h`](../include/wirelink/byte_fifo.h)、`src/byte_fifo.c`、`src/byte_fifo_cobs.c` | SPSC 字节暂存和 gap 传播；暂存压力在这里修改，不放进 RPC 队列 |
| `adapters/`、[`zephyr/Kconfig`](../zephyr/Kconfig) | 驱动 I/O、物理完成、readiness、stop；UART drain 或 USB reset 在这里修改 |
| `runtime/platform`、`runtime/zephyr` | 平台时钟、新通信身份和 wait provider |
| `runtime/host`、`bindings/cpp`、`examples/06_bindings`、`examples/07_device_sdk` | Owner 线程、命令提交、Session 和拥有存储的 SDK 结果；用户侧结果交付在这里修改 |

core 是无分配的严格 ISO C11。OS 调用、驱动、堆分配、线程和时钟读取放在 core 之外。owner 提供时间和执行，adapter 发布 I/O。host wrapper 应使用 core 的调用表/deadline，不维护并行 RPC 状态机。

## 构建匹配 WLC 和生成教程

WLC 是独立 Rust 仓库。将 `WLC_SOURCE` 设置为匹配源码 checkout，本地也可放在 `wlc/`。[编译器 setup action](../.github/actions/setup-wlc/action.yml)使用 Rust 1.96.1。Cargo 需要访问锁定依赖，可以使用缓存或下载；构建输出可放在两个源码树之外：

```sh
export WLC_SOURCE=/absolute/path/to/matching-wlc
cargo build --release --locked --manifest-path "$WLC_SOURCE/Cargo.toml" \
  --target-dir /tmp/wirelink-dev-wlc
export WLC=/tmp/wirelink-dev-wlc/release/wlc
"$WLC" --version
"$WLC" codegen-contract
```

预期分别输出 `wlc 0.13.0` 和 contract `0.13`，与 [`WirelinkWlc.cmake`](../cmake/WirelinkWlc.cmake)匹配。版本字符串不能代替生成代码合同检查。显式传入该可执行文件：

```sh
cmake -S . -B /tmp/wirelink-dev-tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON -DWIRELINK_WLC_EXECUTABLE="$WLC"
cmake --build /tmp/wirelink-dev-tutorials --parallel
ctest --test-dir /tmp/wirelink-dev-tutorials --output-on-failure
```

这会加入 telemetry、RPC、保存结果、device-service、组合服务示例、生成端点 fixture，以及源码清单检查。生成 C 位于构建树的 `wirelink-generated/<target>` 或 fixture 的 generated 目录。profile-v2 `wirelink_wlc_generate` target 包含 codec、route/storage 和所选 connector；`wirelink_wlc_generate_codec` 生成不带执行 profile 的独立公开 codec。

修改编译器时，在设置了 `WLC_SOURCE` 的 Wirelink 根目录运行：

```sh
cargo test --locked --manifest-path "$WLC_SOURCE/Cargo.toml"
cargo clippy --manifest-path "$WLC_SOURCE/Cargo.toml" \
  --all-targets --all-features -- -D warnings
cargo fmt --manifest-path "$WLC_SOURCE/Cargo.toml" --check
```

`tests/kernel.rs` 覆盖生成 route/storage/profile 验证；`tests/codec_plan.rs`、`tests/bounded_lengths.rs`、`tests/narrow_integers.rs`、`tests/required.rs`、`tests/owned_values.rs` 和 `tests/positional.rs` 覆盖 codec 合同。保持标量全值域、未知枚举值、浮点 bits 和按字节计数的 UTF-8 上界。公开 Schema-v1、普通 Positional-v1、managed tagged RPC 的区别见[包与 payload 参考](packet-protocol-cn.md)。

## 构建 host owner、Session 和 native SDK

在 Wirelink 根目录，设置 `WLC`，提供 standalone Asio 头文件：

```sh
export ASIO_INCLUDE=/absolute/path/to/asio/include
export PREFIX=/tmp/wirelink-dev-prefix
cmake -S . -B /tmp/wirelink-dev-host -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_WLC_EXECUTABLE="$WLC" -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_BUILD_PLATFORM=ON -DWIRELINK_BUILD_CPP_BINDINGS=ON \
  -DWIRELINK_BUILD_OWNER_PASS_TESTS=ON \
  -DWIRELINK_ASIO_INCLUDE_DIR="$ASIO_INCLUDE" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build /tmp/wirelink-dev-host --parallel
ctest --test-dir /tmp/wirelink-dev-host --output-on-failure
cmake --install /tmp/wirelink-dev-host
```

`WIRELINK_BUILD_CPP_BINDINGS` 启用 host、platform 和 Asio UDP 依赖。预期除生成教程外，还运行 Executor、Session、UDP peer 和确定性 owner-pass 测试。UDP 测试需要打开本机回环 socket 的权限。只需要 owner、不需要 Session/Asio 时，改用 `-DWIRELINK_BUILD_HOST_RUNTIME=ON`。外部命令队列和 kernel 接纳容量不同，见 [owner 执行](kernel-cn.md)。

两个 SDK 已包含生成 C，不需要运行 WLC 即可构建。安装上述 `PREFIX` 后，在 Wirelink 根目录构建 native C++ 测试：

```sh
cmake -S examples/06_bindings -B /tmp/wirelink-dev-calculator \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build /tmp/wirelink-dev-calculator --parallel
ctest --test-dir /tmp/wirelink-dev-calculator --output-on-failure
cmake -S examples/07_device_sdk -B /tmp/wirelink-dev-device \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build /tmp/wirelink-dev-device --parallel
ctest --test-dir /tmp/wirelink-dev-device --output-on-failure
```

它们验证 manifest，并通过独立 C peer 检查 SDK 调用。Python extension/wheel 的前提和命令见 [SDK guide](../examples/06_bindings/GUIDE.md)和[包检查](../tests/package/README.md)。修改 SDK 模板或已检入生成产物后，运行 `python3 tests/package/check_generated_sdks.py --wlc "$WLC"`；应连同 provenance manifest 一起重新生成两个 SDK，而不是分别手改生成文件。

## 验证 installed consumer

使用全新 prefix，避免残留头文件满足包检查。执行下列命令前先安装上述 host 配置。**在 configure 时设置安装 prefix：**`wirelink.pc` 内嵌 `CMAKE_INSTALL_PREFIX`。仅使用 `cmake --install --prefix` 会搬迁 CMake export，却不会重写 pkg-config prefix。host 命令也将 libdir 固定为下文使用的 `lib`；选择其他 libdir 时同步调整 `PKG_CONFIG_PATH`。

```sh
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" cmake -S tests/package/c_core \
  -B /tmp/wirelink-dev-installed-c -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build /tmp/wirelink-dev-installed-c --parallel
ctest --test-dir /tmp/wirelink-dev-installed-c --output-on-failure
cmake -S tests/package/consumer -B /tmp/wirelink-dev-installed-consumer \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DWIRELINK_CONSUMER_HOST=ON
cmake --build /tmp/wirelink-dev-installed-consumer --parallel
ctest --test-dir /tmp/wirelink-dev-installed-consumer --output-on-failure
cmake -S tests/package/wlc_current -B /tmp/wirelink-dev-installed-generator \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DWIRELINK_WLC_EXECUTABLE="$WLC"
cmake --build /tmp/wirelink-dev-installed-generator --parallel
ctest --test-dir /tmp/wirelink-dev-installed-generator --output-on-failure
```

第一项仅启用 C，检查 CMake 与 pkg-config；第二项检查公开 C/C++20 头文件和已安装 owner 组件；第三项针对安装文件生成 native/COBS/C++ 端点并检查生成诊断。可选 installed adapter 和 SDK consumer 在 `tests/package` 各有项目，先启用并安装所需组件。

## 检查内存与未定义行为

在 Wirelink 根目录运行，需要 Clang 及其 ASan/UBSan runtime。使用独立 Debug 构建树，同时对 C/C++ 加入 instrumentation：

```sh
cmake -S . -B /tmp/wirelink-dev-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DWIRELINK_BUILD_HOST_RUNTIME=ON
cmake --build /tmp/wirelink-dev-sanitize --parallel
ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
ctest --test-dir /tmp/wirelink-dev-sanitize --output-on-failure \
  -R '^(kernel|kernel_wire|packet_port|packet_port_concurrent|packet_stream|byte_fifo|wirelink_bare_metal_loopback|wirelink_host_executor|wirelink_coalescing_event|wirelink_host_profile)$'
```

预期无 sanitizer 诊断，选中用例全部通过。LeakSanitizer 需要运行环境允许其检查线程；ptrace/沙箱失败属于环境失败，不是内存检查通过。应在具备该能力的环境中运行相同 instrumentation 二进制。要检查生成/UDP 路径的 sanitizer 行为，再添加上述 host/Session/WLC 配置。并发 fixture 的原子存储和访问保留在 C 中，C++ 只创建/join 线程，不要为让 instrumentation 编译而替换 C 原子。

Fuzzing 的 `-DWIRELINK_BUILD_FUZZERS=ON` 需要 Clang/libFuzzer 和匹配 WLC。target 与 smoke test 定义在 [`fuzz/CMakeLists.txt`](../fuzz/CMakeLists.txt)。

## 运行 Zephyr 检查

使用已初始化的 Zephyr workspace，激活其 Python/west 环境，准备所选平台工具链，使用 [`west.yml`](../west.yml)指定的 Zephyr revision。将 Wirelink 注册为 module；测试项目也通过 `ZEPHYR_EXTRA_MODULES` 添加自己的仓库。以下受控驱动测试不打开板卡。在该 Zephyr workspace 执行：

```sh
export WIRELINK_SOURCE=/absolute/path/to/wirelink
west twister -T "$WIRELINK_SOURCE/tests/zephyr/unit/static_packet_port" \
  -T "$WIRELINK_SOURCE/tests/zephyr/unit/static_stream" \
  -T "$WIRELINK_SOURCE/tests/zephyr/integration/static_uart" \
  -T "$WIRELINK_SOURCE/tests/zephyr/integration/static_usb_bulk" \
  -T "$WIRELINK_SOURCE/tests/zephyr/integration/static_udp" \
  -p unit_testing -p native_sim --outdir /tmp/wirelink-dev-zephyr \
  --inline-logs -j 4
```

`static_packet_port` 复用 kernel/port/wire C fixture，`static_stream` 检查 connector；`static_uart` 覆盖 IRQ/DMA/poll 驱动边界，`static_usb_bulk` 覆盖 USBD completion/reset，`static_udp` 覆盖 native socket 合同。预期每个选中配置均实际执行并通过；在 Twister JSON/XML 中分别查看 filtered、skipped、build-only 和 executed 计数。

运行更广的 unit/integration 集时，在同一 workspace 设置 `WLC`：

```sh
west twister -T "$WIRELINK_SOURCE/tests/zephyr/unit" \
  -T "$WIRELINK_SOURCE/tests/zephyr/integration" \
  -p unit_testing -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64 \
  -x=WIRELINK_WLC_EXECUTABLE="$WLC" \
  --outdir /tmp/wirelink-dev-zephyr-all --inline-logs
```

platform allow list 决定每个 target 上实际运行的 suite。native 测试需要相应 host 构建依赖，QEMU target 需要 SDK/emulator。板卡构建和真实 UART/USB 采集使用 sample/platform 说明，以及 [Zephyr CI](../.github/workflows/zephyr.yml)所列的合格驱动补丁。应用补丁前检查外部树已有修改。sample 构建或受控驱动测试不能确立物理线路行为，该工作使用 [adapter HIL 清单](adapter-hil.md)。

## 从修改找到决定性检查

下列 CTest 名称指上述 core 或 host 构建树，Twister 路径相对仓库。每个检查都要保持修改边界的成功与失败行为。

| 修改 | 检查与必要证据 |
| --- | --- |
| Deadline、budget、cancel、回调重入 | `ctest -R '^kernel$'`；相等/回绕、同时到期、`1..16` 预算、回调新建调用、单一终态、RX/TX 进展 |
| Token 身份、RX 发布、TX 生命周期 | `packet_port`、`packet_port_concurrent`；过期/重复 completion、内联 completion、stop/reinit、源生命周期 |
| DATA/RPC metadata | `kernel_wire`；保留 [`rpc_vectors.h`](../tests/core/rpc_vectors.h)，拒绝错误 metadata 和不支持的包种类 |
| 流编码或暂存 | `packet_stream`、`byte_fifo`；所有 payload 长度、独立 frame oracle、partial feed、压力与 gap 恢复 |
| 带类型 route、profile 验证、codec 容量 | WLC `--test kernel` 与相关 codec 测试；`generated_usb`、`generated_instrument`、`generated_loopback`、`kernel_cpp`；可重复生成和精确业务值 |
| Owner wake/stop、外部命令 | `wirelink_host_executor`、`wirelink_coalescing_event`、`wirelink_owner_pass`；锁存等待、命令压力、stop/join |
| Session/SDK 所有权 | `wirelink_cpp_session`、native SDK CTest、相关 Python/wheel 测试；关闭后保留结果，区分接纳与终态错误 |
| UART completion、RX 恢复 | `tests/zephyr/integration/static_uart`；buffer 归还、物理 drain、BUSY wake、callback stop；硬件结论需要真实 UART 采集 |
| USB 边界/reset、UDP 等待 | `static_usb_bulk` / `static_udp`；按需运行 host `kernel_packet_udp` / 受控 adapter 测试；headroom、过期 token、真实 reset、关闭 |
| 安装、公开头文件、生成 helper | Installed `c_core`、`consumer`、`wlc_current` 和受影响可选 consumer；无源码树 include 或缺失 target 依赖 |

例如修改 deadline 后：

```sh
cmake --build /tmp/wirelink-dev-core --parallel
ctest --test-dir /tmp/wirelink-dev-core --output-on-failure -R '^kernel$'
```

## 先定位边界，再修改行为

- **接纳失败：**区分 `WOULD_BLOCK`、编码错误、closed state 和身份耗尽。未接纳调用就没有终态回调。
- **BUSY 后无进展：**确认真实资源活动到达 port，再检查 adapter service/readiness 与锁存等待。资源未变时反复 step 不是恢复方式。
- **Handler 已生效但无结果：**检查 reply reservation/编码和客户端 deadline。响应编码失败是对端本地错误，不自动成为业务拒绝。诊断重复副作用时保留请求 ID。
- **Timeout 时发射器仍忙：**同时检查 call identity 和物理 TX token。终态可释放逻辑调用，但 snapshot 字节仍被租用。继续 step，等待实际 adapter completion/stop。
- **Close 不结束：**分别检查活动 claim、排队/借用 RX、未 take 的 TX completion，以及驱动 callback/worker 退出。不得清状态或伪造 DRAINED 强行达到 quiescence。
- **Wire 不匹配：**先确定 connector、消息 ID、所选 payload codec，再解码字段；scalar `fixed32` 在 Schema-v1 和 managed tagged body 中不同。保留失败字节向量。

## 使修改便于审查

C 使用两空格缩进、同行花括号、`snake_case`、`wl_` 公开符号、`WL_` 常量和 SPDX 头。保持 C 无警告，Rust 用 `cargo fmt` 格式化。保持生成输出确定性、schema 字段/消息 ID、标量值域和冻结 conformance vector。修改容量或 codec 值域需要明确合同决定，不能靠调整文档隐藏失败值。

同时测试成功和错误路径：返回前快照、副作用前 reservation、回调借用、过期身份、消费/drain 分离、真实 stop/join。同步更新直接 caller、fixture 和文档。提交应聚焦，注明行为/兼容性影响及实际测试命令/平台。修改测量时定义被测工作，并保留构建配置/结果；入口见 [benchmarks](../benchmarks/README.md)。硬件证据与软件模型分开记录。
