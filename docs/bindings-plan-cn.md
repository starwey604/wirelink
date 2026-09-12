# Python / C++ binding 探索与实施草案

2026-09-11。基于 Wirelink `f285d36`、本地独立 WLC checkout `85b1bdc`。
这是最初的设计草案。第一轮的实际实现与验收见
[迭代记录](bindings-iteration-1-cn.md)和 [calculator SDK](../examples/06_bindings/README.md)。
第二轮已实现 `wlc sdk`，实际命令与验收见 [生成器迭代记录](bindings-iteration-2-cn.md)。
下文保留早期方案对比；AsyncClient 和动态 schema 仍是拟议接口，包名尚未确认可注册。
目标是让应用直接操作类型化消息、RPC 和订阅，并有可安装、可测试、可维护的正式 SDK。
最初探索只记录计划；后续实现按迭代记录推进，尚未发布 binding。

## 当前工作区的事实

| 位置 | 已有能力 | 对 binding 的意义 |
| --- | --- | --- |
| [API 边界](api-boundary.md)、[endpoint.h](../include/wirelink/endpoint.h) | C11 core、单 owner、借用事件、生成的 endpoint driver | 继续作为协议和生命周期基础，不把 Python 或 host 依赖加入 core |
| [WLC](../wlc/src/codegen.rs)、[values 生成器](../wlc/src/value_codegen.rs) | C codecs、可静态定界消息的 owned values、endpoint、托管同步/异步 RPC | C++ 和 Python 的类型信息应来自同一语义模型，不手工维护字段映射 |
| [host executor](../runtime/host/include/wirelink/host/executor.hpp) | C++20 后台 owner、同步 RPC 代理、LATEST outbound 队列 | 有线程基础，但没有通用的非阻塞跨线程 RPC/取消/订阅入口 |
| [executor 实现](../runtime/host/src/executor.cpp) | 同步 RpcJob 使用调用者栈及条件变量；代理槽位为 8 | 不能直接把现有代理包装成可提前返回的 Future，需要有独立寿命的操作状态 |
| [clock bridge](../tests/clock_bridge/)、[storage bridge](../tests/endpoint_storage/) | C++ / Python ctypes 消费 C ABI、native 时钟、opaque handle、创建回滚和重复关闭测试 | 可复用为验收基础；目前不是可安装的通用 binding |
| [Asio UDP](../adapters/asio_udp/include/wirelink/asio/udp_adapter.hpp) | 已安装的 CMake target、endpoint attach、可中断 readiness wait | 最适合第一个不需要硬件的正式示例 |
| [Astrial serial](../adapters/astrial/include/wirelink/astrial/serial_adapter.hpp)、[USB](../adapters/astrial_usb/include/wirelink/astrial/usb_bulk_adapter.hpp) | C++20 平台适配器，以原始 link 接入 | 要补统一的 endpoint 生命周期桥接；serial 公开头暴露 Astrial/tl 类型，不能直接当稳定 SDK 边界 |
| [构建集成](../cmake/WirelinkWlc.cmake)、[Host CI](../.github/workflows/host.yml) | codec/runtime 分开生成、manifest 校验、三平台安装消费 | 扩展现有生成和安装入口，不另造独立构建系统 |

`wlc/` 在父仓库显示为未跟踪目录，内部是独立 Git checkout；编译器变更应在 WLC 仓库维护。
Python 脚本主要用于测试和基准编排；本仓库没有正式 Python 包或 schema 级 C++ façade。
[历史 Python 发布记录](dev-closeout-cn.md)对应下游 libflorid，不能算 Wirelink 本身已具备 wheel。

当前构建要求 WLC `0.7.0-dev / ABI 32`，但
[bootstrap](../cmake/WirelinkWlcBootstrap.cmake)的源码配对仍是 ABI 31，并会主动拒绝自动回退。
本机 `wlc/target/release/wlc` 实际为 `0.6.0 / ABI 31`，`target/debug/wlc` 为 `0.7.0-dev / ABI 32`。
README 仍有 `find_package(Wirelink 0.9 ...)` 示例。正式 binding 发布前必须先统一这些入口。

