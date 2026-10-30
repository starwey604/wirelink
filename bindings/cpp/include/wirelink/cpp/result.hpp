/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_CPP_RESULT_HPP
#define WIRELINK_CPP_RESULT_HPP

#include <wirelink/kernel.h>
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
  wl_kernel_result_t completion{WL_KERNEL_LOCAL_ERROR, WL_ERR_INVALID_STATE};
  std::error_code system_error{};
  static Error from_completion(wl_kernel_result_t result) noexcept {
    Error error; error.completion = result;
    if (result.outcome == WL_KERNEL_REJECTED) error.kind = ErrorKind::rejected;
    else if (result.outcome == WL_KERNEL_TIMED_OUT || result.status == WL_ERR_TIMEOUT)
      error.kind = ErrorKind::timed_out;
    else if (result.outcome == WL_KERNEL_CANCELLED || result.status == WL_ERR_CANCELLED)
      error.kind = ErrorKind::cancelled;
    else if (result.outcome == WL_KERNEL_CLOSED || result.status == WL_ERR_NOT_INITIALIZED)
      error.kind = ErrorKind::closed;
    else if (result.outcome == WL_KERNEL_LOCAL_ERROR) {
      if (result.status == WL_ERR_INVALID_ARG) error.kind = ErrorKind::invalid_argument;
      else if (result.status == WL_ERR_QUEUE_FULL || result.status == WL_ERR_BUSY ||
               result.status == WL_ERR_WOULD_BLOCK)
        error.kind = ErrorKind::queue_full;
      else if (result.status == WL_ERR_NO_MEM) error.kind = ErrorKind::out_of_memory;
      else if (result.status == WL_ERR_CORRUPT_PAYLOAD || result.status == WL_ERR_BAD_FRAME ||
               result.status == WL_ERR_BUF_TOO_SMALL || result.status == WL_ERR_PAYLOAD_TOO_LONG)
        error.kind = ErrorKind::codec;
      else if (result.status == WL_ERR_IO || result.status == WL_ERR_TX_FAILED)
        error.kind = ErrorKind::transport;
    }
    return error;
  }
  static Error local(wl_err_t code) noexcept {
    return from_completion({WL_KERNEL_LOCAL_ERROR, code});
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
