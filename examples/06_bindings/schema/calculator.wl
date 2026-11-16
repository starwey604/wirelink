version 1;

message AddRequest @id(20) {
  int32 left @id(1);
  int32 right @id(2);
}

message AddResponse @id(21) {
  int32 sum @id(1);
}

rpc Add(AddRequest) -> AddResponse;
