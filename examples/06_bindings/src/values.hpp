/* SPDX-License-Identifier: Apache-2.0 */
// Generator-private owned conversions; never installed as a public header.
#pragma once
#include <calculator/types.hpp>
#include "native.hpp"
#include <algorithm>

namespace calculator::wlc_detail {
inline bool to_c(const AddRequest& input, wlc_native::add_request_value_t& output) {
  (void)input;
  wlc_native::add_request_value_clear(&output);
  output.has_left = true;
  if (output.has_left) {
    output.left = input.left;
  }
  output.has_right = true;
  if (output.has_right) {
    output.right = input.right;
  }
  return true;
}

inline AddRequest from_c(const wlc_native::add_request_value_t& input) {
  (void)input;
  AddRequest output{};
  if (input.has_left) {
    output.left = input.left;
  }
  if (input.has_right) {
    output.right = input.right;
  }
  return output;
}

inline bool to_c(const AddResponse& input, wlc_native::add_response_value_t& output) {
  (void)input;
  wlc_native::add_response_value_clear(&output);
  output.has_sum = true;
  if (output.has_sum) {
    output.sum = input.sum;
  }
  return true;
}

inline AddResponse from_c(const wlc_native::add_response_value_t& input) {
  (void)input;
  AddResponse output{};
  if (input.has_sum) {
    output.sum = input.sum;
  }
  return output;
}

} // namespace calculator::wlc_detail
