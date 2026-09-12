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

当前工作区需要 **WLC 0.8.0、生成 ABI 32**，增加静态 schema 导入和借用式
direct 路由，不改变 ABI 31 的线上格式。已发布的 v0.6.0 / ABI 31 二进制不能
生成本轮 API。CMake 默认自动下载配套主机工具；若要自行构建，请在独立 WLC
源码仓库检出 `v0.8.0` 标签后执行：

```sh
cargo build --release --locked
./target/release/wlc codegen-abi
```

把 `target/release/wlc`（Windows 为 `wlc.exe`）的绝对路径传给 CMake；
这不会替换系统里已有的工具。WLC 源码与 Wirelink 可以放在不同位置。
配套源码提交与归档 SHA256 固定在 CMake 中。

## 3. 检查安装

```sh
wlc --version
wlc codegen-abi
```

本轮期望分别输出 `wlc 0.8.0` 和 `32`。请检查将传给 CMake 的那个可执行文件。
ABI 是生成 C 接口与布局的修订编号，不是线上协议版本。
托管与旧映射 RPC 的 payload 格式不同；托管 v1/v2 也不互通。
必须一起重建核心和生成消费者，并成对部署通信双方。
若没有 `codegen-abi` 命令或输出不匹配，需要更换配套 WLC。
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

CMake 按构建主机选择 Windows x86-64、Linux x86-64/aarch64（静态 musl）或
macOS x86-64/arm64 发布包，固定下载 `v0.8.0`，校验硬编码 SHA256 后再检查
编译器版本和 ABI。这五个平台不需要安装 Rust 或本地 WLC 源码；其他主机使用
固定源码归档及主机 Rust/Cargo 构建。

离线构建请设置 `WIRELINK_WLC_EXECUTABLE` 的绝对路径，以及
`WIRELINK_WLC_AUTO_DOWNLOAD=OFF`。用户无需复制开发者的 worktree 布局。

现在回到 [入门：最新温度显示](getting-started-cn.md)。
