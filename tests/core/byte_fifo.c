/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/byte_fifo.h"
#include "wirelink/kernel_wire.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
static wl_byte_fifo_t fifo;
static uint8_t staging[257];
static void *produce(void *unused) {
  (void)unused;
  for (unsigned at = 0U; at < 1000000U;) {
    wl_span_t span;
    if (wl_byte_fifo_reserve(&fifo, &span) == WL_ERR_WOULD_BLOCK) continue;
    size_t n = span.length;
    if (n > 1000000U - at) n = 1000000U - at;
    for (size_t i = 0; i < n; ++i) span.data[i] = (uint8_t)(at + i);
    CHECK(wl_byte_fifo_commit(&fifo, n) == WL_OK); at += (unsigned)n;
  }
  return NULL;
}
static uint8_t wire[256]; static size_t wire_length;
static wl_packet_submit_t sink(void *user, const uint8_t *p, size_t n, wl_packet_token_t token) {
  (void)user; (void)token; CHECK(n <= sizeof(wire)); memcpy(wire,p,n); wire_length=n;
  return WL_PACKET_SENT;
}
static wl_packet_stream_t stream;
static void feed(const uint8_t *bytes, size_t n) {
  while (n) {
    wl_span_t span; CHECK(wl_byte_fifo_reserve(&fifo,&span)==WL_OK);
    size_t take = span.length < n ? span.length : n;
    memcpy(span.data,bytes,take); CHECK(wl_byte_fifo_commit(&fifo,take)==WL_OK);
    bytes+=take;n-=take;
    int error=wl_byte_fifo_to_cobs(&fifo,&stream,16);
    CHECK(error==WL_OK || error==WL_ERR_IO);
  }
}
int main(void) {
  CHECK(wl_byte_fifo_init(&fifo,(wl_span_t){staging,sizeof(staging)},NULL,NULL)==WL_OK);
  pthread_t thread; CHECK(pthread_create(&thread,NULL,produce,NULL)==0);
  for (unsigned at=0;at<1000000U;) {
    wl_span_t span=wl_byte_fifo_peek(&fifo);
    for (size_t i=0;i<span.length;++i) CHECK(span.data[i]==(uint8_t)(at+i));
    CHECK(wl_byte_fifo_consume(&fifo,span.length)==WL_OK);at+=(unsigned)span.length;
  }
  CHECK(pthread_join(thread,NULL)==0 && !wl_byte_fifo_readable(&fifo));
  wl_span_t span;CHECK(wl_byte_fifo_reserve(&fifo,&span)==WL_OK);
  CHECK(wl_byte_fifo_commit(&fifo,span.length+1)==WL_ERR_INVALID_ARG);
  CHECK(wl_byte_fifo_commit(&fifo,0)==WL_OK);
  wl_byte_fifo_note_gap(&fifo);CHECK(wl_byte_fifo_take_gap(&fifo));CHECK(!wl_byte_fifo_take_gap(&fifo));
  static wl_packet_port_t port;static uint8_t rx[2][256],tx[256];
  wl_packet_config_t pc={.instance=888,.rx_storage=&rx[0][0],.rx_unit_size=256,.rx_storage_size=sizeof(rx),
      .submit=wl_packet_stream_cobs_crc16_submit,.user=&stream};
  CHECK(wl_packet_init(&port,&pc)==WL_OK);
  wl_packet_stream_config_t sc={.port=&port,.tx_storage={tx,sizeof(tx)},.physical_submit=sink,.maximum_frame_size=100};
  CHECK(wl_packet_stream_cobs_crc16_init(&stream,&sc)==WL_OK);
  uint8_t canonical[100];memset(canonical+4,33,96);
  CHECK(wl_kernel_native_none_seal(canonical,sizeof(canonical),12,96)==WL_OK);
  wl_packet_token_t token;CHECK(wl_packet_tx_submit(&port,canonical,100,&token)==WL_OK);
  (void)wl_packet_tx_take(&port,&token);
  CHECK(wl_byte_fifo_init(&fifo,(wl_span_t){staging,31},NULL,NULL)==WL_OK);
  for (unsigned i=0;i<1000;++i) {
    feed(wire,wire_length);wl_packet_borrow_t borrow;
    CHECK(wl_packet_rx_borrow(&port,&borrow)==WL_OK && borrow.length==100);
    CHECK(!memcmp(borrow.data,canonical,100));CHECK(wl_packet_rx_release(&port,&borrow)==WL_OK);
  }
  feed(wire,wire_length/2);wl_byte_fifo_note_gap(&fifo);
  CHECK(wl_byte_fifo_cobs_ready(&fifo,&stream));
  feed(wire+wire_length/2,wire_length-wire_length/2);
  CHECK(!wl_packet_rx_ready(&port));
  feed(wire,wire_length);wl_packet_borrow_t borrow;
  CHECK(wl_packet_rx_borrow(&port,&borrow)==WL_OK);CHECK(wl_packet_rx_release(&port,&borrow)==WL_OK);
  CHECK(wl_packet_stream_abort_rx(&stream)==WL_OK);wl_packet_stop(&port);CHECK(wl_packet_quiescent(&port));
}
