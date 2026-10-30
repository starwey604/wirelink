// SPDX-License-Identifier: Apache-2.0
version 1;
message Request @id(40) { required int32 input @id(1); }
message Response @id(41) { required int32 output @id(1); }
message Event @id(42) { required bytes<9> data @id(1); }
