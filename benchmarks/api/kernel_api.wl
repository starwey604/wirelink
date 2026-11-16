version 1;
message Query @id(21) { required fixed32 sequence @id(1); }
message Answer @id(22) { required fixed32 sequence @id(1); }
message Sample @id(23) {
  required fixed32 sequence @id(1);
  required bytes<1024> payload @id(2);
}

rpc Query(Query) -> Answer;

node kernel_api {
  io = external;
  handlers = dynamic;
  call Query;
  serve Query;
  recv Sample;
  send Sample;
}
