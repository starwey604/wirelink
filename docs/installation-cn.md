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

当前开发工作区需要 **WLC 0.4.0、生成 ABI 29**，支持共享 profile 组合、
只发送消息声明和普通 handler 共用上下文。ABI 29 不改变 ABI 26 的线上格式。
此轮尚未发布配套源码快照、预编译包或 tag；已发布的同版本工具不一定匹配。

如果已经拿到配套的内部预编译 WLC，把它解压到一个固定目录，并将可执行文件所在目录
加入 `PATH`。也可以在配置 Wirelink 时显式传入
`-DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc`。

没有匹配的预编译程序时，请先取得本轮配套的 **WLC 开发源码**，再在其目录构建：

```sh
cargo build --release --locked
./target/release/wlc codegen-abi
```

把 `target/release/wlc`（Windows 为 `wlc.exe`）的绝对路径传给 CMake；
这不会替换系统里已有的工具。WLC 源码与 Wirelink 可以放在不同位置。
最近的分发快照 `c6b6a8fa560a15c45d564aad0afd197b13682de8` 仍是 ABI 26，
不能用于此工作区。待本轮源码配对发布后再固定新提交和校验值，不假定远程分支已经更新。

## 3. 检查安装

```sh
wlc --version
wlc codegen-abi
```

本轮期望分别输出 `wlc 0.4.0` 和 `28`。请检查将传给 CMake 的那个可执行文件。
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

当前 ABI 29 没有已发布的配套源码，因此自动下载路径会明确报错，不会下载 ABI 26 代替。
请使用匹配的显式路径或 PATH 工具，建议同时设置 `WIRELINK_WLC_AUTO_DOWNLOAD=OFF`。
配对发布后才恢复固定提交、SHA-256 校验和主机 Cargo 构建的自动获取流程。
用户无需复制开发者的 worktree 布局。

现在回到 [入门：最新温度显示](getting-started-cn.md)。
