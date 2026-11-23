# Wirelink CMake 选项与依赖探测收敛方案

## 1. 问题现状与目标

当前根目录 [CMakeLists.txt](CMakeLists.txt) 暴露了 **16 个顶层 `WIRELINK_BUILD_*` / `WIRELINK_INSTALL` 开关**，导致 CLion 与命令行使用门槛过高：
- 想编译 `00_telemetry` / `01_rpc` / `02_device_info`，必须手动联开 `WIRELINK_BUILD_EXAMPLES`、`WIRELINK_BUILD_WLC_EXAMPLES`、`WIRELINK_BUILD_UDP_ADAPTER`、`WIRELINK_BUILD_PLATFORM` 并手填 `WIRELINK_WLC_EXECUTABLE` 与 `WIRELINK_ASIO_INCLUDE_DIR`，少配一项便静默跳过。
- `wlc` 探测存在两套割裂逻辑（`CMakeLists.txt` 只在 `BUILD_TESTING` 内找 `wlc/target/debug/wlc`，不找文档构建的 `wlc/target/release/wlc` 与 `PATH`；`WirelinkWlc.cmake` 又另找一套）。
- `WIRELINK_BUILD_ASTRIAL_ADAPTER` 与 `WIRELINK_BUILD_ASTRIAL_USB_ADAPTER` 各自重复声明两次 `option()`；`examples/CMakeLists.txt` 重复调用两次 `wl_warnings`；核心安装重复安装了 6 个头文件。
- 核心测试（`wirelink_session_tests`、`wirelink_usb_application`、`wirelink_rpc_lifecycle`、`clock_bridge`）倒挂在示例开关下面。

### 目标
- 将顶层选项从 **16 个收敛到 10 个正交选项**（移除 7 个冗余/碎片开关，合并基准为 1 个；普通开发日常只需关注 2～3 个），消除所有“必须手动同时开启多个关联开关”的负担。
- 统一 `wlc` 与 Asio 的自动探测：在 CLion 中打开工程（只要构建了 `wlc` 并具备 Asio）即可**零手动配置**看到并运行 `telemetry_publisher`、`calculator_server` 等全部示例；缺失工具时输出清晰的 `STATUS` 提示，不静默消失。
- 保持零外部依赖裸核心构建与纯 C 安装包（`WIRELINK_CONSUMER_C_ONLY=ON`，不隐式启用 C++）完全不变。

---

## 2. 顶层选项收敛表（16 → 10）

### 保留的 10 个正交顶层选项

| Option | 默认值 | 行为与自动级联 |
| :--- | :--- | :--- |
| `WIRELINK_INSTALL` | `${PROJECT_IS_TOP_LEVEL}` | 安装核心库与本次启用的组件（保持纯 C 核心包零 C++/OS 强依赖）。 |
| `WIRELINK_BUILD_EXAMPLES` | `${PROJECT_IS_TOP_LEVEL}` | **统一示例开关（吸收 `WIRELINK_BUILD_WLC_EXAMPLES`）**：<br>• `bare_metal_loopback` 始终构建；<br>• 探测到兼容 `wlc` 时自动构建 `03_device_service`、`04_composed_services`；<br>• 同时探测到 Asio（或 `WIRELINK_BUILD_UDP_ADAPTER=ON`）时，**自动级联启用 UDP 适配器与 `platform`**，构建 `00_telemetry`、`01_rpc`、`02_device_info`、`05_zephyr_udp`；<br>• 缺少 `wlc` 或 Asio 时打印一行明确 `STATUS` 说明跳过范围。 |
| `WIRELINK_BUILD_UDP_ADAPTER` | `OFF` | 显式要求构建 `Wirelink::udp`（未找到 Asio 时立即 `FATAL_ERROR`）。 |
| `WIRELINK_BUILD_HOST_RUNTIME` | `OFF` | 构建 `Wirelink::host`（`Executor`）；当 `BUILD_TESTING=ON` 时自动一并构建 `benchmarks/owner_pass` 测试（**无需额外 `WIRELINK_BUILD_OWNER_PASS_TESTS`**，且保持 `presets.toml` 的 `owner_pass` 套件零 Asio 依赖）。 |
| `WIRELINK_BUILD_CPP_BINDINGS` | `OFF` | 构建 `Wirelink::cpp` / `Wirelink::cpp_host`，**自动级联启用** `host_runtime`、`udp`、`platform` 与 `PIC`（缺 Asio 时 `FATAL_ERROR`）。 |
| `WIRELINK_BUILD_ASTRIAL_ADAPTER` | `OFF` | 构建 Astrial 串口适配器（删除第 244 行重复 `option()`）。 |
| `WIRELINK_BUILD_ASTRIAL_USB_ADAPTER` | `OFF` | 构建 Astrial USB 适配器（删除第 153 行重复 `option()`，保持与串口独立以免强制 `libusb`）。 |
| `WIRELINK_BUILD_PACKET_TOOLS` | `OFF` | **自动级联启用** `WIRELINK_BUILD_ASTRIAL_ADAPTER`、`WIRELINK_BUILD_ASTRIAL_USB_ADAPTER` 与内部 `platform`（不再因用户漏开前置开关而 `FATAL_ERROR`）。 |
| `WIRELINK_BUILD_BENCHMARKS` | `OFF` | **合并原 `API_BENCHMARK` + `NOTIFICATION_CENSUS` + `RPC_VALIDATION_BENCHMARK` + `FOREIGN_TESTS`**：启用后按可用依赖统一构建基准目标（`notification_census` 有 `wlc` 即建；`clock_bridge` 有 `wlc` + Python3 即建；`api` 与 `rpc_validation` 共用一份 Google Benchmark 查找逻辑，找到或显式传入 `WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR` 时构建，否则 `STATUS` 提示）。 |
| `WIRELINK_BUILD_FUZZERS` | `OFF` | Clang libFuzzer 目标（保持不变）。 |

