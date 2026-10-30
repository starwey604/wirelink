/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)

typedef struct {
  usb_kernel_t endpoint;
  wl_packet_port_t port;
  uint8_t rx[2][2048];
  uint8_t frames[16][1100];
  size_t lengths[16];
  unsigned frames_count;
  unsigned effects;
  unsigned samples;
  unsigned chunks;
  unsigned progresses;
  unsigned errors;
  unsigned callbacks;
  unsigned running;
  wl_packet_submit_t sink_result;
  const uint8_t *leased;
  size_t leased_length;
  wl_packet_token_t token;
  wl_kernel_result_t results[8];
  uint32_t values[8];
} fixture_t;

typedef struct { fixture_t *fixture; unsigned index; } context_t;

static wl_packet_submit_t sink(void *user, const uint8_t *data, size_t length,
                               wl_packet_token_t token) {
  fixture_t *f = user;
  if (f->sink_result == WL_PACKET_BUSY || f->sink_result == WL_PACKET_FAILED)
    return f->sink_result;
  CHECK(f->frames_count < 16U && length <= sizeof(f->frames[0]));
  memcpy(f->frames[f->frames_count], data, length);
  f->lengths[f->frames_count++] = length;
  f->leased = data;
  f->leased_length = length;
  f->token = token;
  return f->sink_result;
}

static int32_t setpoint(void *user, const setpoint_request_t *request,
                         setpoint_response_value_t *response) {
  fixture_t *f = user;
  ++f->effects;
  if (request->channel == UINT32_MAX) return -77;
  *response = (setpoint_response_value_t){
      .has_request_id = true, .request_id = request->request_id,
      .has_status = true, .status = 0U,
      .has_applied_value = true, .applied_value = request->value
  };
  return 0;
}

static int32_t control(void *user, const control_request_t *request,
                        control_response_value_t *response) {
  fixture_t *f = user;
  ++f->effects;
  response->has_request_id = response->has_status = response->has_data = true;
  response->request_id = request->request_id;
  response->status = 0U;
  if (request->opcode == 1U) {
    response->data.length = 512U;
    for (unsigned i = 0U; i < 512U; ++i) response->data.data[i] = (uint8_t)i;
  } else if (request->opcode == 2U) f->running = 1U;
  else if (request->opcode == 3U) f->running = 0U;
  else if (request->opcode == 4U) response->data.length = 513U; /* Encode error. */
  return 0;
}

static void sample(void *user, const sample_t *value) {
  fixture_t *f = user;
  CHECK(value->sequence == UINT32_MAX && value->sample_tick == 17U &&
        value->value == 19U && value->flags == 23U);
  ++f->samples;
}

static void chunk(void *user, const stream_chunk_t *value) {
  fixture_t *f = user;
  CHECK(value->data.length == 1024U && value->sequence == UINT32_MAX);
  for (size_t i = 0U; i < value->data.length; ++i) CHECK(value->data.data[i] == (uint8_t)i);
  ++f->chunks;
}

static void progress(void *user, const progress_t *value) {
  fixture_t *f = user;
  CHECK(value->sequence == UINT32_MAX && value->bytes == UINT64_MAX && value->flags == UINT32_MAX);
  ++f->progresses;
}

static void error(void *user, uint16_t id, wl_err_t code) {
  fixture_t *f = user;
  CHECK(code != WL_OK);
  (void)id;
  ++f->errors;
}

static void init(fixture_t *f, uint64_t instance) {
  wl_packet_config_t port = {
      .instance = instance, .rx_storage = &f->rx[0][0], .rx_unit_size = 2048U,
      .rx_storage_size = sizeof(f->rx), .submit = sink, .user = f
  };
  CHECK(wl_packet_init(&f->port, &port) == WL_OK);
  usb_kernel_handlers_t handlers = {
      .on_rpc_setpoint = setpoint, .on_rpc_control = control,
      .on_message_sample = sample, .on_message_stream_chunk = chunk,
      .on_message_progress = progress
  };
  CHECK(usb_kernel_init(&f->endpoint, &f->port, &handlers, error, f) == WL_OK);
}

static int ready(fixture_t *f, uint32_t now) {
  int has_deadline;
  uint32_t deadline;
  return wl_kernel_ready(&f->endpoint.core, now, &has_deadline, &deadline);
}

static void run(fixture_t *f, uint32_t now) {
  for (unsigned i = 0U; i < 32U; ++i) {
    CHECK(usb_kernel_step(&f->endpoint, now) == WL_OK);
    if (!ready(f, now)) return;
  }
  CHECK(0);
}

