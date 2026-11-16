version 1;

message Telemetry @id(10) {
  uint32 sample @id(1);
  int32 temperature_centi_c @id(2);
}

message AddRequest @id(20) {
  int32 left @id(1);
  int32 right @id(2);
}

message AddResponse @id(21) {
  int32 sum @id(1);
}

rpc Add(AddRequest) -> AddResponse;
node loopback { send Telemetry; recv Telemetry; call Add; serve Add; }
