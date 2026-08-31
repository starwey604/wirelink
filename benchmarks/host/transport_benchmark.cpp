/* SPDX-License-Identifier: Apache-2.0 */

#include <astrial/Usb.hpp>

#include "wirelink/astrial/serial_adapter.hpp"
#include "wirelink/astrial/usb_bulk_adapter.hpp"
#include "wirelink/wirelink.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::duration<double, std::micro>;

constexpr std::size_t MaxPayload = 512;
constexpr std::size_t UnitStorage = 576;
constexpr std::size_t RingStorage = 4096;
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
    std::chrono::milliseconds timeout{1000};
    std::chrono::microseconds spin{50};
};

uint64_t parse_number(std::string_view value)
{
    std::size_t used{};
    const auto parsed = std::stoull(std::string(value), &used, 0);
    if (used != value.size()) throw std::runtime_error("invalid numeric argument");
    return parsed;
}

Options parse_options(int argc, char** argv)
{
    if (argc < 2) throw std::runtime_error("missing mode: raw-bulk, wirelink-bulk, or cdc");
    Options options;
    options.mode = argv[1];
    for (int index = 2; index < argc; index += 2)
    {
        if (index + 1 >= argc) throw std::runtime_error("option has no value");
        const std::string_view key = argv[index];
        const std::string_view value = argv[index + 1];
        if (key == "--vid") options.vendor_id = static_cast<uint16_t>(parse_number(value));
        else if (key == "--pid") options.product_id = static_cast<uint16_t>(parse_number(value));
        else if (key == "--port") options.port = value;
        else if (key == "--payload") options.payload_size = parse_number(value);
        else if (key == "--warmup") options.warmup = parse_number(value);
        else if (key == "--iterations") options.iterations = parse_number(value);
        else if (key == "--timeout-ms") options.timeout =
            std::chrono::milliseconds(parse_number(value));
        else if (key == "--idle") options.idle_mode = value;
        else if (key == "--wake") options.wake_policy = value;
        else if (key == "--spin-us") options.spin =
            std::chrono::microseconds(parse_number(value));
        else throw std::runtime_error("unknown option: " + std::string(key));
    }
    if (options.payload_size < sizeof(uint32_t) || options.payload_size > MaxPayload ||
        options.iterations == 0)
    {
        throw std::runtime_error("payload must be 4..512 and iterations must be nonzero");
    }
    if (options.mode == "cdc" && options.port.empty())
    {
        throw std::runtime_error("cdc mode requires --port");
    }
    if (options.idle_mode != "poll" && options.idle_mode != "wait" &&
        options.idle_mode != "hybrid")
    {
        throw std::runtime_error("idle must be poll, wait, or hybrid");
    }
    if (options.mode != "wirelink-bulk" && options.idle_mode != "poll")
    {
        throw std::runtime_error("wait and hybrid idle require wirelink-bulk mode");
    }
    if (options.wake_policy != "all" && options.wake_policy != "rx")
    {
        throw std::runtime_error("wake must be all or rx");
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
              << " spin_us=" << options.spin.count()
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
        auto started = m_device->start_reads(
            [this] { return UsbBorrowedBuffer{{m_rx_buffer.data(), m_rx_buffer.size()}, 1}; },
            [this](const std::error_code& error, UsbBorrowedBuffer, std::size_t length)
            {
                std::lock_guard lock(m_mutex);
                if (error) m_error = error;
                m_received.insert(m_received.end(), m_rx_buffer.begin(),
                                  m_rx_buffer.begin() + static_cast<std::ptrdiff_t>(length));
                m_condition.notify_all();
            });
        if (!started) throw std::system_error(started.error());
    }

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
        auto submitted = m_device->async_write_borrowed(
            payload, [this](const std::error_code& error, std::size_t)
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
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<uint8_t> m_received;
    std::error_code m_error;
    bool m_write_done{};
};

struct LinkFixture
{
    wl_ctx_t link{};
    std::array<uint8_t, MaxPayload> tx_payload{};
    std::array<uint8_t, UnitStorage> tx_unit{};
    std::array<uint8_t, 64> control_unit{};
    std::array<uint8_t, RingStorage> rx_fifo{};
    std::array<uint8_t, UnitStorage> rx_fallback{};

    LinkFixture()
    {
        const wl_config_t config{
            .max_payload_len = MaxPayload,
            .envelope = WL_ENVELOPE_COBS_STREAM,
            .integrity = WL_INTEGRITY_NONE,
            .session_id = UINT64_C(0x484f535442454e43),
            .max_retries = 2,
            .ack_timeout_ms = 20,
            .max_transmission_unit = UnitStorage,
        };
        const wl_storage_t storage{
            .tx_payload = tx_payload.data(), .tx_payload_size = tx_payload.size(),
            .tx_unit = tx_unit.data(), .tx_unit_size = tx_unit.size(),
            .control_unit = control_unit.data(), .control_unit_size = control_unit.size(),
            .rx_fifo = rx_fifo.data(), .rx_fifo_size = rx_fifo.size(),
            .rx_fallback = rx_fallback.data(), .rx_fallback_size = rx_fallback.size(),
        };
        const int result = wl_init(&link, &config, &storage);
        if (result != WL_OK) throw std::runtime_error("wl_init failed");
    }
};

uint32_t now_ms()
{
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count());
}

template <typename Wait>
void idle_until(const Options& options, Wait& wait,
                Clock::time_point spin_deadline, Clock::time_point deadline)
{
    const auto now = Clock::now();
    if (options.idle_mode == "poll" ||
        (options.idle_mode == "hybrid" && now < spin_deadline))
    {
        std::this_thread::yield();
        return;
    }
    if (now < deadline)
    {
        (void)wait(std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now));
    }
}

