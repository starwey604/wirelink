profile version 2;
endpoint { envelope=native_packet; rpc_role=both; }
rpc Query { request=Query; response=Answer; }
direct Sample { delivery=unreliable; }
send Sample { delivery=unreliable; }
