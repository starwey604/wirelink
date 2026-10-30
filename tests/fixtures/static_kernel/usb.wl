version 1;
message SetpointRequest @id(100) { required fixed32 request_id @id(1); required fixed32 channel @id(2); required fixed32 value @id(3); }
message SetpointResponse @id(101) { required fixed32 request_id @id(1); required fixed32 status @id(2); required fixed32 applied_value @id(3); }
message Sample @id(102) { required fixed32 sequence @id(1); required fixed32 sample_tick @id(2); required fixed32 value @id(3); required fixed32 flags @id(4); }
message StreamChunk @id(103) { required fixed32 sequence @id(1); required bytes<1024> data @id(2); }
message Progress @id(104) { required fixed32 sequence @id(1); required fixed64 bytes @id(2); required fixed32 flags @id(3); }
message ControlRequest @id(105) { required fixed32 request_id @id(1); required fixed32 opcode @id(2); required fixed32 arg0 @id(3); required fixed32 arg1 @id(4); required fixed32 arg2 @id(5); required fixed32 arg3 @id(6); }
message ControlResponse @id(106) { required fixed32 request_id @id(1); required fixed32 status @id(2); required bytes<512> data @id(3); }
