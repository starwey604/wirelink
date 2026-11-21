version 1;

// Business data only. RPC correlation and rejection belong to Wirelink.
message PingRequest @id(100) { uint32 nonce @id(1); }
message PingResponse @id(101) { uint32 nonce @id(1); }
message GetInfoRequest @id(102) {}
message GetInfoResponse @id(103) { string<31> name @id(1); }
message SetNameRequest @id(104) { string<31> name @id(1); }
message SetNameResponse @id(105) {}
message ReadRegisterRequest @id(106) { uint8 index @id(1); }
message ReadRegisterResponse @id(107) { int32 value @id(1); }
message WriteRegisterRequest @id(108) {
  uint8 index @id(1);
  int32 value @id(2);
}
message WriteRegisterResponse @id(109) {}
message GetCountersRequest @id(110) {}
message GetCountersResponse @id(111) { uint32 writes @id(1); }
message ResetCountersRequest @id(112) {}
message ResetCountersResponse @id(113) {}

message AddRequest @id(120) {
  int32 left @id(1);
  int32 right @id(2);
}
message AddResponse @id(121) { int32 sum @id(1); }
message ScaleRequest @id(122) {
  int32 value @id(1);
  int32 factor @id(2);
}
message ScaleResponse @id(123) { int32 value @id(1); }
message GetLimitsRequest @id(124) {}
message GetLimitsResponse @id(125) {
  int32 lower @id(1);
  int32 upper @id(2);
}
message SetLimitsRequest @id(126) {
  int32 lower @id(1);
  int32 upper @id(2);
}
message SetLimitsResponse @id(127) {}

message SelfTestRequest @id(130) { uint32 pattern @id(1); }
message SelfTestResponse @id(131) { uint32 observed @id(1); }

// Deliberately larger than every RPC: outbound bounds must be included too.
message DeviceTelemetry @id(200) {
  uint32 sequence @id(1);
  packed fixed32 channels[100] @id(2);
}

// Application job protocol: short acknowledgment, later ordinary completion.
message StartSelfTestRequest @id(132) { uint32 pattern @id(1); }
message StartSelfTestResponse @id(133) { uint32 job_id @id(1); }
message SelfTestProgress @id(134) {
  uint32 job_id @id(1);
  uint32 observed @id(2);
}

rpc Ping(PingRequest) -> PingResponse;
rpc GetInfo(GetInfoRequest) -> GetInfoResponse;
rpc SetName(SetNameRequest) -> SetNameResponse;
rpc ReadRegister(ReadRegisterRequest) -> ReadRegisterResponse;
rpc WriteRegister(WriteRegisterRequest) -> WriteRegisterResponse;
rpc GetCounters(GetCountersRequest) -> GetCountersResponse;
rpc ResetCounters(ResetCountersRequest) -> ResetCountersResponse;
rpc Add(AddRequest) -> AddResponse;
rpc Scale(ScaleRequest) -> ScaleResponse;
rpc GetLimits(GetLimitsRequest) -> GetLimitsResponse;
rpc SetLimits(SetLimitsRequest) -> SetLimitsResponse;
rpc SelfTest(SelfTestRequest) -> SelfTestResponse;
rpc StartSelfTest(StartSelfTestRequest) -> StartSelfTestResponse;

node device_server {
  serve Ping;
  serve GetInfo;
  serve SetName;
  serve ReadRegister;
  serve WriteRegister;
  serve GetCounters;
  serve ResetCounters;
  serve Add;
  serve Scale;
  serve GetLimits;
  serve SetLimits;
  serve SelfTest;
  serve StartSelfTest;
  send DeviceTelemetry;
  send SelfTestProgress;
}
node device_client {
  call Ping;
  call GetInfo;
  call SetName;
  call ReadRegister;
  call WriteRegister;
  call GetCounters;
  call ResetCounters;
  call Add;
  call Scale;
  call GetLimits;
  call SetLimits;
  call SelfTest;
  call StartSelfTest;
  recv DeviceTelemetry;
  recv SelfTestProgress;
}