## 两种交付路线的实际体验

这里的 developer 指设备/SDK 作者，user 指安装 SDK 后编写应用的人；同一个人也可以同时承担两种角色。
两种路线都能提供 `Client`、类型提示、同步调用和 `await`；区别在 schema 如何进入 native runtime。
以下沿用仓库 [calculator.wl](../examples/01_rpc/calculator.wl) 的 `AddRequest` / `AddResponse` 和 `Add` 服务。

### A：生成并编译 schema 专用 SDK

SDK 作者维护 `.wl` 和 `.bind.wl`。设备端继续使用现有 C 生成；host 侧新增 C++ / Python 生成目标。

```sh
# 拟议命令：由 WLC 生成 C++ façade、Python 扩展源及打包模板。
wlc sdk calculator.wl --profile calculator.bind.wl \
  --language cpp --language python --package calculator_sdk --out-dir sdk

# SDK 作者的环境需要匹配的 WLC、C/C++ 工具链和 Python 构建依赖。
python -m pip install -e ./sdk
python -m build ./sdk
# 多平台交付由 CI 使用 cibuildwheel 构建并测试。
```

概念产物如下：

```text
sdk/
  pyproject.toml
  CMakeLists.txt
  generated/                # 现有 C codec/runtime + 新 C++/nanobind glue
  include/calculator/client.hpp
  src/calculator_sdk/
    __init__.py
    client.py               # Python 便利接口及 asyncio 适配
    client.pyi
    py.typed
    _native.<平台扩展后缀>   # 构建产物
```

Python 使用者只安装匹配平台的 wheel，不需要 WLC、Rust、CMake 或 C++ 编译器：

```python
from calculator_sdk import Client, Udp

with Client.connect(Udp(peer=("127.0.0.1", 49101))) as client:
    answer = client.add(left=20, right=22, timeout=1.0)
    print(answer.sum)  # 42
```

```python
from calculator_sdk import AsyncClient, Udp

async def main():
    async with AsyncClient.connect(Udp(peer=("127.0.0.1", 49101))) as client:
        answer = await client.add(left=20, right=22, timeout=1.0)
        print(answer.sum)
```

C++ 使用者通过安装包消费目标和生成类型。下列 API 同样只是提案；采用 C++20 和自有
`Result<T>`，不默认要求 C++23 的 `std::expected`。

```cmake
find_package(Wirelink CONFIG REQUIRED)
find_package(CalculatorSdk CONFIG REQUIRED)
target_link_libraries(app PRIVATE CalculatorSdk::client)
```

```cpp
#include <calculator/client.hpp>
using namespace std::chrono_literals;

auto opened = calculator::Client::connect(
    wirelink::UdpOptions{.peer_address = "127.0.0.1", .peer_port = 49101});
if (!opened) return report(opened.error());
auto client = std::move(opened.value());
auto answer = client.add(calculator::AddRequest{.left = 20, .right = 22}, 1s);
if (!answer) return report(answer.error());
std::cout << answer.value().sum << '\n';
```

添加 RPC 时，SDK 作者重新生成、编译并发布 wheel；用户升级 SDK 后获得新方法和类型提示。
用户升级到有兼容 wheel 的版本不需编译；从 sdist 安装或没有匹配 wheel 时仍需构建工具。
这是当前架构改动较小的一条路线：继续执行 WLC 生成的 C codec/runtime。

### B：通用 native runtime + schema 描述数据

Wirelink 维护者发布包含通用 schema 执行能力的 native wheel。SDK 作者只生成 Python 类型、
服务方法和描述数据，设备端仍使用现有 WLC C 生成器。

