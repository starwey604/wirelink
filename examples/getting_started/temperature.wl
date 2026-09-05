version 1;

message Telemetry @id(10) {
  required uint32 sample @id(1);
  required int32 temperature_centi_c @id(2);
}
