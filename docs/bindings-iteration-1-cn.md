# Binding 第一轮：calculator 同步 SDK

2026-09-12，基于 [方案 A](bindings-plan-cn.md)。本轮完成 P0 的手写垂直原型及
P1/P2 的同步基础，尚不代表 P1/P2 的通用类型生成器全部完成。

实际入口为 [calculator SDK](../examples/06_bindings/README.md)，Python 导入名
`calculator_sdk`、本地发行包名 `wirelink-calculator-sdk`、版本 `0.1.0.dev1`。
没有发布 PyPI 包或 release tag。业务代码可以这样使用：

```python
from calculator_sdk import Client, Udp

with Client.connect(Udp(peer=("127.0.0.1", 49101), bind=("127.0.0.1", 49100))) as device:
    answer = device.add(left=20, right=22, timeout=1.0)
    print(answer.sum)
```

## 已实现

- `Wirelink::cpp` 提供 C++20 Result/Error；`Wirelink::cpp_host` 提供 owning Session。
  连接句柄可移动，初始化后的 native endpoint/executor 地址不变。
- calculator C++ Client 复用 WLC 生成的 C codec/runtime 和现有同步 RPC executor 代理。
  EndpointFactory 是绑定实现的内部接入接口；业务不操作裸指针、poll 或回收槽位。
- close 串行化 owner stop/join，拒绝新调用，结清已接纳调用后释放 adapter。
  endpoint 存储最后销毁时，executor/binding 仍然有效。初始化失败会回滚已获取资源。
- Python 提供 Client、Udp、不可变 AddRequest/AddResponse、上下文管理器和类型标记。
  connect、RPC 和 close/join 的 native 阻塞区释放 GIL，native owner 不执行 Python。
- 业务拒绝、超时、取消、关闭、容量耗尽、传输和 codec 错误分别映射；保留底层各错误域。
  托管 RPC admission 的 BUSY 和 executor 的 QUEUE_FULL 都映射为 QueueFullError，原始码不变。
- SDK 有独立 CMake install/export、nanobind/scikit-build-core 打包配置、sdist 和 wheel。
  源码构建需要已安装的匹配 Wirelink 开发包；生成 C 随 sdist 分发，无需 WLC/Rust。
  Wheel 私有链接 native 依赖，采用独立 nanobind domain，附带依赖许可文本。
- `.github/workflows/bindings.yml` 配置 Linux/macOS/Windows × CPython 3.10/3.14 的构建、
  C++ 安装消费、Python 测试、sdist→wheel 和干净环境安装检查。当前未触发远端执行。
- 修正 README 中的 0.9 package 示例和 ABI 32 自动获取说明；兼容性文档对齐 0.7 开发线。
  保留 ABI 31 bootstrap 的拒绝保护，不绕过配对校验。

本轮没有修改 C core、现有 executor、WLC 源码或 frozen-v1/managed RPC wire format。
Calculator 的 C 工件由本地匹配的 WLC `0.7.0-dev / ABI 32` 生成；C++/Python façade
仍是手写参考，`wlc sdk` 尚未实现。

## 本地验收

环境为 Linux x86_64，CPython 3.14.7；Release 使用 GCC 16.2.1，sanitizer 使用 Clang 22.1.8。
开发虚拟环境按用户要求创建在根目录 `.venv`，包含 nanobind 3.0.1、scikit-build-core 1.0.3、
build 1.6.1 和 pytest 9.1.1。

| 验收 | 结果 / 证据 |
| --- | --- |
| 现有 host/UDP/基础组件 | 5/5，`build/bindings-core/Testing/Temporary/LastTest.log` |
| calculator C++ 行为套件 | 通过；100 次调用、4 线程并发、move、重复/并发 close、在途取消、超时复用、失败回滚 |
| 独立安装后的 C++ consumer | 通过，`build/bindings-consumer/Testing/Temporary/LastTest.log` |
| Python wheel 行为套件 | 24/24；包含独立 C service、GIL 释放、容量、参数边界和解释器退出；`build/bindings-pytest.xml` |
| Clang ASan/UBSan native | 通过，`build/bindings-asan-sdk/Testing/Temporary/LastTest.log` |
| Clang TSan native | 通过，`build/bindings-tsan-sdk/Testing/Temporary/LastTest.log` |
| sdist→解包→wheel | 非隔离与标准隔离构建均通过；`build/bindings-wheel-build.log`、`build/bindings-wheel-isolated-build.log` |
| 干净环境离线安装与 C peer | 通过；无 nanobind/build/pytest，无 compiler/CMake/Rust/WLC PATH |
| core-only configure/build | 通过；未探测 C++ compiler、Python 或 nanobind |
| Linux 动态依赖 | 仅系统 C/C++ 运行库，无 Wirelink/Asio/nanobind 动态库；未导出 wl_/calculator_ API 符号 |

没有运行完整 Rust/Zephyr 测试，也没有硬件测试。跨平台 CI 已配置，但本轮本地结果仅覆盖 Linux。
本地 `linux_x86_64` wheel 不是已完成 manylinux 修复的通用 Linux release 包。

最终产物位于 `build/bindings-dist/`，并已安装到工作区 `.venv`：

- wheel：`wirelink_calculator_sdk-0.1.0.dev1-cp314-cp314-linux_x86_64.whl`，194224 B；
  SHA-256 `5ca102ce6d926d1582011ce70f00594f9289c646ae23b6ff72ed84d692b1c43e`。
- sdist：`wirelink_calculator_sdk-0.1.0.dev1.tar.gz`，64330 B；
  SHA-256 `3ca20925ecb34ba03d114131b5163cc522444f0a588baeeb9ac2c8e687fe9302`。

实现中实际修正了两个集成问题：显式 CMAKE_PREFIX_PATH 会覆盖 nanobind 自动发现路径，
改为使用构建环境 Python 查询其 CMake 目录；首次容量测试暴露 BUSY 未映射为容量错误，
补齐映射后通过。Python 头必须先于系统头包含，相关 feature-test macro 重定义警告已消除。

## 下一轮边界

将验证过的消息转换和 Client façade 固化为 WLC 独立 cpp/python 生成模块，先支持有界 owned
消息和托管同步 RPC，并增加字符串、可选字段、未知 enum、packed 数组与命名冲突测试。
保持 C codec/runtime 为唯一 wire 实现。之后才扩展 native 异步 operation 状态、取消和订阅，
再提供 asyncio。Serial/USB、Bulk、动态 schema、free-threaded Python 和子解释器仍未实现。
