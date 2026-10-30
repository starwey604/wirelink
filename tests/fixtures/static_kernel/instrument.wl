// SPDX-License-Identifier: Apache-2.0
version 1;
message Axis @id(200) {
  required int32 coordinate @id(1);
  required string<7> label @id(2);
}
message Reading @id(201) {
  required Axis axis @id(1);
  required packed fixed32 samples[3] @id(2);
  optional bytes<17> note @id(3);
}
