profile version 2;
endpoint { envelope=native_packet; rpc_role=both; }
direct DeviceTelemetry { delivery=unreliable; }
direct SelfTestProgress { delivery=unreliable; }
