version 1;

enum Mode @id(1) { MODE_IDLE = 0; MODE_ACTIVE = 1; }
message InfoRequest @id(2) {}
message Settings @id(3) {
  optional string<12> label @id(1) [default = "设备"];
  bool enabled @id(2);
  optional Mode mode @id(3) [default = 1];
  bytes<8> token @id(4);
  packed float32 gains[3] @id(5);
  optional packed fixed64 counters[2] @id(6);
  optional int64 floor @id(7) [default = -9223372036854775808];
  optional uint64 ceiling @id(8) [default = 18446744073709551615];
  optional string<8> class @id(9);
  optional uint16 timeout @id(10);
}
message InfoResponse @id(4) {
  string<32> name @id(1);
  Settings settings @id(2);
}
message ConfigureRequest @id(5) {
  Settings settings @id(1);
  optional bytes<4> opaque @id(2);
  optional Settings backup @id(3);
}
message ConfigureResponse @id(6) {
  Settings settings @id(1);
  optional bytes<4> opaque @id(2);
  optional Settings backup @id(3);
  uint32 count @id(4);
}
message Numbers @id(7) {
  int8 i8 @id(1);
  uint8 u8 @id(2);
  int16 i16 @id(3);
  uint16 u16 @id(4);
  int32 i32 @id(5);
  uint32 u32 @id(6);
  int64 i64 @id(7);
  uint64 u64 @id(8);
  fixed32 f32 @id(9);
  fixed64 f64 @id(10);
  float32 single @id(11);
  float64 double @id(12);
  optional packed float64 doubles[2] @id(13);
  packed fixed32 words[2] @id(14);
  optional int64 offset @id(15) [default = -7];
  optional int32 minimum @id(16) [default = -2147483648];
  optional bool flag @id(17) [default = true];
  optional string<16> text @id(18) [default = "a\"@NAME@"];
}

rpc GetInfo(InfoRequest) -> InfoResponse;
rpc Configure(ConfigureRequest) -> ConfigureResponse;
