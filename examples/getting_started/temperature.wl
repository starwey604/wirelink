version 1;

message Telemetry = 10 {
  required uint32 sample = 1;
  required int32 temperature_centi_c = 2;
}
