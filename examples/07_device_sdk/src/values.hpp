/* SPDX-License-Identifier: Apache-2.0 */
// Generator-private owned conversions; never installed as a public header.
#pragma once
#include <device/types.hpp>
#include "native.hpp"
#include <algorithm>

namespace device::wlc_detail {
inline bool to_c(const InfoRequest& input, wlc_native::info_request_value_t& output) {
  (void)input;
  wlc_native::info_request_value_clear(&output);
  return true;
}

inline InfoRequest from_c(const wlc_native::info_request_value_t& input) {
  (void)input;
  InfoRequest output{};
  return output;
}

inline bool to_c(const Settings& input, wlc_native::settings_value_t& output) {
  (void)input;
  wlc_native::settings_value_clear(&output);
  output.has_label = input.label.has_value();
  if (output.has_label) {
    if ((*input.label).size() > 12U) return false;
    output.label.length = (*input.label).size();
    std::copy((*input.label).begin(), (*input.label).end(), output.label.data);
  }
  output.has_enabled = true;
  if (output.has_enabled) {
    output.enabled = input.enabled;
  }
  output.has_mode = input.mode.has_value();
  if (output.has_mode) {
    output.mode = static_cast<std::int32_t>((*input.mode));
  }
  output.has_token = true;
  if (output.has_token) {
    if (input.token.size() > 8U) return false;
    output.token.length = input.token.size();
    std::copy(input.token.begin(), input.token.end(), output.token.data);
  }
  output.has_gains = true;
  if (output.has_gains) {
    std::copy(input.gains.begin(), input.gains.end(), output.gains);
  }
  output.has_counters = input.counters.has_value();
  if (output.has_counters) {
    std::copy((*input.counters).begin(), (*input.counters).end(), output.counters);
  }
  output.has_floor = input.floor.has_value();
  if (output.has_floor) {
    output.floor = (*input.floor);
  }
  output.has_ceiling = input.ceiling.has_value();
  if (output.has_ceiling) {
    output.ceiling = (*input.ceiling);
  }
  output.has_class_ = input.class_.has_value();
  if (output.has_class_) {
    if ((*input.class_).size() > 8U) return false;
    output.class_.length = (*input.class_).size();
    std::copy((*input.class_).begin(), (*input.class_).end(), output.class_.data);
  }
  output.has_timeout = input.timeout_.has_value();
  if (output.has_timeout) {
    output.timeout = (*input.timeout_);
  }
  return true;
}

inline Settings from_c(const wlc_native::settings_value_t& input) {
  (void)input;
  Settings output{};
  if (input.has_label) {
    output.label.emplace();
    (*output.label).assign(input.label.data, input.label.data + input.label.length);
  }
  if (input.has_enabled) {
    output.enabled = input.enabled;
  }
  if (input.has_mode) {
    output.mode.emplace();
    (*output.mode) = static_cast<Mode>(input.mode);
  }
  if (input.has_token) {
    output.token.assign(input.token.data, input.token.data + input.token.length);
  }
  if (input.has_gains) {
    std::copy_n(input.gains, 3, output.gains.begin());
  }
  if (input.has_counters) {
    output.counters.emplace();
    std::copy_n(input.counters, 2, (*output.counters).begin());
  }
  if (input.has_floor) {
    output.floor.emplace();
    (*output.floor) = input.floor;
  }
  if (input.has_ceiling) {
    output.ceiling.emplace();
    (*output.ceiling) = input.ceiling;
  }
  if (input.has_class_) {
    output.class_.emplace();
    (*output.class_).assign(input.class_.data, input.class_.data + input.class_.length);
  }
  if (input.has_timeout) {
    output.timeout_.emplace();
    (*output.timeout_) = input.timeout;
  }
  return output;
}

inline bool to_c(const InfoResponse& input, wlc_native::info_response_value_t& output) {
  (void)input;
  wlc_native::info_response_value_clear(&output);
  output.has_name = true;
  if (output.has_name) {
    if (input.name.size() > 32U) return false;
    output.name.length = input.name.size();
    std::copy(input.name.begin(), input.name.end(), output.name.data);
  }
  output.has_settings = true;
  if (output.has_settings) {
    if (!to_c(input.settings, output.settings)) return false;
  }
  return true;
}

inline InfoResponse from_c(const wlc_native::info_response_value_t& input) {
  (void)input;
  InfoResponse output{};
  if (input.has_name) {
    output.name.assign(input.name.data, input.name.data + input.name.length);
  }
  if (input.has_settings) {
    output.settings = from_c(input.settings);
  }
  return output;
}

inline bool to_c(const ConfigureRequest& input, wlc_native::configure_request_value_t& output) {
  (void)input;
  wlc_native::configure_request_value_clear(&output);
  output.has_settings = true;
  if (output.has_settings) {
    if (!to_c(input.settings, output.settings)) return false;
  }
  output.has_opaque = input.opaque.has_value();
  if (output.has_opaque) {
    if ((*input.opaque).size() > 4U) return false;
    output.opaque.length = (*input.opaque).size();
    std::copy((*input.opaque).begin(), (*input.opaque).end(), output.opaque.data);
  }
  output.has_backup = input.backup.has_value();
  if (output.has_backup) {
    if (!to_c((*input.backup), output.backup)) return false;
  }
  return true;
}

inline ConfigureRequest from_c(const wlc_native::configure_request_value_t& input) {
  (void)input;
  ConfigureRequest output{};
  if (input.has_settings) {
    output.settings = from_c(input.settings);
  }
  if (input.has_opaque) {
    output.opaque.emplace();
    (*output.opaque).assign(input.opaque.data, input.opaque.data + input.opaque.length);
  }
  if (input.has_backup) {
    output.backup.emplace();
    (*output.backup) = from_c(input.backup);
  }
  return output;
}

inline bool to_c(const ConfigureResponse& input, wlc_native::configure_response_value_t& output) {
  (void)input;
  wlc_native::configure_response_value_clear(&output);
  output.has_settings = true;
  if (output.has_settings) {
    if (!to_c(input.settings, output.settings)) return false;
  }
  output.has_opaque = input.opaque.has_value();
  if (output.has_opaque) {
    if ((*input.opaque).size() > 4U) return false;
    output.opaque.length = (*input.opaque).size();
    std::copy((*input.opaque).begin(), (*input.opaque).end(), output.opaque.data);
  }
  output.has_backup = input.backup.has_value();
  if (output.has_backup) {
    if (!to_c((*input.backup), output.backup)) return false;
  }
  output.has_count = true;
  if (output.has_count) {
    output.count = input.count;
  }
  return true;
}

inline ConfigureResponse from_c(const wlc_native::configure_response_value_t& input) {
  (void)input;
  ConfigureResponse output{};
  if (input.has_settings) {
    output.settings = from_c(input.settings);
  }
  if (input.has_opaque) {
    output.opaque.emplace();
    (*output.opaque).assign(input.opaque.data, input.opaque.data + input.opaque.length);
  }
  if (input.has_backup) {
    output.backup.emplace();
    (*output.backup) = from_c(input.backup);
  }
  if (input.has_count) {
    output.count = input.count;
  }
  return output;
}

inline bool to_c(const Numbers& input, wlc_native::numbers_value_t& output) {
  (void)input;
  wlc_native::numbers_value_clear(&output);
  output.has_i8 = true;
  if (output.has_i8) {
    output.i8 = input.i8;
  }
  output.has_u8 = true;
  if (output.has_u8) {
    output.u8 = input.u8;
  }
  output.has_i16 = true;
  if (output.has_i16) {
    output.i16 = input.i16;
  }
  output.has_u16 = true;
  if (output.has_u16) {
    output.u16 = input.u16;
  }
  output.has_i32 = true;
  if (output.has_i32) {
    output.i32 = input.i32;
  }
  output.has_u32 = true;
  if (output.has_u32) {
    output.u32 = input.u32;
  }
  output.has_i64 = true;
  if (output.has_i64) {
    output.i64 = input.i64;
  }
  output.has_u64 = true;
  if (output.has_u64) {
    output.u64 = input.u64;
  }
  output.has_f32 = true;
  if (output.has_f32) {
    output.f32 = input.f32;
  }
  output.has_f64 = true;
  if (output.has_f64) {
    output.f64 = input.f64;
  }
  output.has_single = true;
  if (output.has_single) {
    output.single = input.single;
  }
  output.has_double_ = true;
  if (output.has_double_) {
    output.double_ = input.double_;
  }
  output.has_doubles = input.doubles.has_value();
  if (output.has_doubles) {
    std::copy((*input.doubles).begin(), (*input.doubles).end(), output.doubles);
  }
  output.has_words = true;
  if (output.has_words) {
    std::copy(input.words.begin(), input.words.end(), output.words);
  }
  output.has_offset = input.offset.has_value();
  if (output.has_offset) {
    output.offset = (*input.offset);
  }
  output.has_minimum = input.minimum.has_value();
  if (output.has_minimum) {
    output.minimum = (*input.minimum);
  }
  output.has_flag = input.flag.has_value();
  if (output.has_flag) {
    output.flag = (*input.flag);
  }
  output.has_text = input.text.has_value();
  if (output.has_text) {
    if ((*input.text).size() > 16U) return false;
    output.text.length = (*input.text).size();
    std::copy((*input.text).begin(), (*input.text).end(), output.text.data);
  }
  return true;
}

inline Numbers from_c(const wlc_native::numbers_value_t& input) {
  (void)input;
  Numbers output{};
  if (input.has_i8) {
    output.i8 = input.i8;
  }
  if (input.has_u8) {
    output.u8 = input.u8;
  }
  if (input.has_i16) {
    output.i16 = input.i16;
  }
  if (input.has_u16) {
    output.u16 = input.u16;
  }
  if (input.has_i32) {
    output.i32 = input.i32;
  }
  if (input.has_u32) {
    output.u32 = input.u32;
  }
  if (input.has_i64) {
    output.i64 = input.i64;
  }
  if (input.has_u64) {
    output.u64 = input.u64;
  }
  if (input.has_f32) {
    output.f32 = input.f32;
  }
  if (input.has_f64) {
    output.f64 = input.f64;
  }
  if (input.has_single) {
    output.single = input.single;
  }
  if (input.has_double_) {
    output.double_ = input.double_;
  }
  if (input.has_doubles) {
    output.doubles.emplace();
    std::copy_n(input.doubles, 2, (*output.doubles).begin());
  }
  if (input.has_words) {
    std::copy_n(input.words, 2, output.words.begin());
  }
  if (input.has_offset) {
    output.offset.emplace();
    (*output.offset) = input.offset;
  }
  if (input.has_minimum) {
    output.minimum.emplace();
    (*output.minimum) = input.minimum;
  }
  if (input.has_flag) {
    output.flag.emplace();
    (*output.flag) = input.flag;
  }
  if (input.has_text) {
    output.text.emplace();
    (*output.text).assign(input.text.data, input.text.data + input.text.length);
  }
  return output;
}

} // namespace device::wlc_detail