static void deliver(fixture_t *to, const fixture_t *from, unsigned frame) {
  wl_packet_claim_t claim;
  CHECK(frame < from->frames_count);
  CHECK(wl_packet_rx_claim(&to->port, 2048U, &claim) == WL_OK);
  memcpy(claim.span.data, from->frames[frame], from->lengths[frame]);
  CHECK(wl_packet_rx_commit(&to->port, &claim, from->lengths[frame]) == WL_OK);
}

static void setpoint_done(void *user, wl_kernel_result_t result,
                           const setpoint_response_t *response) {
  context_t *c = user;
  fixture_t *f = c->fixture;
  ++f->callbacks;
  f->results[c->index] = result;
  if (result.outcome == WL_KERNEL_SUCCESS) {
    CHECK(response != NULL && response->status == 0U);
    f->values[c->index] = response->applied_value;
  } else CHECK(response == NULL);
}

static void control_done(void *user, wl_kernel_result_t result,
                          const control_response_t *response) {
  fixture_t *f = user;
  ++f->callbacks;
  CHECK(result.outcome == WL_KERNEL_SUCCESS && response != NULL);
  if (response->data.length != 0U) {
    CHECK(response->data.length == 512U);
    for (size_t i = 0U; i < response->data.length; ++i)
      CHECK(response->data.data[i] == (uint8_t)i);
  }
}

static void control_failed(void *user, wl_kernel_result_t result,
                             const control_response_t *response) {
  fixture_t *f = user;
  CHECK(response == NULL && result.outcome == WL_KERNEL_TIMED_OUT);
  f->results[7] = result;
  ++f->callbacks;
}

static setpoint_request_value_t request(uint32_t value) {
  return (setpoint_request_value_t){.has_request_id = true, .request_id = UINT32_MAX,
      .has_channel = true, .channel = 1U, .has_value = true, .value = value};
}

static void test_four_calls_both_directions(void) {
  fixture_t a = {0}, b = {0};
  init(&a, 1000U); init(&b, 2000U);
  context_t context[5];
  wl_kernel_call_handle_t calls[5];
  for (unsigned i = 0U; i < 4U; ++i) {
    context[i] = (context_t){&a, i};
    setpoint_request_value_t value = request(UINT32_MAX - i);
    CHECK(usb_kernel_setpoint_start(&a.endpoint, &value, 0U, 100U, setpoint_done,
        &context[i], &calls[i]) == WL_OK);
    memset(&value, 0, sizeof(value)); /* Submission owns its encoded snapshot. */
  }
  CHECK(a.callbacks == 0U && a.frames_count == 0U);
  setpoint_request_value_t value = request(123U);
  CHECK(usb_kernel_setpoint_start(&a.endpoint, &value, 0U, 100U, setpoint_done,
      &context[0], &calls[4]) == WL_ERR_WOULD_BLOCK);
  run(&a, 0U);
  CHECK(a.frames_count == 4U);
  const unsigned order[] = {2U, 0U, 3U, 1U};
  for (unsigned i = 0U; i < 4U; ++i) { deliver(&b, &a, order[i]); run(&b, 1U); }
  CHECK(b.effects == 4U && b.frames_count == 4U);
  for (unsigned i = 0U; i < 4U; ++i) { deliver(&a, &b, order[i]); run(&a, 2U); }
  CHECK(a.callbacks == 4U);
  for (unsigned i = 0U; i < 4U; ++i) CHECK(a.values[i] == UINT32_MAX - i);
  deliver(&a, &b, 0U); run(&a, 3U);
  CHECK(a.callbacks == 4U);
  context[4] = (context_t){&b, 0U};
  CHECK(usb_kernel_setpoint_start(&b.endpoint, &value, 3U, 100U, setpoint_done,
      &context[4], &calls[4]) == WL_OK);
  run(&b, 3U); deliver(&a, &b, 4U); run(&a, 4U);
  deliver(&b, &a, 4U); run(&b, 5U);
  CHECK(b.callbacks == 1U && b.values[0] == 123U && a.effects == 1U);
}

