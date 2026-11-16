# 开发与验证指南

[返回文档总览](README.md) · [Wire v2 规范](wire-format.md) · [传输层移植指南](porting-adapters.md)

---

## 一、仓库目录职责

| 目录 | 定位与职责 | 核心约束 |
| --- | --- | --- |
| [`src/`](../src/) · [`include/wirelink/`](../include/wirelink/) | 纯 ISO C11 核心运行库 | **严禁**引入 OS、驱动、线程、时钟读取与动态内存分配（`-Wall -Wextra -Wpedantic -Werror`） |
| [`adapters/`](../adapters/) | 硬件与传输适配器（`zephyr/`、`asio_udp/`、`astrial/`、`astrial_usb/`） | 只通过 `wl_packet_port_t` / `wl_byte_fifo_t` / `wl_packet_stream_t` 对接核心 |
| [`runtime/`](../runtime/) | 执行期封装：`host/`（C++20 Executor/Session）、`platform/`、`zephyr/`（静态锁存 waiter） | 不得与核心维护第二套并行 RPC 状态机 |
| [`bindings/cpp/`](../bindings/cpp/) | 自持式 C++20 UDP 客户端底层支持库 | 配合 `wlc sdk` 生成包使用 |
| [`wlc/`](../wlc/) | 独立 Rust Schema 编译器（词法/语义/Plan/生成引擎与 SDK 打包） | 生成代码引擎位于 `wlc/src/codegen/`；保持输出字节 100% 确定性 |
| [`examples/`](../examples/) | 教学示例与 SDK 演示（`00_telemetry` ~ `07_device_sdk`、`bare_metal_loopback.c`） | 文档锚点源码由 `tests/tutorials/doc_sources.py` 自动校验 |
| [`samples/zephyr/`](../samples/zephyr/) | Zephyr RTOS 平台独立工程示例 | 覆盖 `endpoint_clock`、`composed_services`、`rpc_usability`、`session`、`udp_peer` 等 |
| [`tests/`](../tests/) | 测试矩阵：`core/`、`packet_tools/`、`package/`、`tutorials/`、`zephyr/`、`fixtures/` | `tests/fixtures/wlc/generated/` 保留兼容演化验证基线 |
| [`fuzz/`](../fuzz/) | Clang libFuzzer + ASan/UBSan 模糊测试目标 | 覆盖 `codec_tlv`、`codec_positional`、`generated_codec`、`stream_cobs_crc16` 等 |
| [`benchmarks/`](../benchmarks/) | 静态代码体积测算（`footprint/footprint.py`）与微基准套件 | 构建依赖不污染 core |

---

## 二、常用构建与测试命令

### 1. 默认核心构建与基础测试（无需 WLC 或硬件）

默认启用测试，需 C11、C++20 编译器与 CMake 3.21+；C++20 用于 `packet_port_cpp` 等跨语言 ABI 测试。核心与回环示例保持纯 C11。

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
```

这会编译核心库、checked-in loopback 示例并执行全部主机核心单测、`packet_tools` 测试与文档片段校验（`wirelink_tutorial_doc_sources`）。

### 2. 编译 Rust 编译器 `wlc` 并运行编译器测试

`wlc/` 是独立 Cargo 项目。本地候选使用 WLC **0.14.0** 与生成契约 **0.14**；现有 CI 工具链固定为 Rust 1.96.1。不要用旧 release-pair 的编译器构建当前生成代码：

```sh
cargo fmt --manifest-path wlc/Cargo.toml --check
cargo clippy --manifest-path wlc/Cargo.toml --all-targets --all-features -- -D warnings
cargo test --manifest-path wlc/Cargo.toml
cargo build --release --manifest-path wlc/Cargo.toml
```

### 3. 全量生成教程、Host Owner 与 C++/Python SDK 验证

需本地编好的 `wlc` 与 standalone Asio 头文件：

```sh
cmake -S . -B build/all -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE="$PWD/wlc/target/release/wlc" \
  -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_BUILD_CPP_BINDINGS=ON \
  -DWIRELINK_BUILD_OWNER_PASS_TESTS=ON \
  -DWIRELINK_ASIO_INCLUDE_DIR=/path/to/asio/include \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build build/all --parallel
ctest --test-dir build/all --output-on-failure
```

校验已检入 SDK 的生成一致性与发布脚本自身单测：

```sh
python3 tests/package/check_generated_sdks.py --wlc "$PWD/wlc/target/release/wlc"
python3 tests/package/test_release.py
```

### 4. libFuzzer + ASan/UBSan（需 Clang）

```sh
cmake -S . -B build/fuzz -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DWIRELINK_BUILD_FUZZERS=ON
cmake --build build/fuzz --parallel
ctest --test-dir build/fuzz --output-on-failure
```

### 5. Zephyr Twister 单元测试与集成仿真

在已初始化的 Zephyr 工作区（设置好 `ZEPHYR_BASE` 与 `ZEPHYR_SDK_INSTALL_DIR`）执行：

```sh
west twister -T tests/zephyr/unit -T tests/zephyr/integration \
  -p unit_testing -p native_sim -p qemu_cortex_m3 -p qemu_riscv32 -p qemu_x86_64
