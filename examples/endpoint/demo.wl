// SPDX-License-Identifier: Apache-2.0
version 1;
message Request @id(40) { int32 input @id(1); }
message Response @id(41) { int32 output @id(1); }
message Event @id(42) { bytes<9> data @id(1); }

rpc Execute(Request) -> Response;
node demo {
  handlers = dynamic;
  call Execute;
  serve Execute;
  send Event;
  recv Event;
}
