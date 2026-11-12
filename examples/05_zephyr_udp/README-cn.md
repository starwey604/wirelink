# 当前 kernel 的双进程 UDP 示例

[English](README.md).

C 业务与 Zephyr peer 共用 schema/profile，显式选择 native_packet/NONE。每端都保留 RPC 发起和处理角色，Add 使用短 typed handler，telemetry 走普通 send/direct route。

应用按需保存收到的值，不依赖 LATEST、ACK 或 replay 执行层。

```sh
cmake -S . -B build/udp -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_BUILD_ASIO_UDP_ADAPTER=ON -DWIRELINK_BUILD_PLATFORM=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching/wlc
cmake --build build/udp --parallel
ctest --test-dir build/udp -R wirelink_udp_peer --output-on-failure
```

可执行程序接收 `LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT`。server 与 client 的本地/peer 参数互换，client 输出 `20 + 22 = 42` 和一条 telemetry。测试以三个新进程复用 client 地址/端口，并验证 peer 缺席时的有限超时。

C 持有 kernel、port 和两个 RX unit。薄 C++ helper 管理 UDP/等待，初始化时获取新 instance。

它在调用方 owner 上 service 和 step，再合并 kernel/I/O readiness。它不创建 RPC 表或内部线程。

admission 返回前编码快照，completion 在借用期复制响应。close 停止真实 I/O、推进到物理 quiescence 后释放 socket。重试/幂等/应用状态恢复由业务定义。

板卡网络配置见 [Zephyr peer](../../samples/zephyr/udp_peer/README.md)。软件互通和固件 build 不证明真实 Ethernet/Wi-Fi 性能或特定 PHY 已通过验证。
