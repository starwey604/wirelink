# 构建环境与安装

[English](installation.md).

纯 core 使用 ISO C11、CMake 3.21+。host Executor、C++ Session、桌面 adapter 需要 C++20；MSVC 的 C 源使用 `/experimental:c11atomics`，原子存储由 C 持有，C++ 不假定 `std::atomic` ABI。benchmark 工具使用 Python 3.11+，其余测试依对应 CMake 声明。

默认 core 和 checked-in bare-metal 示例不需要 WLC：

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/absolute/prefix -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build/core --parallel
ctest --test-dir build/core --output-on-failure
cmake --install build/core
```

生成需要 **WLC 0.13.0 / codegen 0.13**。从 [WLC v0.13.0](https://github.com/starwey604/wlc/releases/tag/v0.13.0) 下载对应平台归档，按该 Release 的 `SHA256SUMS` 校验，再解压 `wlc`（Windows 为 `wlc.exe`）。五种归档覆盖 Linux musl x86-64/ARM64、Windows x86-64、macOS Intel/ARM；归档内的 `wlc-build.json` 记录精确源码配对。

也可以从匹配的独立源码构建编译器。只有源码构建需要 Rust/Cargo，使用归档编译器和 C core 均不需要。编译器模板保留 LF。

```sh
cargo build --release --locked --manifest-path /path/to/matching-wlc/Cargo.toml
/path/to/matching-wlc/target/release/wlc --version
/path/to/matching-wlc/target/release/wlc codegen-contract
```

预期输出 `wlc 0.13.0` 和 `0.13`；`WIRELINK_WLC_EXECUTABLE` 使用该绝对路径。helper 校验 compiler 和生成 manifest，使用指定的本地工具。codegen 版本描述接口/布局，不是 wire 版本。

```sh
cmake -S . -B build/tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build build/tutorials --parallel
ctest --test-dir build/tutorials --output-on-failure
```

安装后用 `find_package(Wirelink 0.13.0 EXACT CONFIG REQUIRED)`，设置 `CMAKE_PREFIX_PATH=/absolute/prefix`。基础 C 包不要求 C++ compiler；可选 host/adapter component 必须已构建/安装，消费者 project 需启用 CXX。

Asio UDP 使用 standalone Asio，当前本地验证版本 1.38.1，commit `bbecff21a23b97c34641f0f1f08b28c91b9c77cf`。设置 `WIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/asio/include` 并启用 `WIRELINK_BUILD_ASIO_UDP_ADAPTER=ON`；UDP 示例还需 `WIRELINK_BUILD_PLATFORM=ON`。Astrial 使用 `WIRELINK_ASTRIAL_SOURCE_DIR`，native USB 另需 backend 的 libusb 依赖，均不进入 C core。

继续阅读[遥测教程](getting-started-cn.md)、[kernel 模型](kernel-cn.md)与[开发指南](development-cn.md)。安装后的 CMake/pkg-config 消费示例见[开发指南](development-cn.md#验证-installed-consumer)。
