/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_HOST_EXECUTOR_ACTIVITY_HPP
#define WIRELINK_HOST_EXECUTOR_ACTIVITY_HPP

#include <array>
#include <atomic>
#include <cstdint>

namespace wirelink::host {
// Logical work counters, not OS context switches or timing samples. Enable via
// Wirelink::host's PUBLIC build definition; default builds have no counter work.
enum class ExecutorActivity : unsigned {
    passes, core_events, rx_events, no_reported_work,
    rpc_batches, rpc_jobs, rpc_completions, rpc_empty_collections,
    latest_attempts, latest_sent, latest_deferred,
    notifications, feed_without_bytes,
    continue_progress, continue_hint, continue_deadline, continue_notification,
    waits, count
};
inline constexpr std::array executor_activity_names{
    "passes", "core_events", "rx_events", "no_reported_work",
    "rpc_batches", "rpc_jobs", "rpc_completions", "rpc_empty_collections",
    "latest_attempts", "latest_sent", "latest_deferred",
    "notifications", "feed_without_bytes",
    "continue_progress", "continue_hint", "continue_deadline", "continue_notification",
    "waits"
};
static_assert(executor_activity_names.size() == static_cast<unsigned>(ExecutorActivity::count));

struct ExecutorActivitySnapshot {
    bool enabled{};
    std::array<std::uint64_t, static_cast<unsigned>(ExecutorActivity::count)> counts{};
    std::uint64_t get(ExecutorActivity counter) const noexcept {
        return counts[static_cast<unsigned>(counter)];
    }
};

namespace detail {
class ExecutorActivityCounters {
public:
    void add(ExecutorActivity counter, std::uint64_t count = 1) noexcept {
#if defined(WIRELINK_HOST_ACTIVITY)
        counts_[static_cast<unsigned>(counter)].fetch_add(count, std::memory_order_relaxed);
#else
        (void)counter; (void)count;
#endif
    }
    ExecutorActivitySnapshot snapshot() const noexcept {
        ExecutorActivitySnapshot result;
#if defined(WIRELINK_HOST_ACTIVITY)
        result.enabled = true;
        for (unsigned i = 0; i < result.counts.size(); ++i)
            result.counts[i] = counts_[i].load(std::memory_order_relaxed);
#endif
        return result;
    }
private:
#if defined(WIRELINK_HOST_ACTIVITY)
    std::array<std::atomic<std::uint64_t>, static_cast<unsigned>(ExecutorActivity::count)> counts_{};
#endif
};
} // namespace detail
} // namespace wirelink::host
#endif