```sh
# 拟议命令：输出纯 Python SDK 和经过验证的描述文件，不生成 schema 专用扩展。
wlc sdk calculator.wl --profile calculator.bind.wl \
  --language python --runtime dynamic --package calculator_sdk --out-dir sdk

# SDK 作者无需 C/C++ 编译器。仍需 WLC 工具及 Python 打包后端。
python -m build ./sdk
# schema SDK 的 wheel 可以是 calculator_sdk-...-py3-none-any.whl。
```

```text
sdk/src/calculator_sdk/
  __init__.py
  messages.py               # dataclass 或等价的强类型 owned values
  client.py                 # 生成的 add(...)、返回类型和 docstring
  py.typed
  calculator.wld.json       # 拟议 descriptor 格式，包含 schema/profile 语义
```

普通使用者仍可运行上面相同的 `from calculator_sdk import Client, Udp` 示例。
`pip` 自动安装其依赖的通用 `wirelink` native wheel。新 schema SDK 的发布不触发多平台 native 编译。
`py3-none-any` 只描述 schema SDK，底层 Wirelink runtime 本身仍须有对应平台 wheel。

工具开发者也可以直接加载描述文件，不发布 SDK：

```python
import wirelink

api = wirelink.load_descriptor("calculator.wld.json")
with api.Client.connect(wirelink.Udp(peer=("127.0.0.1", 49101))) as client:
    answer = client.call("Add", {"left": 20, "right": 22}, timeout=1.0)
    print(answer["sum"])
```

若要求直接加载原始 `.wl`，可再设计 `wirelink.load_schema(path, profiles=[...])`。
这一步需要随包提供 WLC parser/semantic 能力或匹配的预编译 WLC；不是读取现有 manifest 就能实现。
它无需用户安装 C++ 编译器，但有额外的编译器工具分发与启动成本。

动态 `call("Add", dict)` 路径的字段错误在运行时报告；生成的 `client.py` 可以保持类型提示和
显式方法。动态 runtime 与强类型 SDK 并不矛盾。

C++ 正式入口仍优先保留 A 的编译期类型封装。只有确有调试器、插件或运行时换 schema 的需求时，
才把 B 的动态 descriptor API 公开给 C++，避免普通 C++ 用户退回字符串和通用 value 容器。

| 比较项 | A：schema 专用 native SDK | B：通用 runtime + 纯 Python schema SDK |
| --- | --- | --- |
| SDK 作者的 C/C++ 工具链 | 需要 | 不需要；Wirelink runtime 维护者需要 |
| Python 使用者安装 | 项目 SDK 平台 wheel | 项目纯 Python wheel + runtime 平台 wheel |
| 新增 schema/RPC | 重新生成并构建各平台 SDK | 生成描述和 Python 文件，runtime 支持该特性时无需重编译 |
| 类型提示 | 可以完整生成 | 可以完整生成；直接动态加载时依赖运行时检查 |
| 与现有 C runtime 的复用 | 直接复用生成实现 | 可复用 link/RPC/LATEST 等底层，但需通用 codec/schema endpoint assembly |
| 主要维护成本 | 每个 SDK 的 wheel 矩阵、生成转换代码 | 通用执行器、descriptor 版本及与 C 生成实现的语义一致性 |
| 性能判断 | 跨语言转换和 owned 拷贝需实测 | 还需量化 descriptor 调度与动态值成本；不能预设不够快 |

暂推荐先完成 A 的最小闭环，同时保持 `Client`/消息/异常层不依赖 native 对象布局，
以后可以在相同 Python API 后换用 B。若用户最重视交互加载任意 schema，则把 B 的
descriptor/runtime 验证提前；不能把 B 当成 nanobind 之后免费的附加功能。

## 共用的接口与实现边界

