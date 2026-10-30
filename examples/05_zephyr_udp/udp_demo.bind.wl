profile version 2;

endpoint {
  envelope = native_packet;
}

rpc Add {
  request = AddRequest;
  response = AddResponse;
}

direct Telemetry {
  delivery = unreliable;
}

send Telemetry { delivery=unreliable; }
