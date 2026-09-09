version 1;
message EchoRequest @id(21) { required fixed32 sequence @id(1); }
message EchoResponse @id(22) { required fixed32 sequence @id(1); }
message Telemetry @id(23) {
  required fixed32 sequence @id(1);
  required fixed32 sampled_at @id(2);
}