template <typename Service, typename Wait>
double wirelink_exchange(LinkFixture& fixture, Service&& service,
                         Wait&& wait, const Options& options,
                         const std::vector<uint8_t>& payload,
                         std::chrono::milliseconds timeout)
{
    auto deadline = Clock::now() + timeout;
    auto spin_deadline = Clock::now() + options.spin;
    int result;
    do
    {
        result = wl_send_unreliable(&fixture.link, MessageId, payload.data(), payload.size());
        if (result == WL_OK) break;
        if (result != WL_ERR_BUSY)
        {
            throw std::runtime_error("wl_send_unreliable failed: " +
                                     std::to_string(result));
        }
        const int service_result = service();
        if (service_result != WL_OK && service_result != WL_ERR_WOULD_BLOCK)
        {
            throw std::runtime_error("adapter service failed: " +
                                     std::to_string(service_result));
        }
        idle_until(options, wait, spin_deadline, deadline);
    } while (Clock::now() < deadline);
    if (result != WL_OK) throw std::runtime_error("previous TX did not complete");

    const auto begin = Clock::now();
    deadline = begin + timeout;
    spin_deadline = begin + options.spin;

    while (Clock::now() < deadline)
    {
        result = service();
        if (result != WL_OK && result != WL_ERR_WOULD_BLOCK)
        {
            throw std::runtime_error("adapter service failed: " +
                                     std::to_string(result));
        }

        for (;;)
        {
            wl_event_t event{};
            result = wl_poll(&fixture.link, now_ms(), &event);
            if (result == WL_ERR_NO_DATA) break;
            if (result != WL_OK)
            {
                throw std::runtime_error("wl_poll failed: " +
                                         std::to_string(result));
            }
            if (event.type == WL_EVT_UNRELIABLE_RX && event.message_id == MessageId)
            {
                const bool matches = event.payload_len == payload.size() &&
                    std::equal(payload.begin(), payload.end(), event.payload);
                wl_event_release(&fixture.link, &event);
                if (!matches) throw std::runtime_error("Wirelink echo mismatch");
                return Microseconds(Clock::now() - begin).count();
            }
            wl_event_release(&fixture.link, &event);
        }
        idle_until(options, wait, spin_deadline, deadline);
    }
    throw std::runtime_error("Wirelink exchange timed out");
}

std::vector<double> run_raw(const Options& options)
{
    RawBulkRunner runner(options);
    std::vector<double> samples;
    for (std::size_t index = 0; index < options.warmup + options.iterations; ++index)
    {
        const double elapsed = runner.exchange(make_payload(options.payload_size,
                                                              static_cast<uint32_t>(index)),
                                                options.timeout);
        if (index >= options.warmup) samples.push_back(elapsed);
    }
    return samples;
}

std::vector<double> run_wirelink_bulk(const Options& options)
{
    LinkFixture fixture;
    wirelink::astrial::UsbBulkAdapterConfig config;
    config.usb.device.vendor_id = options.vendor_id;
    config.usb.device.product_id = options.product_id;
    config.usb.auto_reconnect = false;
    config.maximum_read_size = UnitStorage;
    config.wake_policy = options.wake_policy == "rx"
        ? wirelink::astrial::UsbBulkWakePolicy::ReceiveOnly
        : wirelink::astrial::UsbBulkWakePolicy::AllCompletions;
    auto opened = wirelink::astrial::UsbBulkAdapter::open(fixture.link, config);
    if (!opened) throw std::system_error(opened.error());
    auto adapter = std::move(opened.value());
    std::vector<double> samples;
    for (std::size_t index = 0; index < options.warmup + options.iterations; ++index)
    {
        const auto payload = make_payload(options.payload_size, static_cast<uint32_t>(index));
        const double elapsed = wirelink_exchange(
            fixture, [&] { return adapter->service(); },
            [&](std::chrono::nanoseconds timeout)
            {
                return adapter->wait_for_activity(timeout);
            },
            options, payload, options.timeout);
        if (index >= options.warmup) samples.push_back(elapsed);
    }
    wirelink::astrial::UsbBulkAdapterStats stats{};
    adapter->get_stats(stats);
    std::cout << "adapter_activity notifications=" << stats.activity_notifications
              << " waits=" << stats.wait_calls
              << " wakeups=" << stats.wait_wakeups
              << " timeouts=" << stats.wait_timeouts << "\n";
    return samples;
}

std::vector<double> run_cdc(const Options& options)
{
    LinkFixture fixture;
    wirelink::astrial::SerialConfig config;
    config.port = options.port;
    config.auto_reconnect = false;
    auto opened = wirelink::astrial::SerialAdapter::open(fixture.link, config);
    if (!opened) throw std::system_error(opened.error());
    auto adapter = std::move(opened.value());
    std::vector<double> samples;
    for (std::size_t index = 0; index < options.warmup + options.iterations; ++index)
    {
        const auto payload = make_payload(options.payload_size, static_cast<uint32_t>(index));
        const double elapsed = wirelink_exchange(
            fixture, [&] { return adapter->service(); },
            [](std::chrono::nanoseconds) { return false; },
            options, payload, options.timeout);
        if (index >= options.warmup) samples.push_back(elapsed);
    }
    return samples;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto options = parse_options(argc, argv);
        std::vector<double> samples;
        if (options.mode == "raw-bulk") samples = run_raw(options);
        else if (options.mode == "wirelink-bulk") samples = run_wirelink_bulk(options);
        else if (options.mode == "cdc") samples = run_cdc(options);
        else throw std::runtime_error("mode must be raw-bulk, wirelink-bulk, or cdc");
        print_results(options, std::move(samples));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