*(注：若不含维护者专用的 `PACKET_TOOLS` / `BENCHMARKS` / `FUZZERS`，普通开发面对的选项仅 5 个；`ASTRIAL_*` 按硬件按需开启。)*

### 删除的 7 个冗余/碎片开关
1. `WIRELINK_BUILD_WLC_EXAMPLES` → 并入 `WIRELINK_BUILD_EXAMPLES`（且核心 WLC 测试直接随 `BUILD_TESTING` + 探测到的 `wlc` 运行，不再挂在示例开关下）。
2. `WIRELINK_BUILD_PLATFORM` → 降为内部派生变量 `_wl_build_platform`：当 `CPP_BINDINGS`、UDP 教程示例、`PACKET_TOOLS` 启用，或 `BUILD_TESTING` 且非交叉编译（用于 `wirelink_session_tests`）时自动开启；`Wirelink::platform` 仅作为独立导出目标，不影响纯 C 消费者只链接 `Wirelink::wirelink`。
3. `WIRELINK_BUILD_OWNER_PASS_TESTS` → 并入 `WIRELINK_BUILD_HOST_RUNTIME` + `BUILD_TESTING`。
4. `WIRELINK_BUILD_API_BENCHMARK`、`WIRELINK_BUILD_NOTIFICATION_CENSUS`、`WIRELINK_BUILD_RPC_VALIDATION_BENCHMARK`、`WIRELINK_BUILD_FOREIGN_TESTS` → 合并入 `WIRELINK_BUILD_BENCHMARKS`。

---

## 3. 统一探测与解耦清单

1. **统一 `wlc` 探测（`cmake/WirelinkWlc.cmake` + 根 `CMakeLists.txt`）**
   - 在 `cmake/WirelinkWlc.cmake` 中增加可复用的 `_wirelink_wlc_find(out_compiler out_reason)`，并在根 `CMakeLists.txt` 顶部（**不限于 `BUILD_TESTING`**）统一调用：
     - 若用户显式设置了 `WIRELINK_WLC_EXECUTABLE`：必须通过 `_wirelink_wlc_validate_executable`，无效时立即 **`FATAL_ERROR`**（防止 CI 静默跳过测试）。
     - 若未显式设置：按序软探测 `${PROJECT_SOURCE_DIR}/wlc/target/release/wlc` → `${PROJECT_SOURCE_DIR}/wlc/target/debug/wlc` → `PATH` 中的 `wlc`（兼容 Windows `.exe`），并用 `_wirelink_wlc_validate_executable` 过滤不兼容的全局旧版本；若命中则记录到内部变量供本轮使用，未命中则打印清晰 `STATUS` 提示。
   - 根 `CMakeLists.txt` 中的 `usb.wl` / `instrument.wl` 测试夹具统一复用 `wirelink_wlc_generate(...)`，消除手写 `add_custom_command`。

