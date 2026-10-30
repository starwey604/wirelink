# Native packet UDP adapters

Asio `PacketUdpAdapter` sends one native/NONE packet per fixed-peer datagram. The single owner performs socket I/O and core work using a C-owned port; there is no adapter worker. A receive capacity greater than a legal frame detects truncation. Latched waits handle RX/completion/stop with finite deadlines; stop closes port/socket admission; the caller joins the owner and any external notify producers before reclaiming storage. There is no automatic ARQ or peer negotiation.

Enable WIRELINK_BUILD_ASIO_UDP_ADAPTER and provide matching standalone Asio headers. The current two-process C example is examples/05_zephyr_udp; generated C remains C-owned and the small C++ host helper bridges I/O. tests/core and tests/package/packet_adapters validate fixed-peer filtering, boundary faults, backpressure, cancellation and installed consumers. Local UDP tests do not qualify a physical network.

[Migration checklist](migration-0.13.md) · [Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)
