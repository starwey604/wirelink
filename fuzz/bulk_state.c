/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wirelink/bulk.h"
#include "wirelink/crc.h"

#define FUZZ_OBJECT_CAPACITY 4096U
#define FUZZ_CHUNK_CAPACITY 1024U

struct input_cursor {
  const uint8_t *data;
  size_t length;
  size_t offset;
};

struct fuzz_sink {
  uint8_t object[FUZZ_OBJECT_CAPACITY];
  uint64_t resume_offset;
  uint64_t written_length;
  wl_bulk_sink_result_t next_result;
  uint32_t finishes;
  uint32_t aborts;
};

static uint8_t take_u8(struct input_cursor *input) {
  if (input->offset >= input->length) {
    return 0U;
  }
  return input->data[input->offset++];
}

static uint32_t take_u32(struct input_cursor *input) {
  uint32_t value = 0U;
  size_t index;

  for (index = 0U; index < sizeof(value); ++index) {
    value |= (uint32_t)take_u8(input) << (index * 8U);
  }
  return value;
}

static uint64_t take_u64(struct input_cursor *input) {
  return (uint64_t)take_u32(input) | ((uint64_t)take_u32(input) << 32U);
}

static wl_bulk_sink_result_t consume_result(struct fuzz_sink *sink) {
  const wl_bulk_sink_result_t result = sink->next_result;

  sink->next_result = WL_BULK_SINK_OK;
  return result;
}

static wl_bulk_sink_result_t sink_begin(void *user_data,
                                        const wl_bulk_descriptor_t *descriptor,
                                        uint64_t *out_resume_offset) {
  struct fuzz_sink *sink = user_data;
  const wl_bulk_sink_result_t result = consume_result(sink);

  if (result != WL_BULK_SINK_OK) {
    return result;
  }
  if (descriptor->total_length > FUZZ_OBJECT_CAPACITY ||
      sink->resume_offset > descriptor->total_length) {
    return WL_BULK_SINK_INVALID;
  }
  *out_resume_offset = sink->resume_offset;
  sink->written_length = sink->resume_offset;
  return WL_BULK_SINK_OK;
}

static wl_bulk_sink_result_t sink_write(void *user_data, uint32_t transfer_id,
                                        uint64_t offset, const uint8_t *data,
                                        size_t length) {
  struct fuzz_sink *sink = user_data;
  const wl_bulk_sink_result_t result = consume_result(sink);

  (void)transfer_id;
  if (result != WL_BULK_SINK_OK) {
    return result;
  }
  if (offset > FUZZ_OBJECT_CAPACITY ||
      length > FUZZ_OBJECT_CAPACITY - (size_t)offset ||
      offset != sink->written_length) {
    return WL_BULK_SINK_INVALID;
  }
  memcpy(sink->object + (size_t)offset, data, length);
  sink->written_length += length;
  return WL_BULK_SINK_OK;
}

static wl_bulk_sink_result_t
sink_finish(void *user_data, const wl_bulk_descriptor_t *descriptor) {
  struct fuzz_sink *sink = user_data;
  const wl_bulk_sink_result_t result = consume_result(sink);

  if (result != WL_BULK_SINK_OK) {
    return result;
  }
  if (sink->written_length != descriptor->total_length ||
      wl_crc32c(sink->object, (size_t)sink->written_length) !=
          descriptor->object_crc32c) {
    return WL_BULK_SINK_INTEGRITY_FAILED;
  }
  ++sink->finishes;
  return WL_BULK_SINK_OK;
}

static void sink_abort(void *user_data, uint32_t transfer_id, int32_t reason) {
  struct fuzz_sink *sink = user_data;

  (void)transfer_id;
  (void)reason;
  ++sink->aborts;
}

static wl_bulk_sink_result_t arbitrary_sink_result(uint8_t value) {
  return (wl_bulk_sink_result_t)(value % (WL_BULK_SINK_INVALID + 1));
}

