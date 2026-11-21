/* SPDX-License-Identifier: Apache-2.0 */
#include "server_bridge.h"
#include "service.h"
#include "peer.h"
#include <string.h>
typedef struct {
  calculator_peer_node_t endpoint;
  void (*before)(void*);
  void* before_user;

} owner_t;
size_t calculator_test_server_size(void) { return sizeof(owner_t); }
size_t calculator_test_server_alignment(void) { return _Alignof(owner_t); }
wl_err_t calculator_test_server_prepare(void* storage, wl_host_endpoint_t* descriptor) {
  owner_t* owner=storage;
  memset(owner,0,sizeof(*owner));
  *descriptor=(wl_host_endpoint_t){owner,&owner->endpoint.core,&owner->endpoint.port, CALCULATOR_PEER_RX_WIRE_MAX, CALCULATOR_PEER_TX_WIRE_MAX};
  return WL_OK;
}
void calculator_test_server_hook(void* storage, void (*hook)(void*), void* user) {
  owner_t* owner=storage; owner->before=hook; owner->before_user=user;
}
static int32_t add(void* user,const peer_add_request_t* request,peer_add_response_t* response) {
  owner_t* owner=user;
  if(owner->before)owner->before(owner->before_user);
  return calculator_test_add(NULL,request,response);
}

wl_err_t calculator_test_server_initialize(void* storage, uint32_t instance, wl_packet_sink_fn sink,
    wl_packet_notify_fn notify, void* user) {
  owner_t* owner=storage;
  const wl_packet_io_t io = {.submit = sink, .notify = notify, .user = user};
  const calculator_peer_handlers_t handlers={.on_add=add};
  return calculator_peer_init_io(&owner->endpoint, instance, &io, &handlers,owner);
}
