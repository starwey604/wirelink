# ABI 26 产品同步与收敛记录

2026-09-07；仅 `dev/wirelink-p0-hardening`，不合并 main、不打 tag、不发布。

本页记录产品依赖同步时的验收状态，不是最新性能结论。
后续结果见 [性能收敛记录](host-performance-convergence-cn.md)，
当前构建与合并条件见 [集成收尾](dev-closeout-cn.md)，
收尾前的历史问题见 [dev → main 评估](dev-main-merge-assessment-cn.md)。

## 本轮判断

Wirelink / WLC 自动身份 P0 已完成核心、生成 API、模拟平台及 H7 板内功能验收，
详见 [P0 记录](session-evolution-cn.md)。本轮没有发现必须再次修改生成 ABI 的问题；
需要收尾的是产品装配、构建配对及真实 transport 回归。不是重新设计 FCI 业务层。

配对版本：

- Wirelink：`10083e065eb7c79e196b66dff882fffbb95bd0f2`。
- WLC：`c6b6a8fa560a15c45d564aad0afd197b13682de8`，codegen ABI 26。
- FCI protocol：`47d5323ae178042b0f9d83965230dabc9fc3fb6a`，保留 revision-7 schema。

已 fetch 两个产品仓库的 main/dev；各自 `HEAD..origin/main` 均为空，无须合并 main。
libflorid 更新 Wirelink submodule；Ragtime 更新 west 的 Wirelink pin 和对应工作区模块，
未更新 Zephyr/WCH/其他 west 依赖，未修改旧构建、长期测试快照或用户的脏目录。

产品本地提交：libflorid `b2a21ce`、Ragtime_Firmwares `2089ae8`。
本轮先保留为本地 dev 提交，未 push；Ragtime 在本轮开始前已有 12 个未推送提交，
没有把它们连同本次改动一起发布到远端。Wirelink / WLC 的上述依赖提交已在远端可取。

## 已收敛

### 身份来源只保留一份实现

libflorid 删除自行混合时间、地址、计数器、`random_device` 的 session 生成和弱降级。
`FciWirelinkEndpointConfig::m_session_source` 默认使用 `Wirelink::platform`；
`initialize()` 内调用 `wl_session_next()`。用户不用提供数字编号。

Willow、Willow trigger、Willow H5 的 arm 端点和 Willow HIL 删除手动生成 arm session。
`ArmProtocol::Config::session_source` 默认使用 Wirelink Zephyr 平台来源，
`CONFIG_ARM_PROTOCOL` 选择平台/CSPRNG 依赖。H7 构建启用真实 STM32 RNG，未启用测试 RNG。
其他旧产品的 upgrade/dual 数值型底层接入不在本次 arm 迁移范围内。

两边都允许通过 C 回调注入来源，错误原样返回或映射为端点错误；零/空来源不降级。
只在初始化读取一次，不在 RPC、重传或 service 热路径读取。
构造新端点需要新身份；已初始化对象不能通过重复 init 暗中更换身份。

时间机制不改：libflorid 保留 executor 的单调时钟，ArmProtocol 保留 owner pass 的
`now_ms_`。只取平台环境中的 session 来源，不混入另一个时钟纪元。

### 同一个进程只能有一致的 Asio 后端配置

新增 Clang Debug + ASan/UBSan 回归发现旧 `transport_pipeline` 卡在 UDP quiesce。
GDB 定位到 `asio::detail::io_uring_service::deregister_io_object()` 的 futex 锁。
编译命令证实：Astrial/测试使用 `ASIO_HAS_IO_URING + ASIO_DISABLE_EPOLL`，
Wirelink UDP adapter 却按默认 epoll 编译；同名内联类型/函数产生 ODR 冲突。

libflorid 装配时让 UDP adapter 同样链接 `asio_standalone` 配置目标，统一宏和链接依赖。
没有通过禁用 io_uring 或提高等待超时掩盖问题。CTest 增加 30 秒上限；CI 新增
Linux Sanitizer 的 io_uring ON/OFF 两条路径，Windows CI 单独构建固定提交的 WLC，
关闭不匹配的自动下载。本轮未运行远端产品 CI，不能据本地测试声称 MSVC 已通过。

