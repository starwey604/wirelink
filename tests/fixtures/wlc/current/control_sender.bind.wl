profile version 1;

rpc Home {
  request = HomeRequest;
  response = HomeResponse;
  request_operation_id = operation_id;
  response_operation_id = operation_id;
  response_status = status;
  request_delivery = reliable;
  response_delivery = reliable;
}
