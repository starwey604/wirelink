/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_CPP_RESULT_HPP
#define WIRELINK_CPP_RESULT_HPP

#include <wirelink/rpc_result.h>
#include <wirelink/rpc.h>
#include <system_error>
#include <utility>
#include <variant>

namespace wirelink {

enum class ErrorKind {
  invalid_argument, closed, timed_out, cancelled, queue_full,
  transport, codec, runtime, rejected, out_of_memory,
};

struct Error {
  ErrorKind kind{ErrorKind::runtime};
  wl_rpc_completion_t completion{};
  std::error_code system_error{};

  static Error from_completion(wl_rpc_completion_t result) noexcept {
    Error error;
    error.completion = result;
    if (result.status == WL_RPC_REJECTED) error.kind = ErrorKind::rejected;
    else if (result.status == WL_RPC_TIMED_OUT) error.kind = ErrorKind::timed_out;
    else if (result.status == WL_RPC_CANCELLED) error.kind = ErrorKind::cancelled;
    else if (result.local_error == WL_ERR_NOT_INITIALIZED) error.kind = ErrorKind::closed;
    else if (result.local_error == WL_ERR_INVALID_ARG) error.kind = ErrorKind::invalid_argument;
    // Managed RPC admission reports a full runtime as BUSY; the executor's
    // own bounded queue reports QUEUE_FULL. Preserve the raw code in both cases.
    else if (result.local_error == WL_ERR_QUEUE_FULL || result.local_error == WL_ERR_BUSY ||
             result.runtime_error == WL_RPC_ERR_NO_SLOT)
      error.kind = ErrorKind::queue_full;
    else if (result.local_error == WL_ERR_NO_MEM) error.kind = ErrorKind::out_of_memory;
    // Request admission may expose only the core's payload error. Keep that
    // original local code; do not invent a more specific codec diagnostic.
    else if (result.codec_error != WL_CODEC_OK || result.local_error == WL_ERR_CORRUPT_PAYLOAD)
      error.kind = ErrorKind::codec;
    else if (result.transport_error != WL_OK || result.local_error == WL_ERR_IO)
      error.kind = ErrorKind::transport;
    return error;
  }

  static Error local(wl_err_t code) noexcept {
    wl_rpc_completion_t result{};
    result.status = WL_RPC_FAILED;
    result.local_error = code;
    return from_completion(result);
  }
};

inline const char* error_kind_name(ErrorKind kind) noexcept {
  switch (kind) {
    case ErrorKind::invalid_argument: return "invalid_argument";
    case ErrorKind::closed: return "closed";
    case ErrorKind::timed_out: return "timed_out";
    case ErrorKind::cancelled: return "cancelled";
    case ErrorKind::queue_full: return "queue_full";
    case ErrorKind::transport: return "transport";
    case ErrorKind::codec: return "codec";
    case ErrorKind::runtime: return "runtime";
    case ErrorKind::rejected: return "rejected";
    case ErrorKind::out_of_memory: return "out_of_memory";
  }
  return "runtime";
}

// Inspect the result before value()/error(); accessing the wrong alternative
// throws std::bad_variant_access. T may be move-only. No heap is used here.
template<class T> class [[nodiscard]] Result {
public:
  Result(T value) : value_(std::move(value)) {}
  Result(Error error) : value_(std::move(error)) {}
  explicit operator bool() const noexcept { return std::holds_alternative<T>(value_); }
  T& value() & { return std::get<T>(value_); }
  const T& value() const & { return std::get<T>(value_); }
  T&& value() && { return std::get<T>(std::move(value_)); }
  const Error& error() const { return std::get<Error>(value_); }

private:
  std::variant<T, Error> value_;
};

} // namespace wirelink
#endif
