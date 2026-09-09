version 1;
// Example application mapping, not a new Wirelink wire-level bulk format.
message BulkCommand = 100 {
  required uint32 phase = 1;
  required uint32 transfer_id = 2;
  required uint64 total_length = 3;
  required uint32 chunk_size = 4;
  required uint32 crc32c = 5;
  required uint64 offset = 6;
  required bytes<256> data = 7;
}
message BulkStatus = 101 {
  required uint32 transfer_id = 1;
  required uint32 phase = 2;
  required int32 code = 3;
  required uint64 next_offset = 4;
  required uint32 chunk_size = 5;
}
message RebootRequest = 102 {}
message RebootResponse = 103 {}
