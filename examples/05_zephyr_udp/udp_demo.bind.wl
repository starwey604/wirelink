profile version 1;

endpoint {
  envelope = native_packet;
}

rpc Add {
  request = AddRequest;
  response = AddResponse;
}

latest Telemetry {
  delivery = unreliable;
}