static void test_backpressure_and_lease(void) {
  fixture_t a = {0}, b = {.sink_result = WL_PACKET_BUSY};
  init(&a, 3000U); init(&b, 4000U);
  context_t contexts[4];
  wl_kernel_call_handle_t handles[4];
  for (unsigned i = 0U; i < 4U; ++i) {
    contexts[i] = (context_t){&a, i};
    setpoint_request_value_t value = request(i);
    CHECK(usb_kernel_setpoint_start(&a.endpoint, &value, 0U, 10U, setpoint_done,
        &contexts[i], &handles[i]) == WL_OK);
  }
  run(&a, 0U);
  for (unsigned i = 0U; i < 4U; ++i) { deliver(&b, &a, i); run(&b, 0U); }
  CHECK(b.effects == 4U && b.frames_count == 0U);
  deliver(&b, &a, 0U); run(&b, 0U);
  CHECK(b.effects == 4U && !ready(&b, 0U));
  b.sink_result = WL_PACKET_SENT;
  wl_packet_activity(&b.port);
  run(&b, 0U);
  CHECK(b.effects == 5U && b.frames_count == 5U);

  fixture_t c = {.sink_result = WL_PACKET_STARTED}, d = {0};
  init(&c, 5000U); init(&d, 6000U);
  context_t callback = {&c, 0U};
  setpoint_request_value_t value = request(37U);
  wl_kernel_call_handle_t call;
  CHECK(usb_kernel_setpoint_start(&c.endpoint, &value, 0U, 100U, setpoint_done,
      &callback, &call) == WL_OK);
  run(&c, 0U);
  uint8_t saved[64];
  memcpy(saved, c.leased, c.leased_length);
  deliver(&d, &c, 0U); run(&d, 0U);
  deliver(&c, &d, 0U); run(&c, 0U);
  CHECK(c.callbacks == 1U && c.values[0] == 37U);
  CHECK(memcmp(saved, c.leased, c.leased_length) == 0);
  CHECK(wl_packet_tx_active(&c.port));
  CHECK(wl_packet_tx_publish(&c.port, c.token, WL_PACKET_DRAINED) == WL_OK);
  run(&c, 0U);
  CHECK(!wl_packet_tx_active(&c.port));
}

static void test_control_and_all_messages(void) {
  fixture_t a = {0}, b = {0};
  init(&a, 7000U); init(&b, 8000U);
  for (unsigned opcode = 1U; opcode <= 3U; ++opcode) {
    control_request_value_t value = {.has_request_id = true, .request_id = UINT32_MAX,
        .has_opcode = true, .opcode = opcode, .has_arg0 = true, .arg0 = UINT32_MAX,
        .has_arg1 = true, .arg1 = UINT32_MAX, .has_arg2 = true, .arg2 = UINT32_MAX,
        .has_arg3 = true, .arg3 = UINT32_MAX};
    wl_kernel_call_handle_t call;
    CHECK(usb_kernel_control_start(&a.endpoint, &value, 0U, 100U, control_done, &a, &call) == WL_OK);
    run(&a, 0U); deliver(&b, &a, opcode - 1U); run(&b, 0U);
    CHECK(b.running == (opcode == 2U));
    deliver(&a, &b, opcode - 1U); run(&a, 0U);
  }
  CHECK(a.callbacks == 3U);
  sample_value_t telemetry = {.has_sequence = true, .sequence = UINT32_MAX,
      .has_sample_tick = true, .sample_tick = 17U, .has_value = true, .value = 19U,
      .has_flags = true, .flags = 23U};
  CHECK(usb_kernel_send_sample(&a.endpoint, &telemetry) == WL_OK);
  run(&a, 0U); CHECK(a.lengths[3] == 24U);
  const uint8_t golden[] = {0xa4, 0, 0, 102, 0x0d, 0xff, 0xff, 0xff, 0xff,
      0x15, 0, 0, 0, 17, 0x1d, 0, 0, 0, 19, 0x25, 0, 0, 0, 23};
  CHECK(memcmp(a.frames[3], golden, sizeof(golden)) == 0);
  deliver(&b, &a, 3U); run(&b, 0U); CHECK(b.samples == 1U);
  stream_chunk_value_t stream = {.has_sequence = true, .sequence = UINT32_MAX,
      .has_data = true, .data = {.length = 1024U}};
  for (unsigned i = 0U; i < 1024U; ++i) stream.data.data[i] = (uint8_t)i;
  CHECK(usb_kernel_send_stream_chunk(&b.endpoint, &stream) == WL_OK);
  run(&b, 0U); CHECK(b.lengths[3] == 1036U);
  deliver(&a, &b, 3U); run(&a, 0U); CHECK(a.chunks == 1U);
  progress_value_t observation = {.has_sequence = true, .sequence = UINT32_MAX,
      .has_bytes = true, .bytes = UINT64_MAX, .has_flags = true, .flags = UINT32_MAX};
  CHECK(usb_kernel_send_progress(&a.endpoint, &observation) == WL_OK);
  run(&a, 0U); deliver(&b, &a, 4U); run(&b, 0U); CHECK(b.progresses == 1U);
  CHECK(USB_KERNEL_REQUEST_FRAME_MAX == 53U && USB_KERNEL_REPLY_FRAME_MAX == 544U &&
        USB_KERNEL_MESSAGE_FRAME_MAX == 1036U);
}

