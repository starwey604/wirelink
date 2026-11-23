# 双进程 UDP 示例

[English](README.md).

C 业务与 Zephyr peer 共用 `udp_demo.wl`。`udp_client` 发起 Add 并接收 telemetry，`udp_server` 处理 Add 并发送 telemetry。每个程序只生成自己的节点。native 封装直接保留 UDP 包边界。

CMake 找到独立版 Asio 头文件和生成契约为 1 的 WLC 后，默认构建两个 UDP 程序。

```sh
cmake -S . -B build/udp
cmake --build build/udp --parallel
ctest --test-dir build/udp -R wirelink_udp_peer --output-on-failure
```

两个程序默认使用本机端口 50007 和 50008。也可传入 `LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`，server 与 client 的本地和对端参数需要互换。client 输出 `20 + 22 = 42` 和一条 telemetry。测试以三个新进程复用 client 地址和端口，并验证对端缺席时的超时。

C 程序先用 `wl_udp_open` 绑定套接字，再把 `wl_udp_transport(udp)` 传给节点的绑定入口。owner 调用 `wl_node_step` 推进收发，重新采时后调用 `wl_node_wait` 等待网络事件或截止时间。

admission 返回前编码快照，completion 在借用期复制响应。close 停止真实 I/O、推进到物理 quiescence 后释放 socket。重试/幂等/应用状态恢复由业务定义。

板卡网络配置见 [Zephyr peer](../../samples/zephyr/udp_peer/README.md)。软件互通和固件 build 不证明真实 Ethernet/Wi-Fi 性能或特定 PHY 已通过验证。
