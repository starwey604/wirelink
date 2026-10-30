profile version 2;
endpoint { envelope=native_packet; rpc_role=both; }
rpc Execute { request = Request; response = Response; }
rpc Download { request = Empty; response = Large; }