## 验证结果

- libflorid GCC Release：4/4；所有非 MPC C++ 示例编译通过。
- Clang ASan/UBSan：io_uring ON 4/4、OFF 4/4；ON 每项重复 20 次，共 80 次通过。
  首轮卡住由本次修复解决；原始失败保存在
  `libflorid/build/session-abi26-sanitize/Testing/Temporary/LastTest-before-asio-fix.log`。
- 身份专项：空/零/不可用/失败来源、失败后重试、共享来源重建、真实主机默认来源、
  重复 init 不消耗来源、RPC 热路径读取计数不增加。
- Python 3.14 / pybind11 3.1：扩展编译、`import _pyflorid` 通过；不是 wheel 发布验收。
- ArmProtocol：native_sim/native/64 + Cortex-M3 QEMU，2/2 配置、40/40 用例通过。
- Upgrade：Cortex-M3 QEMU，1/1 配置、20/20 用例通过；native/64 被现有平台条件过滤。
- Willow H7、Willow trigger H7、Willow H5、Willow HIL H7 全部交叉构建通过。
  固件工作区 Zephyr `577e42ad1878`，SDK 1.0.1；有既存的 H7 IWDG EWI 配置警告、
  空 library 提示及 Ruckig GCC 参数 ABI 提示，没有把这些称为无警告构建。
- HIL host、persistence host 编译通过。后续 H7 USB 功能验证通过，严格性能未通过；
  [完整实板记录](product-session-hil-cn.md)。持久化实板测试本轮未执行。

新产物位于各仓库 `build/*session-abi26*`；主机 Sanitizer 使用
`build/session-abi26-sanitize` / `build/session-abi26-epoll`。

| 镜像 | Flash | RAM | ELF SHA-256 |
| --- | ---: | ---: | --- |
| Willow H7 完整应用 | 353668 B | 117424 B | `46c2c289563f7101162f61ed14eb1c0ec0e6f3ac13ba72070380a6ca109dde57` |
| Willow HIL H7 | 167096 B | 76360 B | `e665bb9e76c306b4ebe8ddb7c967b90c9bc55e1c99e4812713a07f643e69801b` |

## 实板关卡：功能通过，性能待收敛

用户拔插并按住/松开 RESET 后连接成功，已烧录并校验上述 HIL 镜像。
90000 次长窗口回显、100 次完整生命周期、两轮 adversarial reconnect 功能均通过。
三批严格性能测试各有失败窗；关闭探针后仍出现 75.602 ms 长尾，尚未确认根因。
板端协议错误/漏 tick/deadline miss 为零。结果、测量边界和日志见
[完整实板记录](product-session-hil-cn.md)，不能将本轮归纳为“全部验收通过”。
板上保留普通 HIL 镜像，Commander/RTT 已退出；不需要用户再次按 RESET。

## 保留的独立演进议题

1. **FCI 托管 RPC v2 迁移**：当前仍是显式 operation/status 字段映射，libflorid 保留
   调用编号随机起点。它降低重启碰撞概率，但不是 v2 客户端 session 回送校验。
   需要同时调整 FCI schema、两端业务桥接和 Python 工具，另设协议配对/实板关卡；
   这次依赖升级不自动获得 v2 的完整旧响应隔离，也没有改变线上业务编码。
2. **发布闭环**：独立 WLC 分发、Python sdist 中的 Wirelink 依赖收录、wheel 构建环境的
   固定编译器安装，以及干净环境/各平台验证，留在恢复发布前处理。本次不触发发布。
3. **产品性能和长稳**：优先定位 Linux 主机接收/唤醒/回调的偶发长尾，再跑严格 HIL，
   然后决定是否加入新的长期测试。暂不修改正在运行的长期测试版本。
