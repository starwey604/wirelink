profile version 2;
endpoint { envelope=native_packet; rpc_role=both; }
direct Telemetry { delivery = unreliable; }
rpc Query { request = QueryRequest; response = QueryResponse; }

send Telemetry { delivery=unreliable; }