1. **核心与宿主分层。** 保持 `Wirelink::wirelink` 为 C11、无堆、无线程。新增
   `Wirelink::cpp` 提供类型与轻量包装，`Wirelink::cpp_host` 提供拥有资源的桌面 façade，
   Python 绑定后者。依赖关系是 Python → host façade → 生成 C runtime → C core。
   Embedded C++ 可选择轻量目标；普通桌面 Client 可以在连接时分配稳定存储。
2. **稳定地址与关闭。** Client 使用 PImpl 或等价稳定分配；移动外层 handle 不移动初始化后的
   endpoint/executor。显式 `close()` 幂等，析构不抛异常。关闭先拒绝新操作，由 owner 调用
   generated close 完成 quiesce/终态通知，唤醒并等待已接纳调用退出，然后释放 adapter、
   endpoint，最后释放仍被 endpoint 借用的 executor/binding 存储。不能仅调用 generic close
   而漏掉生成 RPC 的通知。初始化各阶段失败要回滚。
3. **线程入口。** 初版同步 RPC 可复用当前代理。正式异步另加有界 operation 状态与 owner
   command queue，包含提交/取消/发送/订阅控制；禁止业务线程直接调用生成 async 或 step。
   deadline 从 API 接纳计算，包含排队时间。callback 不借用提交者栈；取消与完成竞争只产生
   一个终态，close 结清所有已接纳操作。拒绝接纳必须立即报告。
4. **值所有权。** 请求入队前形成 owned snapshot；响应、FIFO/LATEST、direct 回调的数据
   在 lease/callback 结束前复制到 binding 自有存储。默认 `bytes`/`str` 返回独立值。
   将来只对拥有存储的对象提供只读 memoryview，不把 RX ring 的寿命暴露给 Python。
5. **订阅语义。** 由一个读取者消费底层 SPSC route，再向多个订阅者分发。LATEST 允许合并，
   FIFO 满时按显式策略报错/拒绝；分别提供统计。RPC 终态不能共用可丢消息的遥测队列，
   接纳前必须预留可保存完成结果的容量。现有 submitLatest 是发送端合并，不能替代接收订阅。
6. **Python 调度。** native owner 不执行 Python 业务代码，也不拥有 Python 引用。阻塞 RPC、
   native 等待及 close/join 释放 GIL。asyncio 阶段先用每 Client 一个可关闭的 Python 分发线程
   等待 native 完成队列，再通过 `loop.call_soon_threadsafe` 交付结果，避免每 RPC 一线程。
   Future/回调保存在 Python 侧，取消转成 owner 命令；关闭事件循环、取消后晚到的结果、
   解释器退出和 callback 调用 close 都要有明确语义和测试。
7. **错误。** C++ 返回 `Result<T>`；Python 将 timeout、cancelled、closed、queue full、
   transport、codec、业务 rejection 映射为明确异常。保留 `wl_rpc_completion_t` 的分离错误域
   和 rejection 数值。发送接纳、link ACK 和 RPC 业务成功仍是三个不同结果；不自动重试有副作用 RPC。

类型生成规则必须先约定，避免“方便”破坏 schema 语义：

- required 字段在 Python 构造/调用时必填；optional 的缺席与显式默认值分开保存。
  可以用 `None` 表示缺席，并提供读取 schema 默认值的便利访问器；不能默认把所有字段标为 present。
- 整数检查符号和宽度；enum 接受未知 `int32` 值并可往返，不能用封闭枚举拒绝兼容的新值。
- string 校验 UTF-8，长度界限按字节，保留内嵌 NUL；bytes 独立复制。
- packed 固定数组检查元素类型和长度；NumPy 作为可选能力，显式处理 dtype、布局和字节序。
- Python float32 数值转换不自动承诺保留所有 NaN payload 位；需要逐位保持的场景提供原始编码/
  typed-buffer 路径，并用 C codec 向量验收。
- 首版 generated Client 支持能生成 owned values/default endpoint 的有界 schema。现有无界
  bytes/string 和普通 repeated 不满足这一条件，必须清晰报出不支持字段或进入显式 advanced 路径，
  不能因为换语言就假设已有完整自动内存布局。

