version 1;
// Example application mapping, not a new Wirelink wire-level bulk format.
message BulkCommand = 100 {
  uint32 phase = 1;
  uint32 transfer_id = 2;
  uint64 total_length = 3;
  uint32 chunk_size = 4;
  uint32 crc32c = 5;
  uint64 offset = 6;
  bytes<256> data = 7;
}
message BulkStatus = 101 {
  uint32 transfer_id = 1;
  uint32 phase = 2;
  int32 code = 3;
  uint64 next_offset = 4;
  uint32 chunk_size = 5;
}
message RebootRequest = 102 {}
message RebootResponse = 103 {}

rpc Reboot(RebootRequest) -> RebootResponse;
