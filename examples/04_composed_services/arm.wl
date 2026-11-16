version 1;
message Telemetry = 10 { uint32 tick = 1; }
message QueryRequest = 11 {}
message QueryResponse = 12 { uint64 received = 1; }

rpc Query(QueryRequest) -> QueryResponse;
