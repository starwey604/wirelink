version 1;
message Small @id(1) {
  required string<32> text @id(1);
  optional bytes<32> body @id(2);
}
message Large @id(2) {
  required string<512> text @id(1);
  optional bytes<512> body @id(2);
}
message Nested @id(3) {
  required Large child @id(1);
  packed float32 samples[128] @id(2);
}
