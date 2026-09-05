# 环境准备：安装 WLC

WLC 是独立的消息代码生成工具。在电脑上运行它，把 `.wl` 定义转换成 C 文件，
再把 C 文件编译进应用或固件。Wirelink 源码仓库不包含、也不要求嵌套 WLC 仓库。
[English](installation.md)。

## 1. 准备编译工具

本教程的 Linux/macOS 命令需要 C11 编译器和 CMake 3.21 或更新版本。
使用 WLC 预编译程序时无需 Rust；只有自己构建 WLC 才需要 Rust/Cargo。

## 2. 获取与 Wirelink 匹配的 WLC

当前默认端点功能处于内部开发阶段，需要 **WLC 0.4.0、生成 ABI 19**。
这不表示旧的同版本发行包已经包含新功能。此轮没有发布新包或新 tag。

如果已经拿到配套的内部预编译 WLC，把它解压到一个固定目录，并将可执行文件所在目录
加入 `PATH`。也可以在配置 Wirelink 时显式传入
`-DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc`。

没有匹配的预编译程序时，在任意工作目录单独获取 WLC 源码并安装：

```sh
git clone --branch dev/wirelink-p0-hardening https://github.com/starwey604/wlc.git wlc-source
git -C wlc-source checkout 31df0e0dae644f380b57e9b2d69a96aa56be0f58
cargo install --path wlc-source --locked --force
```

这会把 `wlc` 安装到 Cargo 的可执行程序目录。确保该目录在 `PATH` 中。
`--force` 会替换那里原有的 WLC；需要并存时用 Cargo 的 `--root` 指定独立安装目录，
再将对应的可执行文件路径传给 CMake。`wlc-source` 与 Wirelink 项目可以放在不同位置。
开发分支会继续演进，可复现构建应记录实际使用的 WLC 提交，而不是长期追踪分支头。

## 3. 检查安装

```sh
wlc --version
wlc codegen-abi
```

本轮期望分别输出 `wlc 0.4.0` 和 `19`。
ABI 是生成 C 接口与布局的修订编号，不是线上协议版本；双方消息的字节格式没有因此改变。
若没有 `codegen-abi` 命令或输出不匹配，需要更换配套 WLC。
CMake 也会在生成前检查这两项，避免到编译固件时才发现头文件不匹配。

## 4. 关于自动下载

Wirelink 的 CMake 集成支持从 WLC GitHub Releases 下载固定版本并校验文件摘要。
但当前内部 ABI 不能假定已有匹配的公开发行产物，所以本教程关闭自动下载，使用你已安装的工具。
未来提供匹配发行包后，下载源、平台文件名和校验值应随 Wirelink 一起固定；
用户无需知道或复制我们的 worktree 布局。

现在回到 [入门：最新温度显示](getting-started-cn.md)。
