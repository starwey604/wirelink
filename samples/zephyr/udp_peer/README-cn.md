# 两台设备上的 UDP 示例

[English](README.md)。这次不把 client/server 挤在一个 `main()` 里：

- Zephyr 服务端：[`src/server.c`](src/server.c)，提供 Add RPC，并每 20 ms 发送遥测。
- Zephyr 客户端：[`src/client.c`](src/client.c)，调用一次 `20 + 22`，然后读取遥测。
- 电脑端：[client.c](../../../examples/05_zephyr_udp/client.c)、
  [server.c](../../../examples/05_zephyr_udp/server.c)，运行同一套业务协议。

每个程序只有一个生成端点和一个 adapter。电脑、Zephyr、loopback 回归共用
`examples/05_zephyr_udp/udp_demo.wl` 与 `.bind.wl`，不手写帧或 RPC 调用编号。

## 1. 先在电脑上运行

按[安装说明](../../../docs/installation-cn.md)安装独立 WLC 与 Asio。
使用本仓库 `cmake/WirelinkWlc.cmake` 要求的开发版本/ABI；不假定仓库内有 `wlc/`。
在 Wirelink 根目录执行，替换两个工具路径：

```sh
cmake -S . -B build/udp-peer -DWIRELINK_BUILD_GETTING_STARTED=ON \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc \
  -DWIRELINK_ASIO_INCLUDE_DIR=/absolute/path/to/asio/include
cmake --build build/udp-peer --target udp_peer_client udp_peer_server --parallel 2
```

先在一个终端启动服务端：

```sh
build/udp-peer/examples/05_zephyr_udp/udp_peer_server 127.0.0.1 49101 127.0.0.1 49100
```

再在另一个终端运行客户端：

```sh
build/udp-peer/examples/05_zephyr_udp/udp_peer_client 127.0.0.1 49100 127.0.0.1 49101
```

参数依次是「本地 IP、本地端口、对端 IP、对端端口」。应看到 `20 + 22 = 42`
和 `telemetry sample=...`。服务端用 Ctrl+C 退出。Windows 使用对应的 `.exe`
路径；多配置生成器还需加 `Release` 等配置目录。

## 2. 把服务端换成 Zephyr 板子

先配置 Ethernet 板子的 PHY、引脚、驱动及真实随机源。样例默认板子地址
`192.0.2.2/24:49101`，固定电脑端 `192.0.2.1:49100`。这些地址只用于隔离测试链路，
实际局域网请选可用地址。在初始化好的 Zephyr workspace 执行：

```sh
west build -b YOUR_ETHERNET_BOARD /path/to/wirelink/samples/zephyr/udp_peer \
  -d /path/to/build/udp-peer -- \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc
west flash -d /path/to/build/udp-peer
```

把板名与路径替换为实际值。电脑运行客户端，参数改成：
`192.0.2.1 49100 192.0.2.2 49101`。防火墙需要允许相应 UDP 端口。

想让板子做客户端：构建时在 `--` 后加入 `-DEXTRA_CONF_FILE=client.conf`，
电脑服务端参数改成 `192.0.2.1 49101 192.0.2.2 49100`。客户端完成检查后退出，
再测一次需重新启动客户端。

自定义地址时，通过 `EXTRA_CONF_FILE` 加入应用 `.conf`，覆盖
`CONFIG_NET_CONFIG_MY_IPV4_ADDR`、`CONFIG_NET_CONFIG_MY_IPV4_NETMASK`、
`CONFIG_NET_CONFIG_MY_IPV4_GW`、`CONFIG_SAMPLE_WIRELINK_UDP_PEER_ADDRESS`。
本地/对端端口在样例 Kconfig 中配置。接口初始化由示例负责，不属于 Wirelink 核心。

Linux native_sim 可使用 `-b native_sim/native/64`，并在 `--` 后加
`-DEXTRA_CONF_FILE=sim.conf`；客户端为 `-DEXTRA_CONF_FILE="sim.conf;client.conf"`。
外部通信需要按 [Zephyr 官方说明](https://docs.zephyrproject.org/latest/services/connectivity/networking/native_sim_setup.html)
配置 TAP；不要启用 socket offload。`sim.conf` 使用不安全的测试随机源，
**硬件构建不得使用**。CI 只构建这两个角色，不建立 TAP，也不声称已跨设备通信。

## 3. 重连时复用一个端点

顺序是：停止提交与 owner → 关闭**生成端点**（未完成 RPC 收到取消通知）→
join 旧通知生产者 → 关闭 adapter → 在同一端点内存上重新初始化并打开新连接。
普通初始化会重新取得 session 身份，不需要手工编号，也不要复制旧身份。

这相当于重新建立连接，不会搬迁进行中的 RPC。结果不明的有副作用命令不能自动重发。
本轮测试覆盖 UDP 关闭重开、旧调用句柄失效、旧响应不能完成新调用；没有声称完成了
USB↔UDP 实板切换。切换 stream/native profile 还需匹配生成布局，必要时由应用
用 union 复用不同布局的内存；保持同一时刻仅一个端点活动，不等于两种布局自动兼容。

双方应协调切换并停止/清理旧流量。session/CRC 不是认证，也不是通用防重放机制：
旧请求、无可靠保证的遥测仍需业务侧的新鲜度策略。`latest` 是最近**收到**的值，
并不保证发送端采样时间最新；在意顺序时使用采样计数或时间字段。

## 验收范围

预算、内存、通知与关闭合同见[适配器文档](../../../docs/zephyr-udp-cn.md)。
可选插桩及测试结果见[实施记录](../../../docs/zephyr-udp-progress-cn.md)。
packet 池耗尽的等待已列为后续优化；H5 Ethernet/DMA、真实断网恢复、池容量、
CPU 与延迟仍待实板验收。
