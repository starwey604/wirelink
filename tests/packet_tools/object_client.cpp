/* SPDX-License-Identifier: Apache-2.0 */
// Exercise the actual tool command/retry code against the real sample application.
// Neither USB nor serial is opened. Controlled driver completes only real RAM copies.
#define main packet_transport_cli_main
#include "../../benchmarks/host/transport_benchmark.cpp"
#undef main
#include <cstdlib>
extern "C" {
wl_packet_submit_t object_peer_exchange(void *, const uint8_t *, size_t, wl_packet_token_t);
void object_peer_prepare(void);
void object_peer_faults(int, int);
unsigned object_peer_commits(void);
unsigned object_peer_writes(void);
void object_peer_close(void);
}
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
int main() {
  object_peer_prepare();
  CHECK(example_packet_tool_prepare(8080, 0, object_peer_exchange, nullptr, nullptr) == WL_OK);
  ObjectSender sender;
  auto bytes = make_object(2016);
  const auto crc = wl_crc32c(bytes.data(), bytes.size());
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  bulk_begin_t begin{};
  begin.has_transfer_id = begin.has_total_length = begin.has_requested_chunk_size = begin.has_object_crc32c = true;
  begin.transfer_id = 42; begin.total_length = bytes.size(); begin.requested_chunk_size = 2016; begin.object_crc32c = crc;
  object_peer_faults(1, 0);
  auto status = sender.transact(BULK_BEGIN_MESSAGE_ID, begin, bulk_begin_encode, 42, CONTROL_BULK_PHASE_BEGIN, 0, deadline);
  CHECK(status.accepted_chunk_size == 2016 && sender.retries == 1);
  bulk_chunk_t chunk{}; chunk.has_transfer_id = chunk.has_offset = chunk.has_data = true;
  chunk.transfer_id = 42; chunk.data = {bytes.data(), bytes.size()};
  object_peer_faults(1, 1); // Lost Status repeats the command; stale BEGIN cannot match CHUNK.
  (void)sender.transact(BULK_CHUNK_MESSAGE_ID, chunk, bulk_chunk_encode, 42, CONTROL_BULK_PHASE_CHUNK, bytes.size(), deadline);
  CHECK(object_peer_writes() == 1 && sender.retries == 2);
  bulk_end_t end{}; end.has_transfer_id = end.has_total_length = end.has_object_crc32c = true;
  end.transfer_id = 42; end.total_length = bytes.size(); end.object_crc32c = crc;
  object_peer_faults(1, 1);
  (void)sender.transact(BULK_END_MESSAGE_ID, end, bulk_end_encode, 42, CONTROL_BULK_PHASE_END, bytes.size(), deadline);
  CHECK(object_peer_commits() == 1 && sender.retries == 3);
  object_peer_faults(3, 0);
  bool failed = false;
  try { (void)sender.transact(BULK_END_MESSAGE_ID, end, bulk_end_encode, 42, CONTROL_BULK_PHASE_END, bytes.size(), Clock::now() + std::chrono::seconds(2)); }
  catch (const std::runtime_error &) { failed = true; }
  CHECK(failed && sender.retries == 5 && object_peer_commits() == 1);
  CHECK(example_packet_tool_close_begin() == WL_OK);
  CHECK(example_packet_tool_close_finish(0) == WL_OK);
  object_peer_close();
}
