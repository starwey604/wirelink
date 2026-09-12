# Binding 第二轮：WLC 生成 C++ / Python SDK

2026-09-12。本轮将第一轮手写参考变成 `wlc sdk` 生成的独立 SDK 工程。
第一轮基线已在 Wirelink 提交为 `632df31`；生成器在独立的 WLC 仓库提交为
`02657a1`（`feat: generate owned C++ and Python SDK projects`）。
使用本机既有 Git 用户身份提交，没有 push、发布包或创建 release。

## 实际生成入口

在当前工作区执行：

```sh
cargo build --manifest-path wlc/Cargo.toml --locked
wlc/target/debug/wlc sdk examples/06_bindings/schema/calculator.wl \
  --profile examples/06_bindings/schema/calculator.bind.wl \
  --name calculator --out-dir build/my-calculator-sdk
```

得到 C codec/runtime、C++ Client 和消息类型、Python dataclass/Client、nanobind
桥接、类型标记和 stub、CMake install/export、pyproject.toml、wheel/sdist 配置及许可文本。
SDK 作者无需手写桥接文件。支持 schema imports、重复 `--profile` 组合 host profile，
也支持 `--previous` 兼容性检查和 `--package-version`。

输出按相对路径排序，不嵌入本机路径和时间。重复生成内容相同时可直接执行；覆盖不同内容
需要 `--overwrite`，更改 SDK 名称必须换新目录。生成前完成 schema、profile、名称及
输出文件检查，保留未列入生成清单的用户文件。库入口为 `generate_sdk`，返回文件映射。

源码构建先安装匹配的 Wirelink 开发包，启用 CPP_BINDINGS、PLATFORM、static 和 PIC。
源码 wheel 需要 C/C++、CMake、nanobind/scikit-build-core；sdist 包含生成 C，无需
WLC/Rust。wheel 使用者不需要开发工具或单独安装 Wirelink。完整安装步骤见
[calculator 指南](../examples/06_bindings/GUIDE.md)。

## 类型与运行行为

| 能力 | C++ | Python |
| --- | --- | --- |
| 有界字符串 / bytes | `std::string` / `std::vector<uint8_t>` | `str` / 独立 `bytes` |
| 可选字段 | `std::optional<T>` | `T \| None` |
| 显式默认值 | `<field>_or_default()` | `<field>_or_default` 属性 |
| 固定 packed 数组 | `std::array<T, N>` | 检查长度并复制成 tuple |
| 嵌套消息 | 拥有数据的 struct | frozen dataclass |
| 未知 enum 值 | 保留 int32 数值 | 开放 IntEnum，不持续增长全局缓存 |

字符串上限按 UTF-8 字节计算，允许 NUL。可选字段保持“缺失”与“显式空值／零值”的
区别，读取默认值不会改变 presence。响应在后续调用和连接关闭后仍有效。Python 的阻塞
connect、RPC 和 close 释放 GIL；并发调用与关闭继续复用已验证的 Session / executor。

生成的 C++ 只通过内部不透明指针调用 `src/endpoint.c`。端点初始化、C 回调和同步
RPC 入口全部在 C 中编译；C++ 按 C 端报告的大小和对齐分配固定地址存储。
这修复了测试发现的两个关联问题：`Mode` → `mode_t` 与系统类型重名，以及将整个
C runtime 放进 C++ namespace 后造成的回调类型不匹配。只有被动 C value 声明进入内部
namespace，C enum 宏随后清除；没有禁用 UBSan 来绕过问题。

另一个行为修正：同步请求编码失败可能只返回 `WL_ERR_CORRUPT_PAYLOAD`，绑定现在将其
归类为 codec 错误，同时保留原始 local_error，不伪造更细的 codec_error。

本轮没有修改 C core、原有 C generator 或 wire format。Calculator 的既有 C 工件逐字
不变，仍是 WLC 0.7.0-dev / ABI 32。Binding source API revision 仍为预览版 1。

## 两份可复现示例

- [calculator](../examples/06_bindings/GUIDE.md)：第一轮的调用方式和行为测试继续使用，
  C++/Python façade、构建和包文件改为自动生成；手写指南及 C service 测试独立保留。
