profile version 1;

latest Telemetry {
  delivery = unreliable;
}

rpc Add {
  request = AddRequest;
  response = AddResponse;
  request_delivery = reliable;
  response_delivery = reliable;
}
