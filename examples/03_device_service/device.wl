version 1;

// Business data only. RPC correlation and rejection belong to Wirelink.
message PingRequest @id(100) { required uint32 nonce @id(1); }
message PingResponse @id(101) { required uint32 nonce @id(1); }
message GetInfoRequest @id(102) {}
message GetInfoResponse @id(103) { required string<31> name @id(1); }
message SetNameRequest @id(104) { required string<31> name @id(1); }
message SetNameResponse @id(105) {}
message ReadRegisterRequest @id(106) { required uint8 index @id(1); }
message ReadRegisterResponse @id(107) { required int32 value @id(1); }
message WriteRegisterRequest @id(108) {
  required uint8 index @id(1);
  required int32 value @id(2);
}
message WriteRegisterResponse @id(109) {}
message GetCountersRequest @id(110) {}
message GetCountersResponse @id(111) { required uint32 writes @id(1); }
message ResetCountersRequest @id(112) {}
message ResetCountersResponse @id(113) {}

message AddRequest @id(120) {
  required int32 left @id(1);
  required int32 right @id(2);
}
message AddResponse @id(121) { required int32 sum @id(1); }
message ScaleRequest @id(122) {
  required int32 value @id(1);
  required int32 factor @id(2);
}
message ScaleResponse @id(123) { required int32 value @id(1); }
message GetLimitsRequest @id(124) {}
message GetLimitsResponse @id(125) {
  required int32 lower @id(1);
  required int32 upper @id(2);
}
message SetLimitsRequest @id(126) {
  required int32 lower @id(1);
  required int32 upper @id(2);
}
message SetLimitsResponse @id(127) {}

message SelfTestRequest @id(130) { required uint32 pattern @id(1); }
message SelfTestResponse @id(131) { required uint32 observed @id(1); }

// Deliberately larger than every RPC: outbound bounds must be included too.
message DeviceTelemetry @id(200) {
  required uint32 sequence @id(1);
  required packed fixed32 channels[100] @id(2);
}