- [device SDK](../examples/07_device_sdk/README.md)：读取设备信息、回写配置，包含
  字符串、bytes、可选字段、默认值、未知 enum、数组、嵌套消息及名称转义。

```python
from device_sdk import Client, Udp

with Client.connect(Udp(peer=("127.0.0.1", 49101))) as device:
    info = device.get_info()
    print(info.settings.label_or_default)
    saved = device.configure(settings=info.settings, opaque=b"")

print(saved.settings.token)  # 关闭连接后仍可用
```

示例测试服务打印实际监听端口；运行上例时将端口替换为该输出。测试业务逻辑位于 C 中，
通过独立 endpoint/owner loop 驱动，不复用新 Client 作为服务端。

检查两个示例的全部生成文件：

```sh
.venv/bin/python tests/package/check_generated_sdks.py --wlc wlc/target/debug/wlc
```

生成清单共 68 个文件。后续修改生成器后应重新生成示例，并运行该检查。

## 本地验收

Linux x86_64；CPython 3.14.7；GCC 16.2.1；Clang 22.1.8。
工作区 `.venv` 已安装最终两套 wheel。

| 验收 | 结果与证据 |
| --- | --- |
| WLC 完整 Rust / 生成 C 回归 | 通过，`build/wlc-sdk-cargo-tests.log` |
| SDK 专项 | 8 项；确定性、原 C 输出、无界/超大/冲突拒绝、覆盖保护、imports/profile 组合、实际 C codec 往返 |
| Rust fmt / Clippy | 通过，all-targets / all-features / warnings as errors |
| WLC SDK C/C++ ASan/UBSan | 通过，`build/wlc-sdk-sanitizers.log` |
| calculator / device C++ RPC | 两套均通过，`build/bindings-sdk`、`build/device-sdk` |
| 安装后同时消费两套 C++ 包 | 通过，`build/device-consumer` |
| Python wheel 行为测试 | 50 项通过，`build/bindings-generated-pytest.xml` |
| Clang ASan/UBSan | 两套 SDK 通过，UBSan halt_on_error；`build/bindings-asan-sdk`、`build/device-sdk-asan` |
| Clang TSan | calculator 并发及关闭套件通过，`build/bindings-tsan-sdk` |
| 两套 sdist → 解包 → wheel | 标准隔离构建通过，`build/bindings-generated-wheel.log`、`build/device-sdk-wheel.log` |
| 干净 venv 离线安装两个 wheel | 无编译器/build tool PATH，与两个独立 C peer 通信通过 |
| 两个 SDK 同一解释器 | 两种导入顺序均通过，独立 native 类型及异常不冲突 |
| 生成文件一致性 | 68 个文件逐字一致 |

产物在 `build/bindings-generated-dist/` 和 `build/device-sdk-dist/`，分别是
`wirelink_calculator_sdk`、`wirelink_device_sdk` 的 `0.1.0.dev1` sdist 和
`cp314-cp314-linux_x86_64` wheel。没有发布 PyPI，也没有将本机 Linux wheel 宣称为
已经完成 manylinux 修复的发行包。

Wirelink CI 已扩展为两套 SDK 的 Linux/macOS/Windows × Python 3.10/3.14 矩阵，并增加
Clang ASan/UBSan 的 C/C++ 桥接回归；WLC CI 增加跨平台的 SDK CLI 检查。
这些 workflow 尚未在远端执行，本地结果仅证明上述 Linux 环境。

## 当前边界

仅支持有界消息、托管同步 RPC、UDP 和默认 endpoint 存储，单个请求／响应的最大编码
长度加 managed metadata 不得超过 2048 字节。名称冲突、无界/repeated 字段、mapped RPC、
非客户端 profile、非 UDP envelope 和非 RPC routes 在生成时明确拒绝。

Asyncio、订阅、Python 服务端、Serial/USB、Bulk、动态 schema、free-threaded Python
和子解释器仍是后续迭代。独立 wheel 可以共存；含重名 C 工件的多个 SDK 要链接进同一
C++ 程序，仍需共享 codec 的组合方案。本轮不承诺稳定 C++ 二进制 ABI。
