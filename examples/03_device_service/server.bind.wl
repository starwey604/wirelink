profile version 1;
endpoint {
  envelope = native_packet;
  rpc_role = server;
}
// Sending does not allocate a receive mailbox.
send DeviceTelemetry { delivery = unreliable; }
