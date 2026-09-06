version 1;
message Request @id(2) {
  required int32 input @id(1);
  required string<31> name @id(2);
}
message Response @id(3) {
  required int32 output @id(1);
  required string<31> name @id(2);
}
message Empty @id(4) {}
message Large @id(5) { required bytes<2031> data @id(1); }
