/* SPDX-License-Identifier: Apache-2.0 */

#include <astrial/Usb.hpp>

#include "control.h"
#include "wirelink/astrial/serial_adapter.hpp"
#include "wirelink/astrial/usb_bulk_adapter.hpp"
#include "wirelink/bulk.h"
#include "wirelink/crc.h"
#include "wirelink/frame.h"
#include "wirelink/wirelink.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
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

constexpr std::size_t EchoMaxPayload = 512;
constexpr std::size_t MaxPayload = WL_FRAME_MAX_PAYLOAD;
constexpr std::size_t UnitStorage = WL_FRAME_MAX_COBS_LEN;
constexpr std::size_t RingStorage = 2 * UnitStorage;
constexpr std::size_t DefaultObjectBytes = 1024 * 1024;
constexpr std::size_t MaxObjectBytes = 64 * 1024 * 1024;
constexpr std::size_t DefaultObjectChunk = 2016;
constexpr uint16_t MessageId = 0x424d;

static_assert(CONTROL_BULK_PHASE_BEGIN == WL_BULK_PHASE_BEGIN &&
              CONTROL_BULK_PHASE_CHUNK == WL_BULK_PHASE_CHUNK &&
              CONTROL_BULK_PHASE_END == WL_BULK_PHASE_END &&
              CONTROL_BULK_PHASE_ABORT == WL_BULK_PHASE_ABORT);
static_assert(CONTROL_BULK_STATUS_TIMED_OUT == WL_BULK_STATUS_TIMED_OUT);

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
    std::chrono::microseconds spin{50};
};

