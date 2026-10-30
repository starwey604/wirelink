/* SPDX-License-Identifier: Apache-2.0 */
#include "server_bridge.h"
#include "service.h"
#include "calculator_kernel.h"
#include <string.h>
typedef struct {
  calculator_kernel_t endpoint;
  wl_packet_port_t port;
  uint8_t rx[2][2053];
  void (*before)(void*);
  void* before_user;

} owner_t;
size_t calculator_test_server_size(void) { return sizeof(owner_t); }
size_t calculator_test_server_alignment(void) { return _Alignof(owner_t); }
wl_err_t calculator_test_server_prepare(void* storage, wl_host_endpoint_t* descriptor) {
  owner_t* owner=storage;
  memset(owner,0,sizeof(*owner));
  *descriptor=(wl_host_endpoint_t){owner,&owner->endpoint.core,&owner->port,2052};
  return WL_OK;
}
void calculator_test_server_hook(void* storage, void (*hook)(void*), void* user) {
  owner_t* owner=storage; owner->before=hook; owner->before_user=user;
}
static int32_t add(void* user,const add_request_t* request,add_response_value_t* response) {
  owner_t* owner=user;
  if(owner->before)owner->before(owner->before_user);
  add_request_value_t value;
  if(add_request_value_from_view(request,&value)!=WL_CODEC_OK)return WL_ERR_CORRUPT_PAYLOAD;
  return calculator_test_add(NULL,&value,response);
}

wl_err_t calculator_test_server_initialize(void* storage, uint64_t instance, wl_packet_sink_fn sink,
    wl_packet_notify_fn notify, void* user) {
  owner_t* owner=storage;
  wl_packet_config_t config={.instance=instance,.rx_storage=&owner->rx[0][0],
    .rx_storage_size=sizeof(owner->rx),.rx_unit_size=sizeof(owner->rx[0]),
    .submit=sink,.notify=notify,.user=user};
  wl_err_t error=wl_packet_init(&owner->port,&config);
  if(error!=WL_OK)return error;
  const calculator_kernel_handlers_t handlers={.on_rpc_add=add};
  return calculator_kernel_init(&owner->endpoint,&owner->port,&handlers,NULL,owner);
}