2. **统一 Asio 探测（区分显式硬报错与示例软启用）**
   - 顶层软查找 `WIRELINK_ASIO_INCLUDE_DIR`：依次检索用户显式值、`${PROJECT_SOURCE_DIR}/.tools/asio/asio/include`、`${PROJECT_SOURCE_DIR}/../astrial/3rdparty/asio/include`、环境变量 `ASIO_INCLUDE_DIR` 以及系统默认 include 路径。
   - 显式开启 `WIRELINK_BUILD_UDP_ADAPTER=ON` 或 `WIRELINK_BUILD_CPP_BINDINGS=ON` 时，未找到 Asio 立即 `FATAL_ERROR`。
   - 仅开启默认 `WIRELINK_BUILD_EXAMPLES=ON` 且同时具备兼容 `wlc` 与 Asio 时，自动设置内部 `_wl_build_udp = ON` 与 `_wl_build_platform = ON`。
   - 仅在真正启用 C++ 组件（`host`、`udp`、`astrial*`、`cpp_bindings`、`BUILD_TESTING`、需 C++ 的 benchmark）时调用 `enable_language(CXX)`，保证 `-DBUILD_TESTING=OFF -DWIRELINK_BUILD_EXAMPLES=OFF` 与已安装核心包纯 C 不沾 C++。

3. **解除核心测试对示例目录的倒挂依赖**
   - 将 `calculator_fixture_protocol`（`examples/01_rpc/lifecycle/calculator_fixture.wl`）与 `wirelink_rpc_lifecycle`（`tests/core/node_lifecycle.c`）迁入核心测试配置区（`BUILD_TESTING` 且探测到 `wlc` 即构建），解除 `tests/core/clock_bridge.cmake` 与 `wirelink_rpc_lifecycle` 对 `WIRELINK_BUILD_EXAMPLES` 的依赖。
   - `wirelink_session_tests` 与 `wirelink_usb_application` 同样直接挂在 `BUILD_TESTING` + 探测到的 `wlc` 下，不再绑定示例开关。

4. **顺手清理与全仓引用同步**
   - 删除 `CMakeLists.txt` 中重复的 `option(WIRELINK_BUILD_ASTRIAL_*)`、冗余的 6 行单文件 `install(FILES include/wirelink/codec.h ...)` 以及 `examples/CMakeLists.txt` 中重复的 `wl_warnings`。
   - 提取 `benchmarks/api` 与 `benchmarks/rpc_validation` 重复的 Google Benchmark 查找逻辑到公共处；同步更新 `benchmarks/presets.toml`、`benchmarks/*/README*.md`、`tests/core/README.md`、`.github/workflows/host.yml`、`.github/workflows/bindings.yml`、`examples/*/README*.md`、`docs/development.md` 与 `site/src/content/docs/zh-cn/overview/quick-start.mdx`。

---

## 4. 本地验收清单

```sh
# 1. 纯 C 核心零外部依赖配置与安装（模拟 short-enum-abi / C-only 消费方）
cmake -S . -B build/c-only -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DWIRELINK_BUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=$PWD/build/c-only-install
cmake --build build/c-only --parallel && cmake --install build/c-only
cmake -S tests/package/installed_core -B build/c-only-consumer -DCMAKE_PREFIX_PATH=$PWD/build/c-only-install -DWIRELINK_CONSUMER_C_ONLY=ON
cmake --build build/c-only-consumer --parallel && ctest --test-dir build/c-only-consumer --output-on-failure

# 2. 零手工选项开箱体验（自动发现 wlc/target/release/wlc 与 Asio 并自动编出全部示例与核心测试）
cargo build --release --locked --manifest-path wlc/Cargo.toml
cmake -S . -B build/auto -DCMAKE_BUILD_TYPE=Release -DWIRELINK_ASIO_INCLUDE_DIR=<asio_include_dir>
cmake --build build/auto --parallel --target telemetry_publisher telemetry_subscriber calculator_server calculator_client
ctest --test-dir build/auto --output-on-failure

# 3. C++ Bindings + Host Runtime（单开关自动级联 host/udp/platform/owner_pass）
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release -DWIRELINK_BUILD_CPP_BINDINGS=ON -DWIRELINK_ASIO_INCLUDE_DIR=<asio_include_dir>
cmake --build build/host --parallel && ctest --test-dir build/host --output-on-failure

# 4. Benchmarks 与文档校验
ctest --test-dir build/auto -L benchmark --output-on-failure
python3 tests/tutorials/doc_sources.py
```
