# Wirelink v0.8.0 发布说明

Wirelink v0.8.0 与 WLC v0.8.0 配套发布。此版本提供 C++20 和 Python 的正式 RPC bindings：SDK 由 schema 生成，开发者维护业务协议，使用者通过类型明确的客户端调用服务。Python 包安装后无需 WLC、Rust、CMake 或编译器。

## 支持范围

- 传输与调用：UDP 上的 managed RPC；C++ 同步调用及可取消的 `Operation<T>`；Python 同步 `Client` 及 `AsyncClient`。
- 业务值：有界字符串和字节串、固定长度数组、嵌套消息、可选字段、默认值及保留未知值的枚举。响应拥有其数据，关闭连接后仍可使用。
- 生命周期：超时包含主机队列等待时间，支持取消和并发关闭。Python 异步客户端归属于创建它的事件循环，每个连接使用一个完成通知线程。
- 构建与安装：可安装的 CMake SDK、类型注解和 `py.typed`、包含预生成 C 代码的 sdist，以及静态链接原生依赖的 wheel。

本版本尚未提供订阅、Python 服务端、Serial/USB、Bulk、动态 schema、free-threaded Python 或子解释器支持。独立 Python SDK 可以在同一解释器中使用；若多个 C++ SDK 的生成 C 符号重名，须自行组织共享 codec。

## 下载和安装

[Wirelink 发布页](https://github.com/starwey604/wirelink/releases/tag/v0.8.0) 提供两个示例 SDK：发行包 `wirelink-calculator-sdk` 对应导入名 `calculator_sdk`，`wirelink-device-sdk` 对应 `device_sdk`。包版本均为 `0.8.0`。它们用于演示对应 schema，产品协议应使用 WLC 生成自己的 SDK。

| 平台 | 发布的 CPython 版本 | wheel 目标 |
| --- | --- | --- |
| Linux x86-64 | 3.10、3.11、3.12、3.13、3.14 | manylinux_2_28，glibc 2.28 或更新 |
| Windows x86-64 | 3.10、3.11、3.12、3.13、3.14 | win_amd64 |
| macOS Intel | 3.10、3.11、3.12、3.13、3.14 | macOS 13 或更新，x86_64 |
| macOS Apple Silicon | 3.10、3.11、3.12、3.13、3.14 | macOS 13 或更新，arm64 |

下载适合本机 CPython 版本和平台的 wheel，并按附件 `SHA256SUMS` 核对摘要。例如，Linux 上的 CPython 3.14 用户可在空目录中下载 calculator 的 `cp314-cp314-manylinux_2_28_x86_64` wheel，然后执行：

```sh
python -m pip install --no-index --no-deps ./wirelink_calculator_sdk-0.8.0-cp314-cp314-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl
python -c "import calculator_sdk; print(calculator_sdk.__version__)"
```

预期输出为 `0.8.0`。文件名以发布页实际附件为准；包未上传至 PyPI。使用方法见 [calculator 指南](../examples/06_bindings/GUIDE.md) 和 [device SDK](../examples/07_device_sdk/README.md)。

## 为自己的协议生成 SDK

从 [WLC 发布页](https://github.com/starwey604/wlc/releases/tag/v0.8.0) 下载并校验主机工具。WLC 提供 Windows x86-64、Linux x86-64/aarch64、macOS x86-64/arm64 包；WLC 的主机平台范围与示例 Python wheel 范围相互独立。

```sh
wlc --version
wlc codegen-abi
wlc sdk product.wl --profile product.bind.wl --name product \
  --package-version 1.0.0 --out-dir product-sdk
```

前两条命令应分别输出 `wlc 0.8.0` 和 `32`。SDK 的包名和版本由产品维护者决定。生成目录内的 README 包含 C++ 和 Python 构建步骤。源码构建需要先安装 Wirelink 0.8.0，并启用 `WIRELINK_BUILD_CPP_BINDINGS`、`WIRELINK_BUILD_PLATFORM` 和静态 PIC 构建；提供 standalone Asio 头文件。sdist 包含生成的 C 文件，构建时无需 WLC，但仍需要这些原生开发依赖。

## 兼容性与升级

Protocol v1 保持不变。WLC 生成 C ABI 为 `32`，binding source API revision 为 `2`，Python 扩展使用对应 CPython ABI；这些版本彼此独立。

从 v0.6.0 升级时，重新生成所有 C 工件和 SDK，并与 Wirelink 0.8.0 一起重编译。已有 ABI 32 开发版本也应重新生成 SDK，以更新版本、构建依赖和包元数据。不要混用旧版原生开发包。v0.8 的 CMake 包按同一 minor 版本匹配，生成的 SDK 要求精确的 Wirelink 0.8.0；C++ 不承诺跨编译器的稳定二进制 ABI。

取消操作不能撤销对端已经执行的业务。应用应使用 `with`、`async with` 或显式关闭来确定资源释放时间；连接创建只打开本地 I/O，不代表已经与对端握手。

## 发布验收

发布前须通过 WLC 的 Rust 测试、格式及 Clippy 检查，Wirelink 主机及 Zephyr 测试、C/C++ sanitizer 检查，以及上述平台的 SDK 构建测试。wheel 从解包后的 sdist 构建，再在无编译工具 PATH 的独立虚拟环境中离线安装；两个 SDK 同时连接独立 C 服务，验证同步与异步 RPC。Linux wheel 在 manylinux_2_28 容器中构建并经 auditwheel 检查和修复。

WLC 下载地址、源码提交和 SHA256 固定在 CMake 配对文件中。发布附件附带 SHA256SUMS；最终验收记录与提交可在两个仓库的 v0.8.0 发布页查看。
