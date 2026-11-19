/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/byte_fifo.h>
#include <wirelink/clock.h>
#include <wirelink/cobs.h>
#include <wirelink/codec.h>
#include <wirelink/crc.h>
#include <wirelink/environment.h>
#include <wirelink/io_readiness.h>
#include <wirelink/endpoint.h>
#include <wirelink/wire.h>
#include <wirelink/packet_loopback.h>
#include <wirelink/packet_port.h>
#include <wirelink/packet_stream.h>
#include <wirelink/platform.h>
#include <wirelink/span.h>
#include <wirelink/types.h>
#include <wirelink/version.h>
#include <wirelink/wait.h>
#include <wirelink/wirelink.h>
#include <type_traits>
template<class T> concept Complete = requires { sizeof(T); };
static_assert(Complete<wl_packet_port_t> && std::is_standard_layout_v<wl_packet_port_t>);
static_assert(!Complete<wl_byte_fifo_t>);
static_assert(std::is_same_v<wl_rpc_outcome_t, int32_t>);
static_assert(std::is_same_v<wl_packet_submit_t, int32_t>);
static_assert(std::is_standard_layout_v<wl_rpc_result_t>);
static_assert(std::is_standard_layout_v<wl_call_handle_t>);
static_assert(std::is_standard_layout_v<wl_packet_token_t>);
static_assert(std::is_trivially_copyable_v<wl_environment_t>);
static_assert(WL_ENDPOINT_CALLS == 4 && WL_ENDPOINT_REPLIES == 3);
int main() {
  const auto hint = wl_io_readiness_at(10);
  return hint.runnable || hint.next_deadline_ms != 10 ||
    wl_wait_remaining(10, UINT32_MAX - 4, 0) != 5;
}
