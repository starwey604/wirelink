// SPDX-License-Identifier: Apache-2.0
// Explicit v1 -> v2 migration: no business schema changes.
profile version 2;
endpoint { envelope = native_packet; rpc_role = both; }
rpc Setpoint { request = SetpointRequest; response = SetpointResponse; }
rpc Control { request = ControlRequest; response = ControlResponse; }
direct Sample { delivery = unreliable; }
send Sample { delivery = unreliable; }
direct StreamChunk { delivery = unreliable; }
send StreamChunk { delivery = unreliable; }
direct Progress { delivery = unreliable; }
send Progress { delivery = unreliable; }
