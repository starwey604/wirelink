version 1;

message InfoRequest @id(30) {}

message InfoResponse @id(31) {
  required string<31> name @id(1);
  required string<15> firmware @id(2);
  required uint32 query_count @id(3);
}
