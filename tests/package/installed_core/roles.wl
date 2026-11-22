// SPDX-License-Identifier: Apache-2.0
message Input @id(51) { int32 value @id(1); }
message Output @id(52) { int32 value @id(1); }
rpc Execute(Input) -> Output;
node client { call Execute; }
node server { serve Execute; }