uint64_t parse_number(std::string_view value)
{
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
        if (key == "--vid") options.vendor_id = static_cast<uint16_t>(parse_number(value));
        else if (key == "--pid") options.product_id = static_cast<uint16_t>(parse_number(value));
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
            options.timeout = std::chrono::milliseconds(parse_number(value));
            timeout_set = true;
        }
        else if (key == "--idle")
        {
            options.idle_mode = value;
            idle_set = true;
        }
        else if (key == "--wake") options.wake_policy = value;
        else if (key == "--spin-us") options.spin =
            std::chrono::microseconds(parse_number(value));
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
    if (options.mode != "wirelink-bulk" &&
        options.mode != "wirelink-object" && options.idle_mode != "poll")
    {
        throw std::runtime_error(
            "wait and hybrid idle require wirelink-bulk or wirelink-object mode");
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
    std::array<uint8_t, MaxPayload> encode_scratch{};

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

template <typename Message>
int encode_and_send(LinkFixture& fixture, uint16_t message_id,
                    const Message& message,
                    wl_codec_status_t (*encoder)(const Message*, uint8_t*,
                                                 size_t, size_t*))
{
    size_t encoded_length{};
    const auto codec_result = encoder(&message, fixture.encode_scratch.data(),
                                      fixture.encode_scratch.size(),
                                      &encoded_length);
    if (codec_result != WL_CODEC_OK)
        throw std::runtime_error("bulk message encode failed: " +
                                 std::to_string(codec_result));
    return wl_send_unreliable(&fixture.link, message_id,
                              fixture.encode_scratch.data(), encoded_length);
}

int send_bulk_action(LinkFixture& fixture, const wl_bulk_sender_action_t& action,
                     std::span<const uint8_t> object)
{
    switch (action.phase)
    {
    case WL_BULK_PHASE_BEGIN:
    {
        const bulk_begin_t message{
            .has_transfer_id = true,
            .transfer_id = action.descriptor.transfer_id,
            .has_total_length = true,
            .total_length = action.descriptor.total_length,
            .has_requested_chunk_size = true,
            .requested_chunk_size = action.descriptor.requested_chunk_size,
            .has_object_crc32c = true,
            .object_crc32c = action.descriptor.object_crc32c,
        };
        return encode_and_send(fixture, BULK_BEGIN_MESSAGE_ID, message,
                               bulk_begin_encode);
    }
    case WL_BULK_PHASE_CHUNK:
    {
        if (action.offset > object.size() ||
            action.length > object.size() - static_cast<std::size_t>(action.offset))
            throw std::runtime_error("bulk sender produced an invalid source span");
        const bulk_chunk_t message{
            .has_transfer_id = true,
            .transfer_id = action.descriptor.transfer_id,
            .has_offset = true,
            .offset = action.offset,
            .has_data = true,
            .data = {object.data() + static_cast<std::size_t>(action.offset),
                     action.length},
        };
        return encode_and_send(fixture, BULK_CHUNK_MESSAGE_ID, message,
                               bulk_chunk_encode);
    }
    case WL_BULK_PHASE_END:
    {
        const bulk_end_t message{
            .has_transfer_id = true,
            .transfer_id = action.descriptor.transfer_id,
            .has_total_length = true,
            .total_length = action.descriptor.total_length,
            .has_object_crc32c = true,
            .object_crc32c = action.descriptor.object_crc32c,
        };
        return encode_and_send(fixture, BULK_END_MESSAGE_ID, message,
                               bulk_end_encode);
    }
    case WL_BULK_PHASE_ABORT:
    {
        const bulk_abort_t message{
            .has_transfer_id = true,
            .transfer_id = action.descriptor.transfer_id,
            .has_reason = true,
            .reason = action.abort_reason,
        };
        return encode_and_send(fixture, BULK_ABORT_MESSAGE_ID, message,
                               bulk_abort_encode);
    }
    default:
        throw std::runtime_error("bulk sender produced an invalid action phase");
    }
}

wl_bulk_status_t decode_bulk_status(const wl_event_t& event)
{
    bulk_status_t message{};
    if (event.message_id != BULK_STATUS_MESSAGE_ID ||
        bulk_status_decode(event.payload, event.payload_len, &message) != WL_CODEC_OK ||
        !message.has_transfer_id || !message.has_phase || !message.has_code ||
        !message.has_next_offset || !message.has_accepted_chunk_size)
    {
        throw std::runtime_error("malformed BulkStatus response");
    }
    return {
        .transfer_id = message.transfer_id,
        .phase = message.phase,
        .code = message.code,
        .next_offset = message.next_offset,
        .accepted_chunk_size = message.accepted_chunk_size,
    };
}

struct ObjectCounters
{
    uint64_t actions{};
    uint64_t statuses{};
    uint64_t retries{};
    uint64_t busy{};
};

void add_sender_stats(ObjectCounters& counters,
                      const wl_bulk_sender_stats_t& stats)
{
    counters.actions += stats.actions_submitted;
    counters.statuses += stats.statuses_received;
    counters.retries += stats.retries;
    counters.busy += stats.busy_responses;
}

void run_wirelink_object(const Options& options)
{
    LinkFixture fixture;
    const auto object = make_object(options.object_bytes);
    const uint32_t object_crc32c = wl_crc32c(object.data(), object.size());
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
    auto wait_for_usb = [&](std::chrono::nanoseconds timeout)
    {
        return adapter->wait_for_activity(timeout);
    };

    std::vector<double> transfer_us;
    std::vector<double> status_rtt_us;
    ObjectCounters counters;
    double measured_cpu_us{};
    wirelink::astrial::UsbBulkAdapterStats adapter_baseline{};

    for (std::size_t iteration = 0;
         iteration < options.warmup + options.iterations; ++iteration)
    {
        const bool measured = iteration >= options.warmup;
        if (iteration == options.warmup) adapter->get_stats(adapter_baseline);

        wl_bulk_sender_t sender{};
        const wl_bulk_sender_config_t sender_config{
            .status_timeout_ms = 100,
            .busy_retry_ms = 1,
            .max_retries = 10,
        };
        const wl_bulk_descriptor_t descriptor{
            .transfer_id = transfer_id_at(options.transfer_id_base, iteration),
            .total_length = object.size(),
            .requested_chunk_size = static_cast<uint32_t>(options.object_chunk),
            .object_crc32c = object_crc32c,
        };
        if (descriptor.transfer_id == 0 ||
            wl_bulk_sender_init(&sender, &sender_config) != WL_BULK_OK ||
            wl_bulk_sender_start(&sender, &descriptor) != WL_BULK_OK)
        {
            throw std::runtime_error("bulk sender initialization failed");
        }

        const auto transfer_begin = Clock::now();
        const auto transfer_deadline = transfer_begin + options.timeout;
        const auto cpu_begin = std::clock();
        Clock::time_point action_submitted_at{};
        auto spin_deadline = transfer_begin;
        bool action_outstanding{};
        bool completed{};

        while (Clock::now() < transfer_deadline)
        {
            const int service_result = adapter->service();
            if (service_result != WL_OK && service_result != WL_ERR_WOULD_BLOCK)
                throw std::runtime_error("USB adapter service failed: " +
                                         std::to_string(service_result));

            for (;;)
            {
                wl_event_t event{};
                const int poll_result = wl_poll(&fixture.link, now_ms(), &event);
                if (poll_result == WL_ERR_NO_DATA) break;
                if (poll_result != WL_OK)
                    throw std::runtime_error("wl_poll failed: " +
                                             std::to_string(poll_result));

                if ((event.type == WL_EVT_UNRELIABLE_RX ||
                     event.type == WL_EVT_RELIABLE_RX) &&
                    event.message_id == BULK_STATUS_MESSAGE_ID)
                {
                    const auto status = decode_bulk_status(event);
                    if (status.phase == WL_BULK_PHASE_BEGIN &&
                        status.code == WL_BULK_STATUS_OK &&
                        status.next_offset != 0)
                    {
                        wl_event_release(&fixture.link, &event);
                        throw std::runtime_error(
                            "peer retained this transfer ID; choose a fresh "
                            "--transfer-id-base");
                    }
                    const auto status_result =
                        wl_bulk_sender_on_status(&sender, &status, now_ms());
                    if (status_result == WL_BULK_OK)
                    {
                        if (action_outstanding && measured)
                            status_rtt_us.push_back(
                                Microseconds(Clock::now() - action_submitted_at).count());
                        action_outstanding = false;
                    }
                    else if (status_result != WL_BULK_ERR_NOT_FOUND)
                    {
                        wl_event_release(&fixture.link, &event);
                        throw std::runtime_error("bulk Status rejected: " +
                                                 std::to_string(status_result));
                    }
                }
                wl_event_release(&fixture.link, &event);
            }

            const auto current_ms = now_ms();
            if (wl_bulk_sender_poll(&sender, current_ms) != WL_BULK_OK)
                throw std::runtime_error("bulk sender poll failed");

            wl_bulk_sender_result_t sender_result{};
            if (wl_bulk_sender_get_result(&sender, &sender_result) != WL_BULK_OK)
                throw std::runtime_error("bulk sender result failed");
            if (sender_result.state == WL_BULK_SENDER_COMPLETED)
            {
                completed = true;
                break;
            }
            if (sender_result.state == WL_BULK_SENDER_FAILED ||
                sender_result.state == WL_BULK_SENDER_ABORTED)
            {
                throw std::runtime_error(
                    "object transfer terminated: state=" +
                    std::to_string(sender_result.state) + " status=" +
                    std::to_string(sender_result.status) + " offset=" +
                    std::to_string(sender_result.next_offset));
            }

            wl_bulk_sender_action_t action{};
            const auto action_result =
                wl_bulk_sender_action_acquire(&sender, &action);
            if (action_result == WL_BULK_OK)
            {
                const int send_result = send_bulk_action(fixture, action, object);
                if (send_result == WL_OK)
                {
                    if (wl_bulk_sender_action_submitted(&sender, &action, current_ms) !=
                        WL_BULK_OK)
                        throw std::runtime_error("bulk action submission failed");
                    action_submitted_at = Clock::now();
                    spin_deadline = action_submitted_at + options.spin;
                    action_outstanding = true;
                }
                else
                {
                    if (wl_bulk_sender_action_defer(&sender, &action) != WL_BULK_OK)
                        throw std::runtime_error("bulk action defer failed");
                    if (send_result != WL_ERR_BUSY)
                        throw std::runtime_error("bulk action send failed: " +
                                                 std::to_string(send_result));
                }
            }
            else if (action_result != WL_BULK_ERR_NOT_FOUND)
            {
                throw std::runtime_error("bulk action acquire failed: " +
                                         std::to_string(action_result));
            }

            wl_bulk_deadline_hint_t hint{};
            if (wl_bulk_sender_get_deadline_hint(&sender, now_ms(), &hint) !=
                WL_BULK_OK)
                throw std::runtime_error("bulk deadline hint failed");
            auto idle_deadline = Clock::now() + std::chrono::milliseconds(1);
            if (hint.next_deadline_ms != WL_BULK_NO_DEADLINE_MS)
                idle_deadline = Clock::now() +
                    std::chrono::milliseconds(hint.next_deadline_ms);
            idle_deadline = std::min(idle_deadline, transfer_deadline);
            idle_until(options, wait_for_usb, spin_deadline, idle_deadline);
        }

        if (!completed) throw std::runtime_error("object transfer timed out");
        const auto cpu_end = std::clock();
        if (measured)
        {
            transfer_us.push_back(Microseconds(Clock::now() - transfer_begin).count());
            if (cpu_begin != static_cast<std::clock_t>(-1) &&
                cpu_end != static_cast<std::clock_t>(-1))
            {
                measured_cpu_us +=
                    static_cast<double>(cpu_end - cpu_begin) * 1'000'000.0 /
                    static_cast<double>(CLOCKS_PER_SEC);
            }
            wl_bulk_sender_stats_t sender_stats{};
            if (wl_bulk_sender_get_stats(&sender, &sender_stats) != WL_BULK_OK)
                throw std::runtime_error("bulk sender stats failed");
            add_sender_stats(counters, sender_stats);
        }
    }

    wirelink::astrial::UsbBulkAdapterStats adapter_stats{};
    adapter->get_stats(adapter_stats);
    std::sort(transfer_us.begin(), transfer_us.end());
    std::sort(status_rtt_us.begin(), status_rtt_us.end());
    const double total_wall_us =
        std::accumulate(transfer_us.begin(), transfer_us.end(), 0.0);
    const double total_bytes =
        static_cast<double>(options.object_bytes) * options.iterations;
    const double goodput = total_bytes * 1'000'000.0 / total_wall_us;

    std::cout << std::fixed << std::setprecision(2)
              << "mode=wirelink-object idle=" << options.idle_mode
              << " spin_us=" << options.spin.count()
              << " wake=" << options.wake_policy
              << " object_bytes=" << options.object_bytes
              << " chunk=" << options.object_chunk
              << " transfer_id_base=" << options.transfer_id_base
              << " transfers=" << options.iterations << "\n"
              << "object_us min=" << transfer_us.front()
              << " p50=" << percentile(transfer_us, 0.50)
              << " p99=" << percentile(transfer_us, 0.99)
              << " max=" << transfer_us.back() << "\n"
              << "status_rtt_us samples=" << status_rtt_us.size()
              << " p50=" << percentile(status_rtt_us, 0.50)
              << " p99=" << percentile(status_rtt_us, 0.99)
              << " max=" << status_rtt_us.back() << "\n"
              << "payload_bytes_per_second=" << goodput << "\n"
              << "sender actions=" << counters.actions
              << " statuses=" << counters.statuses
              << " retries=" << counters.retries
              << " busy=" << counters.busy << "\n"
              << "host_cpu_us=" << measured_cpu_us
              << " host_cpu_percent=" << measured_cpu_us * 100.0 / total_wall_us
              << "\n"
              << "adapter rx_bytes="
              << adapter_stats.rx_bytes - adapter_baseline.rx_bytes
              << " rx_claims=" << adapter_stats.rx_claims - adapter_baseline.rx_claims
              << " rx_pauses=" << adapter_stats.rx_pauses - adapter_baseline.rx_pauses
              << " tx_submissions="
              << adapter_stats.tx_submissions - adapter_baseline.tx_submissions
              << " tx_completions="
              << adapter_stats.tx_completions - adapter_baseline.tx_completions
              << " waits=" << adapter_stats.wait_calls - adapter_baseline.wait_calls
              << " wakeups="
              << adapter_stats.wait_wakeups - adapter_baseline.wait_wakeups
              << " errors=" << adapter_stats.errors - adapter_baseline.errors << "\n";
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
        if (options.mode == "wirelink-object")
        {
            run_wirelink_object(options);
            return 0;
        }
        std::vector<double> samples;
        if (options.mode == "raw-bulk") samples = run_raw(options);
        else if (options.mode == "wirelink-bulk") samples = run_wirelink_bulk(options);
        else if (options.mode == "cdc") samples = run_cdc(options);
        else throw std::runtime_error(
            "mode must be raw-bulk, wirelink-bulk, wirelink-object, or cdc");
        print_results(options, std::move(samples));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
