# Allocation-free packet loopback

`Wirelink::packet_loopback` and `wirelink/packet_loopback.h` connect two packet
ports sharing one owner. The core and adapter allocate no memory and own no
clock/thread. Initialize the loopback with both stable port addresses, then
initialize each C-owned port with that direction's submit/notify callbacks and
a fresh fixture identity. Generated endpoints bind those ports.

Service the loopback and both kernels from the same outer loop. Each direction
holds one submitted source until a real copy into peer RX storage succeeds.
Backpressure retains that source; source consumption and physical completion
are published only after the copy. Peer release wakes the source owner.

Close both kernels, call `wl_packet_loopback_stop` to abandon any actual retained
transfer, consume final completion and require both cores quiescent before
storage reuse. Deterministic fixture IDs in `examples/common/loopback_owner.h`
are only for isolated in-process tests. Production endpoints use fresh identity
sources. The default `examples/bare_metal_loopback.c` and tutorials are compiled
acceptance examples, with no hardware or second protocol engine.

[Kernel API](kernel-api.md).
