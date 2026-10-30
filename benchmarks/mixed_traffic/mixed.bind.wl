profile version 2;
endpoint { envelope = native_packet; rpc_role = both; }
rpc Echo { request = EchoRequest; response = EchoResponse; }
direct Telemetry { delivery = unreliable; }
send Telemetry { delivery = unreliable; }
