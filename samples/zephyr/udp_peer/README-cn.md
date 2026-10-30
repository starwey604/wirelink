# 双设备 UDP peer

client/server 和 [desktop 对端](../../../examples/05_zephyr_udp/README.md) 共用业务 schema、managed tagged RPC wire、明确 native_packet/NONE profile。每端保留双 RPC 角色，Add 为短 typed handler；telemetry 是应用每 20 ms 尝试发送的普通消息，没有 LATEST／ACK／replay 执行层。

先在应用配置板卡 Ethernet/Wi-Fi driver、IPv4、网络缓冲和真实 fresh-instance 来源，再由 `net_config_init` 初始化网络。默认 server 为 `192.0.2.2/24:49101`，peer 为 `192.0.2.1:49100`；这些文档网段地址需在试验网络正确配置或覆盖。没有自动发现。使用 matching WLC 0.13.0：

```sh
west build -s /path/to/wirelink/samples/zephyr/udp_peer -b YOUR_BOARD \
  -d build/udp-server -- -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
west build -s /path/to/wirelink/samples/zephyr/udp_peer -b YOUR_OTHER_BOARD \
  -d build/udp-client -- -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc \
  -DEXTRA_CONF_FILE=client.conf
```

明确配置本地／peer 地址与端口。`sim.conf` 的 test random 仅供模拟，不可用作产品身份来源。desktop client 可用显式 IPv4／port 对接单板；预期输出 `20 + 22 = 42` 与 `telemetry sample=...`。

C 持有 core、port、两份含 guard 的完整 RX unit。I/O、typed callback 都在应用 owner；等待合并 kernel deadline、UDP readiness／retry 和业务动作期限，不另建 RPC timer／adapter 线程。例程 helper 只包装 step／wait／close。借用 response 在 callback 返回前复制。

重启连接需停止新提交／owner，close core，停止／join 真实 I/O，推进到 `wl_kernel_quiescent`，再用新的非零 instance identity 重用存储。未完成调用为 CLOSED；不迁移旧 handle 或复用旧身份。timeout／cancel 不回滚远端动作；请求不自动重试／去重，普通消息的新鲜度和业务幂等由应用定义。身份／完整性校验都不等于认证。

peer 的 native32/64 client/server 四配置是 build-only。真实生成 RPC／telemetry、四并发与第五次拒绝、迟到响应、取消、回绕 timeout、有限等待在 `samples/zephyr/udp_validation` 的真实 Zephyr UDP loopback 运行，desktop 双进程也真实互通。尚不据此声称具体硬件板卡／PHY 互通或吞吐合格；该验收需要实际网络硬件与配置。
