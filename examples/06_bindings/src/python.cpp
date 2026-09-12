/* SPDX-License-Identifier: Apache-2.0 */
// Python feature-test macros must be defined before any system headers.
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <calculator/client.hpp>
#include <wirelink/version.h>

namespace nb = nanobind;

NB_MODULE(_native, module) {
  module.attr("core_version") = WIRELINK_VERSION_STRING;
  module.attr("codegen_abi") = 32;
  module.attr("binding_api") = 1;
  nb::class_<wirelink::Error>(module, "Error")
      .def_prop_ro("kind", [](const wirelink::Error& e) { return wirelink::error_kind_name(e.kind); })
      .def_prop_ro("status", [](const wirelink::Error& e) { return e.completion.status; })
      .def_prop_ro("rejection", [](const wirelink::Error& e) { return e.completion.rejection; })
      .def_prop_ro("local_error", [](const wirelink::Error& e) { return e.completion.local_error; })
      .def_prop_ro("transport_error", [](const wirelink::Error& e) { return e.completion.transport_error; })
      .def_prop_ro("runtime_error", [](const wirelink::Error& e) { return e.completion.runtime_error; })
      .def_prop_ro("codec_error", [](const wirelink::Error& e) { return e.completion.codec_error; })
      .def_prop_ro("os_error", [](const wirelink::Error& e) { return e.system_error.value(); })
      .def_prop_ro("os_message", [](const wirelink::Error& e) { return e.system_error.message(); });

  nb::class_<calculator::Client>(module, "Client")
      .def("close", &calculator::Client::close, nb::call_guard<nb::gil_scoped_release>())
      .def_prop_ro("is_open", &calculator::Client::is_open)
      .def_prop_ro("local_port", &calculator::Client::local_port)
      .def("add", [](calculator::Client& client, std::int32_t left, std::int32_t right,
                     std::int64_t timeout_ms) {
        auto result = [&] {
          nb::gil_scoped_release release;
          return client.add({left, right}, std::chrono::milliseconds(timeout_ms));
        }();
        if (!result) return nb::make_tuple(nb::none(), wirelink::Error(result.error()));
        return nb::make_tuple(result.value().sum, nb::none());
      });

  module.def("connect", [](const std::string& peer_address, std::uint16_t peer_port,
                           const std::string& bind_address, std::uint16_t bind_port) {
    auto result = [&] {
      nb::gil_scoped_release release;
      return calculator::Client::connect({peer_address, peer_port, bind_address, bind_port});
    }();
    if (!result) return nb::make_tuple(nb::none(), wirelink::Error(result.error()));
    return nb::make_tuple(std::move(result).value(), nb::none());
  });
}
