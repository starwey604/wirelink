/* SPDX-License-Identifier: Apache-2.0 */
#include <astrial/Usb.hpp>
#include "packet_tool_io.hpp"
#include "control.h"
#include "wirelink/crc.h"
#include <array>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <span>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::duration<double, std::micro>;
constexpr std::size_t EchoMaxPayload = 2048;
constexpr std::size_t DefaultObjectBytes = 1024 * 1024;
constexpr std::size_t MaxObjectBytes = 64 * 1024 * 1024;
constexpr std::size_t DefaultObjectChunk = 2016;
constexpr uint16_t MessageId = 0x424d;
struct Options
{
    std::string mode;
    std::string idle_mode{"poll"};
    std::string wake_policy{"all"};
    uint16_t vendor_id{0x2fe3};
    uint16_t product_id{0x574c};
    std::string port;
    std::size_t payload_size{128};
    std::size_t warmup{200};
    std::size_t iterations{2000};
    std::size_t object_bytes{DefaultObjectBytes};
    std::size_t object_chunk{DefaultObjectChunk};
    uint32_t transfer_id_base{};
    std::chrono::milliseconds timeout{1000};
};

uint64_t parse_number(std::string_view value)
{
    if (value.empty() || value.front() == '-') throw std::runtime_error("invalid unsigned argument");
    std::size_t used{};
    const auto parsed = std::stoull(std::string(value), &used, 0);
    if (used != value.size()) throw std::runtime_error("invalid numeric argument");
    return parsed;
}

std::size_t parse_size(std::string_view value)
{
    const auto parsed = parse_number(value);
    if (parsed > SIZE_MAX) throw std::runtime_error("size value is out of range");
    return static_cast<std::size_t>(parsed);
}

