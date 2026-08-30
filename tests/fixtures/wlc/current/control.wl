version 2;

enum JointMode = 1 {
  DISABLED = 0;
  MIT = 1;
}

message JointCommand = 2 {
  optional fixed32 position_bits = 1;
  optional fixed32 velocity_bits = 2;
  optional fixed32 torque_bits = 3;
  optional fixed32 kp_bits = 4;
  optional fixed32 kd_bits = 5;
  optional JointMode mode = 6 [default = 0];
}

message ArmCommand = 16 {
  repeated JointCommand joints = 1;
  optional uint64 sequence = 2;
  optional string source = 3 [default = "host"];
  optional bytes extension = 4;
  optional bool enabled = 5 [default = true];
}
