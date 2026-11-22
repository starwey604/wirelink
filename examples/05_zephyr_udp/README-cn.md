# 双进程 UDP 示例

[English](README.md).

C 业务与 Zephyr peer 共用 `udp_demo.wl`。`udp_client` 发起 Add 并接收 telemetry，`udp_server` 处理 Add 并发送 telemetry。每个程序只生成自己的节点。native 封装直接保留 UDP 包边界。

```sh
cmake -S . -B build/udp -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_BUILD_ASIO_UDP_ADAPTER=ON -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching/wlc
cmake --build build/udp --parallel
ctest --test-dir build/udp -R wirelink_udp_peer --output-on-failure
```

两个程序默认使用本机端口 50007 和 50008。也可传入 `LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`，server 与 client 的本地和对端参数需要互换。client 输出 `20 + 22 = 42` 和一条 telemetry。测试以三个新进程复用 client 地址和端口，并验证对端缺席时的超时。

生成节点持有端点、端口和两个 RX 槽。C 程序调用 `wl_asio_udp_create` 创建适配器，以新实例号通过 `_init_io` 绑定节点。Asio 适配器在调用方 owner 上推进 I/O 和端点，并等待其中任一就绪。

admission 返回前编码快照，completion 在借用期复制响应。close 停止真实 I/O、推进到物理 quiescence 后释放 socket。重试/幂等/应用状态恢复由业务定义。

板卡网络配置见 [Zephyr peer](../../samples/zephyr/udp_peer/README.md)。软件互通和固件 build 不证明真实 Ethernet/Wi-Fi 性能或特定 PHY 已通过验证。
