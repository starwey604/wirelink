// [docs:telemetry_schema]
message Telemetry @id(10) {
  uint32 sample @id(1);
  int32 temperature_centi_c @id(2);
}
node pub { send Telemetry; }
node sub { recv Telemetry; }
// [/docs:telemetry_schema]
