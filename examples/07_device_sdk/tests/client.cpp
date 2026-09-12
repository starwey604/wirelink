/* SPDX-License-Identifier: Apache-2.0 */
#include <device/client.hpp>
#include "server.hpp"
#include <array>
#include <string>

int main() {
  Server server;
  auto connection = device::Client::connect({"127.0.0.1", server.port()});
  CHECK(connection);
  auto client = std::move(connection).value();
  auto info = client.get_info({});
  if (!info) {
    const auto& error = info.error();
    std::fprintf(stderr, "get_info: %s status=%d local=%d transport=%d runtime=%d codec=%d\n",
        wirelink::error_kind_name(error.kind), error.completion.status,
        error.completion.local_error, error.completion.transport_error,
        error.completion.runtime_error, error.completion.codec_error);
  }
  CHECK(info);
  CHECK(info.value().name == std::string("device\0x", 8));
  CHECK(info.value().settings.mode == static_cast<device::Mode>(12345));
  CHECK(!info.value().settings.label);
  CHECK(info.value().settings.label_or_default() == "设备");

  device::ConfigureRequest request{};
  request.settings = info.value().settings;
  request.opaque = std::vector<std::uint8_t>{};
  request.backup.emplace(request.settings);
  request.backup->label = std::string{};
  request.backup->counters = std::array<std::uint64_t, 2>{0, UINT64_MAX};
  request.settings.class_ = std::string("x\0y", 3);
  request.settings.timeout_ = 99;
  device::ConfigureResponse saved{};
  for (std::uint32_t i = 1; i <= 100; ++i) {
    auto response = client.configure(request);
    CHECK(response && response.value().count == i);
    saved = std::move(response).value();
  }
  request.settings.token = {255};
  auto rejected = client.configure(request);
  CHECK(!rejected && rejected.error().kind == wirelink::ErrorKind::rejected);
  CHECK(rejected.error().completion.rejection == 7);
  request.settings.label = std::string(13, 'a');
  auto invalid = client.configure(request);
  CHECK(!invalid && invalid.error().kind == wirelink::ErrorKind::invalid_argument);
  request.settings.label = std::string("\xc0\xaf", 2);
  auto utf8 = client.configure(request);
  CHECK(!utf8 && utf8.error().kind == wirelink::ErrorKind::codec);
  CHECK(utf8.error().completion.local_error == WL_ERR_CORRUPT_PAYLOAD);
  CHECK(utf8.error().completion.codec_error == WL_CODEC_OK);

  client.close();
  CHECK(!saved.settings.label && saved.backup->label->empty());
  CHECK(saved.opaque && saved.opaque->empty());
  CHECK(saved.settings.class_ == std::string("x\0y", 3));
  CHECK(saved.settings.timeout_ == 99);
  CHECK(saved.settings.token == std::vector<std::uint8_t>({0, 128, 255}));
  CHECK((*saved.backup->counters)[1] == UINT64_MAX);
  CHECK(saved.settings.floor_or_default() == INT64_MIN);
  CHECK(saved.settings.ceiling_or_default() == UINT64_MAX);
}
