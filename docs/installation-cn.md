# 环境准备：WLC 与主机 UDP 依赖

WLC 是独立的消息代码生成工具。在电脑上运行它，把 `.wl` 定义转换成 C 文件，
再把 C 文件编译进应用或固件。Wirelink 源码仓库不包含、也不要求嵌套 WLC 仓库。
[English](installation.md)。

## 1. 准备编译工具

本教程需要 C11、C++20 编译器和 CMake 3.21 或更新版本。业务示例是 C11；
C++20 只用于主机 Asio UDP 适配器。Windows 可使用支持 C11 的近期 Visual Studio/MSVC。
默认教程构建关闭测试，不需要 Python；运行自动化双进程测试时才需要 Python 3。
使用 WLC 预编译程序时无需 Rust；只有自己构建 WLC 才需要 Rust/Cargo。

## 2. 获取与 Wirelink 匹配的 WLC

当前开发树需要 **WLC 0.12.0、生成契约 0.12**。构建本轮配套的独立 WLC
工作树；它不必位于 Wirelink 内部：

```sh
cargo build --release --locked --manifest-path /path/to/matching-wlc/Cargo.toml
/path/to/matching-wlc/target/release/wlc --version
/path/to/matching-wlc/target/release/wlc codegen-contract
```

CMake 使用该程序的绝对路径 `WIRELINK_WLC_EXECUTABLE`，并设
`WIRELINK_WLC_AUTO_DOWNLOAD=OFF`。源码应保留 LF，确保 SDK 模板确定性。
执行报告保存精确 revision、dirty 源码与哈希；已发布 WLC 0.8.0 无法生成新接口。

## 3. 检查安装

```sh
wlc --version
wlc codegen-contract
```

本轮期望分别输出 `wlc 0.12.0` 和 `0.12`。请检查将传给 CMake 的那个可执行文件。
生成契约标识生成的 C 接口与布局，不是线上协议版本。
托管与旧映射 RPC 的 payload 格式不同；托管 v1/v2 也不互通。
必须一起重建核心和生成消费者，并成对部署通信双方。
若没有 `codegen-contract` 命令或输出不匹配，需要更换配套 WLC。
CMake 也会在生成前检查这两项，避免到编译固件时才发现头文件不匹配。

## 4. 获取 standalone Asio

Asio 是可选的跨平台 C++ 网络依赖；Wirelink 核心和固件不依赖它。
在任意目录获取本轮验证的版本，不需要放进 Wirelink 工作区：

```sh
git clone --branch asio-1-38-1 --depth 1 https://github.com/chriskohlhoff/asio.git asio-source
```

对应提交为 `bbecff21a23b97c34641f0f1f08b28c91b9c77cf`。
配置时将 `WIRELINK_ASIO_INCLUDE_DIR` 指向包含 `asio.hpp` 的
`asio-source/asio/include` 绝对路径。使用已安装的同版 Asio 头文件也可以。
入门示例会启用 Asio UDP 适配器；两个程序只绑定 localhost，无需虚拟串口驱动。

## 5. 关于自动下载

目前尚未固定 0.12 的发布源码/归档配对，自动下载会明确失败，不会使用旧 0.8
生成器。请提供匹配的本地程序；已发布版本的历史流程见
[0.8 发布说明](release-v0.8.0-cn.md)。

现在回到 [入门：最新温度显示](getting-started-cn.md)。

迁移步骤及已删除接口见 [0.12](migration-0.12-cn.md).
