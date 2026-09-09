profile version 1;

// One shared contract. Both programs compile this exact file.
rpc Ping { request = PingRequest; response = PingResponse; }
rpc GetInfo { request = GetInfoRequest; response = GetInfoResponse; }
rpc SetName { request = SetNameRequest; response = SetNameResponse; }
rpc ReadRegister { request = ReadRegisterRequest; response = ReadRegisterResponse; }
rpc WriteRegister { request = WriteRegisterRequest; response = WriteRegisterResponse; }
rpc GetCounters { request = GetCountersRequest; response = GetCountersResponse; }
rpc ResetCounters { request = ResetCountersRequest; response = ResetCountersResponse; }

rpc Add { request = AddRequest; response = AddResponse; }
rpc Scale { request = ScaleRequest; response = ScaleResponse; }
rpc GetLimits { request = GetLimitsRequest; response = GetLimitsResponse; }
rpc SetLimits { request = SetLimitsRequest; response = SetLimitsResponse; }

rpc SelfTest { request = SelfTestRequest; response = SelfTestResponse; }
