// SPDX-License-Identifier: Apache-2.0
import "../../../examples/05_zephyr_udp/udp_demo.wl";

// Both roles exercise real bidirectional sockets without widening the teaching nodes.
node udp_validation {
  io = external;
  handlers = dynamic;
  call Add;
  serve Add;
  send Telemetry;
  recv Telemetry;
}
