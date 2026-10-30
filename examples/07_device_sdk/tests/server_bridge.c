/* SPDX-License-Identifier: Apache-2.0 */
#include "server_bridge.h"
#include "service.h"
#include "device_kernel.h"
#include <string.h>
typedef struct {
  device_kernel_t endpoint;
  wl_packet_port_t port;
  uint8_t rx[2][2053];
  void (*before)(void*);
  void* before_user;
  device_test_state_t state;
} owner_t;
size_t device_test_server_size(void) { return sizeof(owner_t); }
size_t device_test_server_alignment(void) { return _Alignof(owner_t); }
wl_err_t device_test_server_prepare(void* storage, wl_host_endpoint_t* descriptor) {
  owner_t* owner=storage;
  memset(owner,0,sizeof(*owner));
  *descriptor=(wl_host_endpoint_t){owner,&owner->endpoint.core,&owner->port,2052};
  return WL_OK;
}
void device_test_server_hook(void* storage, void (*hook)(void*), void* user) {
  owner_t* owner=storage; owner->before=hook; owner->before_user=user;
}
static int32_t get_info(void* user,const info_request_t* request,info_response_value_t* response) {
  owner_t* owner=user;
  if(owner->before)owner->before(owner->before_user);
  info_request_value_t value;
  if(info_request_value_from_view(request,&value)!=WL_CODEC_OK)return WL_ERR_CORRUPT_PAYLOAD;
  return device_test_get_info(&owner->state,&value,response);
}
static int32_t configure(void* user,const configure_request_t* request,configure_response_value_t* response) {
  owner_t* owner=user;
  if(owner->before)owner->before(owner->before_user);
  configure_request_value_t value;
  if(configure_request_value_from_view(request,&value)!=WL_CODEC_OK)return WL_ERR_CORRUPT_PAYLOAD;
  return device_test_configure(&owner->state,&value,response);
}

wl_err_t device_test_server_initialize(void* storage, uint64_t instance, wl_packet_sink_fn sink,
    wl_packet_notify_fn notify, void* user) {
  owner_t* owner=storage;
  wl_packet_config_t config={.instance=instance,.rx_storage=&owner->rx[0][0],
    .rx_storage_size=sizeof(owner->rx),.rx_unit_size=sizeof(owner->rx[0]),
    .submit=sink,.notify=notify,.user=user};
  wl_err_t error=wl_packet_init(&owner->port,&config);
  if(error!=WL_OK)return error;
  const device_kernel_handlers_t handlers={.on_rpc_get_info=get_info,.on_rpc_configure=configure};
  return device_kernel_init(&owner->endpoint,&owner->port,&handlers,NULL,owner);
}