static wl_bulk_descriptor_t descriptor_from_input(struct input_cursor *input) {
  wl_bulk_descriptor_t descriptor;

  descriptor.transfer_id = take_u32(input);
  descriptor.total_length =
      take_u64(input) % (uint64_t)(FUZZ_OBJECT_CAPACITY + 1U);
  descriptor.requested_chunk_size = 1U + take_u32(input) % FUZZ_CHUNK_CAPACITY;
  descriptor.object_crc32c = take_u32(input);
  return descriptor;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  struct input_cursor input = {data, size, 0U};
  struct fuzz_sink sink = {0};
  wl_bulk_receiver_t receiver;
  wl_bulk_sender_t sender;
  wl_bulk_receiver_status_view_t status_view = {0};
  wl_bulk_sender_action_t action = {0};
  wl_bulk_descriptor_t descriptor = {1U, 0U, 1U, 0U};
  const wl_bulk_receiver_config_t receiver_config = {
      .max_object_length = FUZZ_OBJECT_CAPACITY,
      .max_chunk_size = FUZZ_CHUNK_CAPACITY,
      .write_alignment = 1U,
      .idle_timeout_ms = 1000U,
      .sink = {&sink, sink_begin, sink_write, sink_finish, sink_abort},
  };
  const wl_bulk_sender_config_t sender_config = {
      .status_timeout_ms = 100U,
      .busy_retry_ms = 5U,
      .max_retries = 3U,
  };
  wl_time_ms_t now_ms = 0U;
  uint8_t chunk_data[FUZZ_CHUNK_CAPACITY];
  size_t step = 0U;

  if (wl_bulk_receiver_init(&receiver, &receiver_config) != WL_BULK_OK ||
      wl_bulk_sender_init(&sender, &sender_config) != WL_BULK_OK) {
    abort();
  }

  while (input.offset < input.length && step++ < 256U) {
    const uint8_t operation = take_u8(&input) % 14U;

    now_ms += take_u32(&input);
    switch (operation) {
    case 0:
      descriptor = descriptor_from_input(&input);
      sink.resume_offset =
          descriptor.total_length == 0U
              ? 0U
              : take_u64(&input) % (descriptor.total_length + 1U);
      sink.next_result = arbitrary_sink_result(take_u8(&input));
      (void)wl_bulk_receiver_on_begin(&receiver, &descriptor, now_ms);
      break;
    case 1: {
      wl_bulk_chunk_t chunk;
      size_t index;

      chunk.transfer_id = take_u32(&input);
      chunk.offset = take_u64(&input) %
                     (uint64_t)(FUZZ_OBJECT_CAPACITY + FUZZ_CHUNK_CAPACITY);
      chunk.length = take_u32(&input) % (FUZZ_CHUNK_CAPACITY + 1U);
      for (index = 0U; index < chunk.length; ++index) {
        chunk_data[index] = take_u8(&input);
      }
      chunk.data = (take_u8(&input) & 1U) != 0U ? chunk_data : NULL;
      sink.next_result = arbitrary_sink_result(take_u8(&input));
      (void)wl_bulk_receiver_on_chunk(&receiver, &chunk, now_ms);
      break;
    }
    case 2:
      sink.next_result = arbitrary_sink_result(take_u8(&input));
      (void)wl_bulk_receiver_on_end(&receiver, take_u32(&input),
                                    take_u64(&input), take_u32(&input), now_ms);
      break;
    case 3:
      (void)wl_bulk_receiver_on_abort(&receiver, take_u32(&input),
                                      (int32_t)take_u32(&input), now_ms);
      break;
    case 4:
      if (wl_bulk_receiver_status_acquire(&receiver, &status_view) ==
          WL_BULK_OK) {
        if ((take_u8(&input) & 1U) != 0U) {
          (void)wl_bulk_receiver_status_release(&receiver, &status_view);
        } else {
          (void)wl_bulk_receiver_status_defer(&receiver, &status_view);
        }
      }
      break;
    case 5:
      status_view.token ^= take_u32(&input);
      (void)wl_bulk_receiver_status_release(&receiver, &status_view);
      break;
    case 6:
      (void)wl_bulk_receiver_poll(&receiver, now_ms);
      break;
    case 7:
      (void)wl_bulk_receiver_reset(&receiver);
      break;
    case 8:
      descriptor = descriptor_from_input(&input);
      (void)wl_bulk_sender_start(&sender, &descriptor);
      break;
    case 9:
      if (wl_bulk_sender_action_acquire(&sender, &action) == WL_BULK_OK) {
        if ((take_u8(&input) & 1U) != 0U) {
          (void)wl_bulk_sender_action_submitted(&sender, &action, now_ms);
        } else {
          (void)wl_bulk_sender_action_defer(&sender, &action);
        }
      }
      break;
    case 10: {
      wl_bulk_status_t status;

      status.transfer_id = take_u32(&input);
      status.phase = (wl_bulk_phase_t)(take_u8(&input) % 7U);
      status.code = (wl_bulk_status_code_t)(take_u8(&input) % 12U);
      status.next_offset = take_u64(&input);
      status.accepted_chunk_size = take_u32(&input);
      (void)wl_bulk_sender_on_status(&sender, &status, now_ms);
      break;
    }
    case 11:
      (void)wl_bulk_sender_poll(&sender, now_ms);
      break;
    case 12:
      (void)wl_bulk_sender_request_abort(&sender, (int32_t)take_u32(&input));
      break;
    default:
      (void)wl_bulk_sender_reset(&sender);
      break;
    }

    {
      wl_bulk_receiver_state_t receiver_state;
      wl_bulk_sender_result_t sender_result;
      uint64_t receiver_offset;

      if (wl_bulk_receiver_get_state(&receiver, &receiver_state,
                                     &receiver_offset) == WL_BULK_OK &&
          receiver_offset > FUZZ_OBJECT_CAPACITY) {
        abort();
      }
      if (wl_bulk_sender_get_result(&sender, &sender_result) == WL_BULK_OK &&
          sender_result.next_offset > FUZZ_OBJECT_CAPACITY) {
        abort();
      }
      if (sink.written_length > FUZZ_OBJECT_CAPACITY) {
        abort();
      }
    }
  }
  return 0;
}
