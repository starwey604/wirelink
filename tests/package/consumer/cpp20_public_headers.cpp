/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/allocator.h>
#include <wirelink/alignment.h>
#include <wirelink/byte_fifo.h>
#include <wirelink/clock.h>
#include <wirelink/cobs.h>
#include <wirelink/codec.h>
#include <wirelink/crc.h>
#include <wirelink/diagnostics.h>
#include <wirelink/environment.h>
#include <wirelink/fifo.h>
#include <wirelink/frame.h>
#include <wirelink/io_readiness.h>
#include <wirelink/kernel.h>
#include <wirelink/kernel_wire.h>
#include <wirelink/latest.h>
#include <wirelink/outbox.h>
#include <wirelink/packet_loopback.h>
#include <wirelink/packet_port.h>
#include <wirelink/packet_stream.h>
#include <wirelink/platform.h>
#include <wirelink/profile.h>
#include <wirelink/span.h>
#include <wirelink/types.h>
#include <wirelink/version.h>
#include <wirelink/wait.h>
#include <wirelink/wirelink.h>
#include <type_traits>
template<class T> concept Complete = requires { sizeof(T); };
static_assert(!Complete<wl_packet_port_t> && !Complete<wl_byte_fifo_t>);
static_assert(std::is_standard_layout_v<wl_kernel_result_t>);
static_assert(std::is_standard_layout_v<wl_kernel_call_handle_t>);
static_assert(std::is_standard_layout_v<wl_packet_token_t>);
static_assert(std::is_trivially_copyable_v<wl_environment_t>);
static_assert(sizeof(wl_fifo_t) == WL_FIFO_CONTEXT_STORAGE_SIZE);
static_assert(sizeof(wl_latest_t) == WL_LATEST_CONTEXT_STORAGE_SIZE);
static_assert(sizeof(wl_outbox_t) == WL_OUTBOX_STORAGE_SIZE);
static_assert(WL_KERNEL_CALLS == 4 && WL_KERNEL_REPLIES == 4);
int main() {
  const auto hint = wl_io_readiness_at(10);
  return hint.runnable || hint.next_deadline_ms != 10 ||
    wl_wait_remaining(10, UINT32_MAX - 4, 0) != 5;
}
