# Zephyr fixed-peer packet UDP

`wirelink/zephyr/kernel_udp.h` 提供 fixed-peer native/NONE UDP，packet 存储由调用方持有。port 初始化后 open，owner service，合并 adapter readiness 和 core deadline，使用 adapter 的 wait/stop notification。先 request_stop，再 join owner task/关闭 socket，producer 不得访问释放后的存储。

按需启用 CONFIG_WIRELINK_ZEPHYR_UDP 和 CONFIG_WIRELINK_PLATFORM。samples/zephyr/udp_peer 与 host peer 共用 typed schema/profile。QEMU/native socket stack 的分配等待属于平台，不进入 core。软件测试/board build 与真实网络执行分别记录。

[内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
