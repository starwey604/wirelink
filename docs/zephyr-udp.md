# Zephyr fixed-peer packet UDP

`wirelink/zephyr/kernel_udp.h` provides fixed-peer native/NONE UDP with caller-owned packet storage. Open after port initialization, service in the owner, merge adapter readiness with core deadlines and use the adapter wait/stop notification. Request stop before joining the owning task and closing the socket; no producer may access freed storage.

Enable CONFIG_WIRELINK_ZEPHYR_UDP and CONFIG_WIRELINK_PLATFORM as needed. samples/zephyr/udp_peer uses the same typed schema/profile as the host peer. QEMU/native socket-stack behavior and allocation waits remain platform properties outside core. Software tests and board builds are distinct from network hardware execution.

[Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)
