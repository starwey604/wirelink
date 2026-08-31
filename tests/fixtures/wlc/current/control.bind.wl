profile version 1;

latest ArmMitCommand {
  delivery = unreliable;
}

fifo JointCommand {
  delivery = reliable;
}

rpc Home {
  request = HomeRequest;
  response = HomeResponse;
  request_operation_id = operation_id;
  response_operation_id = operation_id;
  response_status = status;
  request_delivery = reliable;
  response_delivery = reliable;
}
