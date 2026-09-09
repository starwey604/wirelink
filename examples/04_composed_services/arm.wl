version 1;
message Telemetry = 10 { required uint32 tick = 1; }
message QueryRequest = 11 {}
message QueryResponse = 12 { required uint64 received = 1; }