Python 服务端处理器建议放在异步阶段之后，用现有 deferred RPC 把请求交给 Python 分发器，
再在 owner 完成/拒绝响应。不要直接在 native owner 的 immediate handler 中调用 Python。
Bulk 是基于产品 schema、服务策略和运行时组合的额外能力，应以现有 composed-services 为基础
单独设计进度、取消和 chunk 存储；不能只绑定 `wl_bulk_*` 就称为易用的文件传输 API。

## 生成、打包与兼容性

建议选择 nanobind 作为新的 Python 扩展实现，scikit-build-core 对接现有 CMake，cibuildwheel
构建发布矩阵。这是适合新 façade 的工程判断，不以 binding 库自己的性能数据替代 Wirelink 实测。
[nanobind 打包文档](https://nanobind.readthedocs.io/en/latest/packaging.html)给出这套工具的直接集成路径；
[scikit-build-core](https://scikit-build-core.readthedocs.io/en/latest/)和
[cibuildwheel](https://cibuildwheel.pypa.io/en/stable/)是对应构建与多平台 wheel 工具。
若已有下游 pybind11 类型互操作成为必须需求，先用同一小型原型验证再定库，不维护两套 backend。

WLC 增加独立 `cpp_codegen` / `python_codegen` 模块，从已验证的 SemanticModel 和 profile
生成语言 façade、转换器和类型声明，不解析生成 C 文本。沿用 codec/runtime 分离和多个 profile
组合；命名必须处理关键字、大小写映射碰撞、不同 schema 模块的类型隔离和确定性产物。
生成输出增加语言 binding API revision，与 C codegen ABI 分开记录。

A 的第一个打包实现采用**每项目一个自包含扩展**：私有链接匹配 C core、host 支持和该 schema
生成代码；在 Python 层统一便利类/异常。隐藏 native 符号，不跨扩展传递 C++/endpoint 指针，
测试两个 SDK 同进程导入与使用。先接受重复链接的体积成本；共享 `_wirelink_native` 与 schema
二进制插件之间的稳定 ABI 是另一个项目，不能在没有版本握手时直接共享 STL 对象。

B 需要新增版本化 descriptor：包括字段类型/ID/约束/default/presence、RPC/route/profile
语义和资源上限。当前 manifest 主要记录 provenance 和部分 bounded fields，不能充当它。
通用 runtime 应复用现有 link 与 RPC primitives，并将 codec、managed RPC v2、保留策略和
错误语义逐项与生成 C 对端交叉验证。它不需要改变 frozen compact-v1 协议。

初版 CI 建议覆盖 Linux x86_64/aarch64、Windows x86_64、macOS arm64，并明确 macOS x86_64
是否纳入交付。CPython 先按目标消费环境确定版本范围并构建普通 wheel；用 3.10–3.14 作为
候选矩阵，按依赖实际支持验证。abi3 可在后续缩减矩阵，不能将其等同于 Wirelink/WLC ABI 稳定。
free-threaded Python 和子解释器另设验证关卡，不随常规 CPython 支持自动承诺。

分发验收至少包括：

- wheel 在无源码 checkout、无 WLC、无 C/C++ 编译器的干净环境安装并与 C 对端通信。
- 从解包 sdist 构建 wheel 再安装。sdist 含必要生成代码、native 源码或完整可审计依赖获取方式，
  不依赖工作区嵌套 `wlc/`；发布生成文件时校验 manifest 与配套库。
- 使用安装后的 CMake package 构建独立 C++ 消费者，core-only 配置不探测 Python 或引入 host。
- 对 wheel 依赖做 auditwheel/delocate/Windows DLL 检查；USB 单独处理 libusb 和 Windows CRT。
  UDP 可先交付；serial/USB 必须各自完成生命周期和依赖打包验收才列为支持。
- 区分协议 v1、C API 版本、C codegen ABI、语言 binding revision、schema/profile identity。
  identity 只用于诊断和本地配对，不能当作 v1 已有远端握手或协商机制。

## 建议的实施顺序与验收

| 阶段 | 具体交付 | 通过条件 |
| --- | --- | --- |
| P0：配套版本和 API 原型 | 修复版本入口；手写 calculator 最小 façade；确定 A/B 优先级 | 匹配编译器解析正确；SDK 示例无需用户写 storage、poll 或回收代码 |
| P1：C++ 同步闭环 | stable Client 所有权、统一 UDP options、Result、生成 C++ values/services、安装目标 | C++↔C RPC、失败回滚、重复 close、close 时 pending call、独立安装消费通过 |
| P2：Python 同步闭环 | nanobind 薄层、上下文管理器、类型声明、A 的 SDK 生成/打包模板 | 同一 calculator 与 owned-string 例子可 pip 安装运行，结果可跨 close 保存；阻塞时其他 Python 线程可运行 |
| P3：有界异步与消息 | native operation queue、取消、LATEST/FIFO 分发、asyncio、deferred Python server | 超时含排队；满队列显式失败；取消/完成/关闭竞争只终结一次；慢订阅者不阻塞 owner |
| P4：正式多平台交付 | serial/USB 统一接入、wheel/sdist/安装消费者矩阵、文档、版本与 changelog | 无开发工具环境可用；两 SDK 同进程；拔插与资源释放通过相应平台验证 |
| P5：按需求扩展 | B 的 descriptor/runtime、Bulk、NumPy、advanced storage | 与 C 实现的跨语言 conformance 和业务语义一致；记录性能、内存、队列容量与限制 |

P0/P1 是两路线的共同基础。若 B 是首要体验，P2 改为先做通用 runtime 的 calculator/
owned-string 垂直原型，再完成 descriptor 生成；不要先开发完整 schema 解释器才验证端到端。
WLC 的生成变更、Wirelink 的 façade/host/打包变更应分别提交，按配对版本更新 CI 与 fixtures。

验收沿用现有 C / WLC 测试，再新增 bindings 目录下的行为测试和 package 消费测试。
重点覆盖字段/presence/UTF-8/未知 enum/packed 数组、C↔C++↔Python 对端、owned 生命周期、
并发 close/cancel、超时和容量边界；ASan/UBSan 用 native 层查寿命，TSan 用并发原型查数据竞争。
性能以 C 对端为共同基线，区分 codec 转换、跨语言调用、端到端 RPC、订阅积压和 idle CPU；
记录复制次数及内存上限，不提前承诺零拷贝、实时性或固定倍率。

GIL 释放可使用 [nanobind 的 GIL guards](https://nanobind.readthedocs.io/en/latest/api_core.html#gil-management)。
跨线程交付 asyncio 使用 [Python 官方的 call_soon_threadsafe](https://docs.python.org/3/library/asyncio-eventloop.html#asyncio.loop.call_soon_threadsafe)，
并处理目标 loop 已关闭的情况。

## 本次验证范围

在 `/tmp/wirelink-binding-review-20260911` 配置独立 Release 构建，显式指定本地 WLC debug
`0.7.0-dev / ABI 32`，关闭自动下载，启用 host/getting-started/storage，复用已有 Asio headers。
选取现有 C++/Python clock/storage bridge、host executor 与 RPC executor 验证基础：
前五项在沙箱内通过；RPC executor 首次在打开本机 UDP socket 时失败，获准在沙箱外单独重跑后通过。
六项最终均通过，未运行全套 C / Rust / Zephyr 测试，也未进行硬件测试。
CTest 日志位于上述构建目录的 `Testing/Temporary/`。
这些测试只能证明可复用的现有基础；不代表草案中的正式 binding 已实现或跨平台通过。
