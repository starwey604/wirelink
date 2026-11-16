version 1;

message Telemetry @id(10) {
  uint32 sample @id(1);
  int32 temperature_centi_c @id(2);
}

node temperature { send Telemetry; recv Telemetry; }