static void test_generated_failures(void) {
  fixture_t a = {0}, b = {0};
  init(&a, 9000U); init(&b, 10000U);
  context_t context = {&a, 0U};
  setpoint_request_value_t value = request(11U);
  value.channel = UINT32_MAX;
  wl_kernel_call_handle_t call;
  CHECK(usb_kernel_setpoint_start(&a.endpoint, &value, 0U, 10U, setpoint_done, &context, &call) == WL_OK);
  run(&a, 0U);
  /* Fixed32 managed business fields must retain all uint32 values. */
  wl_kernel_frame_t frame;
  wl_kernel_rpc_metadata_t metadata;
  CHECK(wl_kernel_native_none_read(a.frames[0], a.lengths[0], &frame) == WL_OK);
  CHECK(wl_kernel_rpc_read(frame.payload, frame.length, 1, &metadata) == WL_OK);
  const uint8_t business[] = {0x08,0xff,0xff,0xff,0xff,0x0f,
      0x10,0xff,0xff,0xff,0xff,0x0f,0x18,0x0b};
  CHECK(metadata.length == sizeof(business) && !memcmp(metadata.body, business, sizeof(business)));
  deliver(&b, &a, 0U); run(&b, 0U); deliver(&a, &b, 0U); run(&a, 0U);
  CHECK(a.results[0].outcome == WL_KERNEL_REJECTED && a.results[0].status == -77);
  CHECK(usb_kernel_setpoint_start(&a.endpoint, &value, 0U, 1U, setpoint_done, &context, &call) == WL_OK);
  run(&a, 0U);
  deliver(&b, &a, 1U); run(&b, 0U);
  run(&a, 1U);
  CHECK(a.callbacks == 2U && a.results[0].outcome == WL_KERNEL_TIMED_OUT);
  deliver(&a, &b, 1U); run(&a, 2U); CHECK(a.callbacks == 2U);
  control_request_value_t invalid = {.has_request_id = true};
  CHECK(usb_kernel_control_start(&a.endpoint, &invalid, 0U, 1U, control_done, &a, &call) == WL_ERR_CORRUPT_PAYLOAD);
  /* Decoder failure before handler, followed by a valid next packet. */
  a.frames[0][a.lengths[0] - 1U] = 0x80U;
  deliver(&b, &a, 0U);
  CHECK(usb_kernel_step(&b.endpoint, 0U) == WL_ERR_CORRUPT_PAYLOAD && b.effects == 2U);
  a.frames[0][a.lengths[0] - 1U] = 0x0bU;
  deliver(&b, &a, 0U); run(&b, 0U); CHECK(b.effects == 3U);
  value.channel = 1U;
  CHECK(usb_kernel_setpoint_start(&a.endpoint, &value, 2U, 10U, setpoint_done, &context, &call) == WL_OK);
  run(&a, 2U); deliver(&b, &a, 2U); run(&b, 2U);
  b.frames[3][b.lengths[3] - 1U] = 0x80U; /* Malformed typed response. */
  deliver(&a, &b, 3U); run(&a, 2U);
  CHECK(a.callbacks == 3U && a.results[0].outcome == WL_KERNEL_LOCAL_ERROR);
  control_request_value_t bad_reply = {.has_request_id = true, .request_id = 1U,
      .has_opcode = true, .opcode = 4U, .has_arg0 = true, .has_arg1 = true,
      .has_arg2 = true, .has_arg3 = true};
  CHECK(usb_kernel_control_start(&a.endpoint, &bad_reply, 2U, 5U, control_failed, &a, &call) == WL_OK);
  run(&a, 2U); deliver(&b, &a, 3U);
  CHECK(usb_kernel_step(&b.endpoint, 2U) == WL_ERR_CORRUPT_PAYLOAD);
  CHECK(b.effects == 5U && b.frames_count == 4U && b.errors == 2U);
  run(&a, 7U);
  CHECK(a.callbacks == 4U && a.results[7].outcome == WL_KERNEL_TIMED_OUT);
}

int main(void) {
  test_four_calls_both_directions();
  test_backpressure_and_lease();
  test_control_and_all_messages();
  test_generated_failures();
  printf("generated USB endpoint=%zu, port=%zu, RX=%zu bytes per peer\n",
      sizeof(usb_kernel_t), sizeof(wl_packet_port_t), (size_t)4096U);
  return 0;
}
