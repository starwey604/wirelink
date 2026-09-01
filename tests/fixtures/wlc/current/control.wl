version 5;

enum JointMode = 1 {
  DISABLED = 0;
  MIT = 1;
}

enum OperationStatus = 3 {
  OPERATION_OK = 0;
  OPERATION_REJECTED = 1;
}

enum ControlBulkPhase = 4 {
  CONTROL_BULK_PHASE_NONE = 0;
  CONTROL_BULK_PHASE_BEGIN = 1;
  CONTROL_BULK_PHASE_CHUNK = 2;
  CONTROL_BULK_PHASE_END = 3;
  CONTROL_BULK_PHASE_ABORT = 4;
}

enum ControlBulkStatusCode = 5 {
  CONTROL_BULK_STATUS_OK = 0;
  CONTROL_BULK_STATUS_BUSY = 1;
  CONTROL_BULK_STATUS_OUT_OF_ORDER = 2;
  CONTROL_BULK_STATUS_CONFLICT = 3;
  CONTROL_BULK_STATUS_INVALID = 4;
  CONTROL_BULK_STATUS_WRITE_FAILED = 5;
  CONTROL_BULK_STATUS_INTEGRITY_FAILED = 6;
  CONTROL_BULK_STATUS_ABORTED = 7;
  CONTROL_BULK_STATUS_TIMED_OUT = 8;
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

message ArmMitCommand = 17 {
  packed float32 controls[30] = 1;
  optional uint64 sequence = 2;
  optional float32 dt_s = 3;
}

message HomeRequest = 18 {
  optional uint32 operation_id = 1;
  optional uint32 joint_mask = 2;
}

message HomeResponse = 19 {
  optional uint32 operation_id = 1;
  optional OperationStatus status = 2;
}

message BulkBegin = 32 {
  optional fixed32 transfer_id = 1;
  optional fixed64 total_length = 2;
  optional fixed32 requested_chunk_size = 3;
  optional fixed32 object_crc32c = 4;
}

message BulkChunk = 33 {
  optional fixed32 transfer_id = 1;
  optional fixed64 offset = 2;
  optional bytes<4096> data = 3;
}

message BulkEnd = 34 {
  optional fixed32 transfer_id = 1;
  optional fixed64 total_length = 2;
  optional fixed32 object_crc32c = 3;
}

message BulkAbort = 35 {
  optional fixed32 transfer_id = 1;
  optional int32 reason = 2;
}

message BulkStatus = 36 {
  optional fixed32 transfer_id = 1;
  optional ControlBulkPhase phase = 2;
  optional ControlBulkStatusCode code = 3;
  optional fixed64 next_offset = 4;
  optional fixed32 accepted_chunk_size = 5;
}
