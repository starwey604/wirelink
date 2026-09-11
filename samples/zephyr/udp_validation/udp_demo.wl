version 1;

message AddRequest @id(20) {
  required int32 left @id(1);
  required int32 right @id(2);
}

message AddResponse @id(21) {
  required int32 sum @id(1);
}

message Telemetry @id(22) {
  required uint32 sample @id(1);
}
