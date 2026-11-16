version 1;
message Request @id(2) { required int32 input @id(1); }
message Response @id(3) { required int32 output @id(1); }

rpc Execute(Request) -> Response;

node session {
  io = external;
  handlers = dynamic;
  call Execute;
  serve Execute;
}
