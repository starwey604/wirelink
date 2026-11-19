// [docs:device_info_schema]
version 1;

message InfoRequest @id(30) {}

message InfoResponse @id(31) {
  string<31> name @id(1);
  string<15> firmware @id(2);
  uint32 query_count @id(3);
}

rpc GetInfo(InfoRequest) -> InfoResponse;
node info_client { call GetInfo; }
node info_server { serve GetInfo; }
// [/docs:device_info_schema]