Options parse_options(int argc, char** argv)
{
    if (argc < 2)
        throw std::runtime_error(
            "missing mode: raw-bulk, wirelink-bulk, wirelink-object, or cdc");
    Options options;
    options.mode = argv[1];
    bool iterations_set{};
    bool warmup_set{};
    bool timeout_set{};
    bool idle_set{};
    bool transfer_id_base_set{};
    for (int index = 2; index < argc; index += 2)
    {
        if (index + 1 >= argc) throw std::runtime_error("option has no value");
        const std::string_view key = argv[index];
        const std::string_view value = argv[index + 1];
        if (key == "--vid" || key == "--pid") {
            const auto number = parse_number(value);
            if (number > UINT16_MAX) throw std::runtime_error("USB VID/PID out of range");
            if (key == "--vid") options.vendor_id = static_cast<uint16_t>(number);
            else options.product_id = static_cast<uint16_t>(number);
        }
        else if (key == "--port") options.port = value;
        else if (key == "--payload") options.payload_size = parse_size(value);
        else if (key == "--warmup")
        {
            options.warmup = parse_size(value);
            warmup_set = true;
        }
        else if (key == "--iterations")
        {
            options.iterations = parse_size(value);
            iterations_set = true;
        }
        else if (key == "--bytes") options.object_bytes = parse_size(value);
        else if (key == "--chunk") options.object_chunk = parse_size(value);
        else if (key == "--transfer-id-base")
        {
            const auto parsed = parse_number(value);
            if (parsed == 0 || parsed > UINT32_MAX)
                throw std::runtime_error("transfer ID base must be 1..UINT32_MAX");
            options.transfer_id_base = static_cast<uint32_t>(parsed);
            transfer_id_base_set = true;
        }
        else if (key == "--timeout-ms")
        {
            const auto milliseconds = parse_number(value);
            if (!milliseconds || milliseconds > INT32_MAX) throw std::runtime_error("timeout must be 1..INT32_MAX ms");
            options.timeout = std::chrono::milliseconds(milliseconds);
            timeout_set = true;
        }
        else if (key == "--idle")
        {
            options.idle_mode = value;
            idle_set = true;
        }
        else if (key == "--wake") options.wake_policy = value;
        else if (key == "--spin-us") throw std::runtime_error("spin phase removed; select --idle poll or wait");
        else throw std::runtime_error("unknown option: " + std::string(key));
    }
    if (options.mode == "wirelink-object")
    {
        if (!iterations_set) options.iterations = 1;
        if (!warmup_set) options.warmup = 0;
        if (!timeout_set) options.timeout = std::chrono::milliseconds(30'000);
        if (!idle_set) options.idle_mode = "hybrid";
        if (options.object_bytes == 0 || options.object_bytes > MaxObjectBytes ||
            options.object_chunk == 0 || options.object_chunk > DefaultObjectChunk ||
            options.iterations == 0 || options.iterations > UINT32_MAX ||
            options.warmup > UINT32_MAX - options.iterations)
        {
            throw std::runtime_error(
                "object bytes must be 1..64 MiB, chunk must be 1..2016, and "
                "the warm-up plus iteration count must be 1..UINT32_MAX");
        }
        if (options.transfer_id_base == 0)
        {
            uint64_t seed = static_cast<uint64_t>(
                std::chrono::system_clock::now().time_since_epoch().count());
            seed ^= seed >> 33;
            seed *= UINT64_C(0xff51afd7ed558ccd);
            seed ^= seed >> 33;
            options.transfer_id_base = static_cast<uint32_t>(seed);
            if (options.transfer_id_base == 0) options.transfer_id_base = 1;
        }
    }
    else if (transfer_id_base_set)
    {
        throw std::runtime_error(
            "transfer ID base is only valid in wirelink-object mode");
    }
    else if (options.payload_size < sizeof(uint32_t) ||
             options.payload_size > EchoMaxPayload || options.iterations == 0)
    {
        throw std::runtime_error("payload must be 4..2048 and iterations must be nonzero");
    }
    if (options.iterations > UINT32_MAX || options.warmup > UINT32_MAX - options.iterations)
      throw std::runtime_error("warm-up plus iterations must fit uint32");
    if (options.mode == "cdc" && options.port.empty())
    {
        throw std::runtime_error("cdc mode requires --port");
    }
    if (options.idle_mode != "poll" && options.idle_mode != "wait" &&
        options.idle_mode != "hybrid")
    {
        throw std::runtime_error("idle must be poll, wait, or hybrid");
    }
    if (options.mode == "raw-bulk" && options.idle_mode != "poll")
    {
        throw std::runtime_error(
            "raw-bulk uses its own callback wait; select --idle poll");
    }
    if (options.wake_policy != "all")
    {
        throw std::runtime_error("wake must be all (physical progress wakes are required)");
    }
    return options;
}

std::vector<uint8_t> make_payload(std::size_t size, uint32_t sequence)
{
    std::vector<uint8_t> payload(size);
    for (std::size_t index = 0; index < size; ++index)
    {
        payload[index] = static_cast<uint8_t>((sequence * 31U + index * 17U) & 0xffU);
    }
    std::memcpy(payload.data(), &sequence, sizeof(sequence));
    return payload;
}

std::vector<uint8_t> make_object(std::size_t size)
{
    std::vector<uint8_t> object(size);
    for (std::size_t index = 0; index < object.size(); ++index)
        object[index] = static_cast<uint8_t>(index) ^ UINT8_C(0xa5);
    return object;
}

uint32_t transfer_id_at(uint32_t base, std::size_t index)
{
    const uint64_t ordinal = static_cast<uint64_t>(base - 1U) + index;
    return static_cast<uint32_t>(ordinal % UINT32_MAX) + 1U;
}

double percentile(const std::vector<double>& sorted, double fraction)
{
    const auto index = static_cast<std::size_t>(
        fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

void print_results(const Options& options, std::vector<double> samples)
{
    std::sort(samples.begin(), samples.end());
    const double mean = std::accumulate(samples.begin(), samples.end(), 0.0) /
                        static_cast<double>(samples.size());
    const double total_seconds =
        std::accumulate(samples.begin(), samples.end(), 0.0) / 1'000'000.0;
    const double sequential_goodput =
        static_cast<double>(options.payload_size * samples.size()) / total_seconds;

    std::cout << std::fixed << std::setprecision(2)
              << "mode=" << options.mode << " idle=" << options.idle_mode
              << " wake=" << options.wake_policy
              << " payload=" << options.payload_size
              << " samples=" << samples.size() << "\n"
              << "rtt_us min=" << samples.front()
              << " p50=" << percentile(samples, 0.50)
              << " p95=" << percentile(samples, 0.95)
              << " p99=" << percentile(samples, 0.99)
              << " max=" << samples.back() << " mean=" << mean << "\n"
              << "sequential_payload_bytes_per_second=" << sequential_goodput << "\n";
}

class RawBulkRunner
{
public:
    explicit RawBulkRunner(const Options& options)
    {
        UsbBulkConfig config;
        config.device.vendor_id = options.vendor_id;
        config.device.product_id = options.product_id;
        config.read_queue_depth = 1;
        config.auto_reconnect = false;
        auto opened = UsbBulkDevice::open(config);
        if (!opened) throw std::system_error(opened.error());
        m_device = std::make_unique<UsbBulkDevice>(std::move(opened.value()));
        auto started = m_device->start_reads_borrowed(
            [this] { return UsbBorrowedBuffer{{m_rx_buffer.data(), m_rx_buffer.size()}, 1}; },
            [this](const std::error_code& error, UsbBorrowedBuffer, std::size_t length)
            {
                std::lock_guard lock(m_mutex);
                if (error) m_error = error;
                m_received.insert(m_received.end(), m_rx_buffer.begin(),
                                  m_rx_buffer.begin() + static_cast<std::ptrdiff_t>(length));
                m_condition.notify_all();
            });
        if (!started) { m_device->close(); m_device.reset(); throw std::system_error(started.error()); }
    }

    ~RawBulkRunner() { if (m_device) { m_device->close(); m_device.reset(); } }

    double exchange(const std::vector<uint8_t>& payload,
                    std::chrono::milliseconds timeout)
    {
        {
            std::lock_guard lock(m_mutex);
            m_received.clear();
            m_error.clear();
            m_write_done = false;
        }
        const auto begin = Clock::now();
        m_tx_buffer = payload;
        auto submitted = m_device->async_write_borrowed(
            m_tx_buffer, [this](const std::error_code& error, std::size_t)
            {
                std::lock_guard lock(m_mutex);
                if (error) m_error = error;
                m_write_done = true;
                m_condition.notify_all();
            });
        if (!submitted) throw std::system_error(submitted.error());

        std::unique_lock lock(m_mutex);
        if (!m_condition.wait_for(lock, timeout, [&]
            { return m_error || (m_write_done && m_received.size() >= payload.size()); }))
        {
            lock.unlock(); m_device->close();
            throw std::runtime_error("raw bulk exchange timed out");
        }
        if (m_error) throw std::system_error(m_error);
        if (!std::equal(payload.begin(), payload.end(), m_received.begin()))
        {
            throw std::runtime_error("raw bulk echo mismatch");
        }
        return Microseconds(Clock::now() - begin).count();
    }

private:
    std::unique_ptr<UsbBulkDevice> m_device;
    std::array<uint8_t, 4096> m_rx_buffer{};
    std::vector<uint8_t> m_tx_buffer;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<uint8_t> m_received;
    std::error_code m_error;
    bool m_write_done{};
};


void open(ExamplePacketIo &io, const Options &options) {
  if (options.mode == "cdc") io.serial(options.port);
  else io.usb(options.vendor_id, options.product_id);
}
std::vector<double> run_raw(const Options &options) {
  RawBulkRunner runner(options);
  std::vector<double> samples;
  for (std::size_t i = 0; i < options.warmup + options.iterations; ++i) {
    const auto payload = make_payload(options.payload_size, static_cast<uint32_t>(i));
    const auto elapsed = runner.exchange(payload, options.timeout);
    if (i >= options.warmup) samples.push_back(elapsed);
  }
  return samples;
}
std::vector<double> run_echo(const Options &options) {
  ExamplePacketIo io; open(io, options);
  std::vector<double> samples;
  for (std::size_t i = 0; i < options.warmup + options.iterations; ++i) {
    const auto payload = make_payload(options.payload_size, static_cast<uint32_t>(i));
    const auto start = Clock::now(), deadline = start + options.timeout;
    if (example_packet_tool_send(MessageId, payload.data(), payload.size()) != WL_OK)
      throw std::runtime_error("echo admission failed");
    bool matched = false;
    while (Clock::now() < deadline) {
      io.service();
      uint16_t id{}; std::size_t length{}; std::array<uint8_t, 2048> reply{};
      if (example_packet_tool_reply(&id, reply.data(), reply.size(), &length) == WL_OK) {
        if (id != MessageId || length != payload.size() ||
            !std::equal(payload.begin(), payload.end(), reply.begin()))
          throw std::runtime_error("echo mismatch");
        matched = true;
      }
      if (matched && example_packet_tool_sent()) break;
      if (options.idle_mode != "poll") io.wait(deadline);
    }
    if (!matched || !example_packet_tool_sent()) throw std::runtime_error("echo timed out");
    if (i >= options.warmup) samples.push_back(Microseconds(Clock::now() - start).count());
  }
  return samples;
}
// Application protocol, deliberately independent of core RPC/reliability.
// Retry the same idempotent command at most three times. A stale phase/offset
// cannot complete a new action. No automatic replay/remote cancellation claim.
class ObjectSender {
public:
  ExamplePacketIo io;
  std::size_t retries{};
  bool wait{true};
  template<class Value, class Encoder>
  bulk_status_t transact(uint16_t id, const Value &value, Encoder encoder,
                         uint32_t transfer, int phase, uint64_t expected,
                         Clock::time_point overall) {
    std::array<uint8_t, 2048> payload{}; std::size_t length{};
    if (encoder(&value, payload.data(), payload.size(), &length) != WL_CODEC_OK)
      throw std::runtime_error("object command encoding failed");
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
      if (Clock::now() >= overall) break;
      if (example_packet_tool_send(id, payload.data(), length) != WL_OK)
        throw std::runtime_error("object command still physically outstanding");
      const auto deadline = std::min(overall, Clock::now() + std::chrono::milliseconds(250));
      bulk_status_t accepted{}; bool matched = false;
      while (Clock::now() < deadline) {
        io.service();
        uint16_t incoming{}; std::size_t size{}; std::array<uint8_t, 2048> data{};
        if (example_packet_tool_reply(&incoming, data.data(), data.size(), &size) == WL_OK) {
          bulk_status_t status{};
          if (incoming != BULK_STATUS_MESSAGE_ID ||
              bulk_status_decode(data.data(), size, &status) != WL_CODEC_OK ||
              !status.has_transfer_id || !status.has_phase || !status.has_code ||
              !status.has_next_offset || !status.has_accepted_chunk_size)
            throw std::runtime_error("malformed object status");
          if (status.transfer_id == transfer && status.phase == phase) {
            if (status.code != CONTROL_BULK_STATUS_OK)
              throw std::runtime_error("object application rejected command: " + std::to_string(status.code));
            if (status.next_offset == expected) { accepted = status; matched = true; }
          }
        }
        if (matched && example_packet_tool_sent()) return accepted;
        if (wait) io.wait(deadline);
      }
      if (!example_packet_tool_sent())
        throw std::runtime_error("physical TX timeout; cannot retry a borrowed transfer");
      if (attempt < 2) ++retries;
    }
    throw std::runtime_error("object status timeout after bounded application retries");
  }
};
void run_wirelink_object(const Options &options) {
  ObjectSender sender; sender.wait = options.idle_mode != "poll"; open(sender.io, options);
  auto object = make_object(options.object_bytes);
  const auto crc = wl_crc32c(object.data(), object.size());
  std::vector<double> samples;
  for (std::size_t i = 0; i < options.warmup + options.iterations; ++i) {
    const auto start = Clock::now(), deadline = start + options.timeout;
    const auto transfer = transfer_id_at(options.transfer_id_base, i);
    bulk_begin_t begin{};
    begin.has_transfer_id = begin.has_total_length = begin.has_requested_chunk_size = begin.has_object_crc32c = true;
    begin.transfer_id = transfer; begin.total_length = object.size();
    begin.requested_chunk_size = static_cast<uint32_t>(options.object_chunk); begin.object_crc32c = crc;
    const auto status = sender.transact(BULK_BEGIN_MESSAGE_ID, begin, bulk_begin_encode,
      transfer, CONTROL_BULK_PHASE_BEGIN, 0, deadline);
    if (!status.accepted_chunk_size || status.accepted_chunk_size > options.object_chunk)
      throw std::runtime_error("invalid accepted chunk size");
    for (std::size_t offset = 0; offset < object.size();) {
      const auto size = std::min<std::size_t>(status.accepted_chunk_size, object.size() - offset);
      bulk_chunk_t chunk{};
      chunk.has_transfer_id = chunk.has_offset = chunk.has_data = true;
      chunk.transfer_id = transfer; chunk.offset = offset;
      chunk.data.data = object.data() + offset; chunk.data.length = size;
      (void)sender.transact(BULK_CHUNK_MESSAGE_ID, chunk, bulk_chunk_encode,
        transfer, CONTROL_BULK_PHASE_CHUNK, offset + size, deadline);
      offset += size;
    }
    bulk_end_t end{}; end.has_transfer_id = end.has_total_length = end.has_object_crc32c = true;
    end.transfer_id = transfer; end.total_length = object.size(); end.object_crc32c = crc;
    (void)sender.transact(BULK_END_MESSAGE_ID, end, bulk_end_encode,
      transfer, CONTROL_BULK_PHASE_END, object.size(), deadline);
    if (i >= options.warmup) samples.push_back(Microseconds(Clock::now() - start).count());
  }
  const auto seconds = std::accumulate(samples.begin(), samples.end(), 0.0) / 1e6;
  std::cout << "workload=wirelink-packet-object-v1 transfers=" << samples.size()
    << " bytes=" << object.size() << " retries=" << sender.retries
    << " payload_bytes_per_second=" << object.size() * static_cast<double>(samples.size()) / seconds << '\n';
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      std::cout << "modes: raw-bulk | wirelink-bulk | wirelink-object | cdc\n"
        "--vid N --pid N --port PATH --payload N --iterations N --warmup N\n"
        "--bytes N --chunk N --transfer-id-base N --timeout-ms N --idle poll|wait|hybrid --wake all\n";
      return 0;
    }
    const auto options = parse_options(argc, argv);
    std::cout << "workload=wirelink-packet-transport-v1\n";
    if (options.mode == "wirelink-object") run_wirelink_object(options);
    else if (options.mode == "raw-bulk") print_results(options, run_raw(options));
    else if (options.mode == "wirelink-bulk" || options.mode == "cdc") print_results(options, run_echo(options));
    else throw std::runtime_error("unknown transport mode");
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
