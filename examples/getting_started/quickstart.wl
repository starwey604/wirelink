version 1;

enum AddStatus = 1 {
  ADD_OK = 0;
  ADD_REJECTED = 1;
}

message Telemetry = 10 {
  required uint32 sample = 1;
  required int32 temperature_centi_c = 2;
}

message AddRequest = 20 {
  optional uint32 operation_id = 1;
  required int32 left = 2;
  required int32 right = 3;
}

message AddResponse = 21 {
  optional uint32 operation_id = 1;
  optional AddStatus status = 2;
  required int32 sum = 3;
}