west twister -T samples/zephyr \
  -p native_sim -p qemu_cortex_m3 -p qemu_x86_64
```

当前活跃单元测试与集成套件目录：
- `tests/zephyr/unit/`: `cobs`、`generated_codec`、`static_packet_port`、`static_stream`
- `tests/zephyr/integration/`: `cobs`、`mixed_traffic`、`static_uart`、`static_udp`、`static_usb_bulk`、`udp_socket_contract`、`waiter`

### 6. 静态代码体积测算（`-Os`）

```sh
python3 benchmarks/footprint/footprint.py --no-digests --json build/footprint.json
```

---

## 三、按修改模块定位源代码与对应测试

| 修改目标 | 主要源码 | 必跑验收项 |
| --- | --- | --- |
| 内核状态机 / 队列 / 超时取消 | [`src/kernel.c`](../src/kernel.c)、[`kernel.h`](../include/wirelink/kernel.h) | `ctest --test-dir build/core`、`tests/zephyr/unit/static_packet_port`、`tests/zephyr/integration/mixed_traffic` |
| Wire v2 帧头 / RPC 子头 | [`kernel_wire.h`](../include/wirelink/kernel_wire.h)、`wlc/src/codegen/wire.rs` | `cargo test`、`build/fuzz`、`tests/zephyr/unit/generated_codec` |
| TLV / Positional 编解码运行时 | [`src/codec.c`](../src/codec.c)、[`codec.h`](../include/wirelink/codec.h) | `tests/zephyr/unit/generated_codec`、`cargo test`、`build/fuzz` |
| `cobs_crc16` / `length16` 流连接器 | [`src/cobs_crc16_stream.c`](../src/cobs_crc16_stream.c)、[`src/length16_none_stream.c`](../src/length16_none_stream.c)、[`src/crc.c`](../src/crc.c) | `tests/zephyr/unit/{cobs,static_stream}`、`tests/packet_tools/`、`build/fuzz` |
| `wlc` 代码生成引擎与单结构体 API | `wlc/src/codegen/` | `cargo test`、`cargo clippy`、`check_generated_sdks.py`、`-DWIRELINK_BUILD_WLC_EXAMPLES=ON` |
| 文档与示例片段同步 | `README*.md`、`docs/*.md`、`wlc/README.md`、`examples/` 锚点 | `python3 tests/tutorials/doc_sources.py` |


## 四、生成代码与文档的验收边界

当前生成入口是单个 `.wl`，节点选择通过 CLI `--node` / `--all-nodes` 或 CMake `NODE` / `ALL_NODES`。有多个节点时不能依赖默认选择。生成输出为单对 `.h/.c` 与 manifest；安装消费工程应只链接实际需要的节点，验证默认链接选项下不产生未实现的其他角色静态钩子。

默认字段必选且不带 `has_*`；旧可选字段显式写 `optional`，旧可选数组写 `optional packed`。修改 schema 后同时检查生成布局、初始化、业务回调、测试和 SDK，不能只做函数名替换。对 embedded 的验证至少覆盖：TX 忙后 `tx_ready` 恢复、流接收 `consumed` 尾部、停 RX 后关闭半帧与 quiescent。external 还需保留物理租约完成/停止测试。

文档代码锚点直接取自可运行示例：

```sh
python3 tests/tutorials/doc_sources.py
ctest --test-dir build/core -R wirelink_tutorial_doc_sources --output-on-failure
```

体积报告应同时列出节点、端口、RX、stream、FIFO 与 TX 存储；缓冲移到结构体内部不等于减少总 RAM。本机 0.13→0.14 双向 demo 的完整装配分别为 native 782→790 B、COBS 1,046→1,054 B、length16 924→932 B，均增加 8 B。对象统计中的进程级自动实例号另占 `.bss` 4 B；不要把对象总和或 `sizeof(node)` 当作最终固件资源。

## 五、本地候选与发布配对分开验收

本轮授权只保留本地修改，不提交、推送、创建标签或 Release。[`.github/release-pair.json`](../.github/release-pair.json) 仍固定已冻结的 **0.13** 编译器 SHA，不能把该 SHA 标为 0.14。开发构建显式设置 `WIRELINK_WLC_EXECUTABLE`，使用本地 0.14 WLC。

`test_release.py` 验证脚本逻辑，不证明当前源代码的发布配对成立。新候选的 `release.py prepare`、实际冻结 SHA 与远端 CI 配对验收，需待用户允许冻结源码后完成；不属于当前本地测试的通过结论。发布时再按实际候选执行这些门禁，保留源版本、构建日志与安装消费证据。
