/* SPDX-License-Identifier: Apache-2.0 */
#include "perf_endpoint.h"
#include "wirelink/loopback.h"
#include <benchmark/benchmark.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

static void check(bool ok) { if (!ok) std::abort(); }

struct Environment {
  uint64_t identity = 1;
  uint64_t reads = 0;
  static wl_time_ms_t now(void *context) {
    ++static_cast<Environment *>(context)->reads;
    return 1000; // Protocol time, never the benchmark's measurement clock.
  }
  static wl_err_t next(void *context, uint64_t *out) {
    *out = ++static_cast<Environment *>(context)->identity;
    return WL_OK;
  }
  wl_environment_t get() { return {{now, this}, {next, this}}; }
};

static int32_t echo(void *, const echo_request_value_t *request, echo_response_value_t *response) {
  response->has_sequence = response->has_body = true;
  response->sequence = request->sequence;
  response->body.length = request->body.length;
  std::memcpy(response->body.data, request->body.data, request->body.length);
  return 0;
}

struct Pair {
  Environment environment;
  perf_endpoint_t client{}, server{};
  wl_loopback_t cable{};
  echo_request_value_t request{};
  unsigned completed = 0;
  Pair() {
    perf_endpoint_config_t config;
    check(perf_endpoint_init(&client, environment.get()) == WL_OK);
    check(perf_endpoint_config_defaults(&config, environment.get()) == WL_OK);
    config.on_echo = echo;
    check(perf_endpoint_init_config(&server, &config) == WL_OK);
    check(wl_loopback_connect(&cable, perf_endpoint_handle(&client), perf_endpoint_handle(&server)) == WL_OK);
    request.has_sequence = request.has_body = true;
    std::memset(request.body.data, 0x5a, sizeof(request.body.data));
  }
  ~Pair() {
    check(perf_endpoint_close(&client) == WL_OK);
    check(perf_endpoint_close(&server) == WL_OK);
  }
  static void done(void *context, const wl_rpc_completion_t *result, const echo_response_value_t *response) {
    auto &pair = *static_cast<Pair *>(context);
    check(result->status == WL_RPC_SUCCESS && response != nullptr);
    check(response->sequence == pair.request.sequence && response->body.length == pair.request.body.length);
    check(std::memcmp(response->body.data, pair.request.body.data, response->body.length) == 0);
    ++pair.completed;
  }
  void step() {
    check(perf_endpoint_step(&client) == WL_OK);
    check(perf_endpoint_step(&server) == WL_OK);
  }
  void call(unsigned batch) {
    completed = 0;
    ++request.sequence;
    for (unsigned i = 0; i < batch; ++i)
      check(perf_endpoint_echo_async(&client, &request, 1500, done, this, nullptr) == WL_OK);
    unsigned passes = 0;
    while (completed < batch && ++passes < 128) step();
    check(completed == batch);
    // Include ACK/TX lease retirement, not merely response notification.
    step();
    step();
  }
};

static void counters(benchmark::State &state, uint64_t reads) {
  state.counters["endpoint_bytes"] = sizeof(perf_endpoint_t);
  state.counters["runtime_arena_bytes"] = PERF_ENDPOINT_RUNTIME_CAPACITY;
  state.counters["services"] = API_SERVICES;
  state.counters["slots"] = PERF_ENDPOINT_RPC_CAPACITY;
  state.counters["protocol_clock_reads/op"] = double(reads) / double(state.iterations());
}

static void Idle(benchmark::State &state) {
  auto pair = std::make_unique<Pair>();
  const auto before = pair->environment.reads;
  for (auto _ : state) {
    (void)_;
    check(perf_endpoint_step(state.range(0) == 0 ? &pair->client : &pair->server) == WL_OK);
  }
  counters(state, pair->environment.reads - before);
}
BENCHMARK(Idle)->Arg(0)->Arg(1);

static void Rpc(benchmark::State &state) {
  auto pair = std::make_unique<Pair>();
  pair->request.body.length = static_cast<uint16_t>(state.range(0));
  const auto batch = static_cast<unsigned>(state.range(1));
  for (unsigned i = 0; i < 32; ++i) pair->call(batch);
  const auto before = pair->environment.reads;
  for (auto _ : state) { (void)_; pair->call(batch); }
  counters(state, pair->environment.reads - before);
  state.SetItemsProcessed(state.iterations() * batch);
  state.SetBytesProcessed(state.iterations() * batch * state.range(0) * 2);
}
BENCHMARK(Rpc)->Args({0, 1})->Args({32, 1})->Args({512, 1})->Args({1024, 1});
#if PERF_ENDPOINT_RPC_CAPACITY >= 4
BENCHMARK(Rpc)->Args({32, 4});
#endif

static void Telemetry(benchmark::State &state) {
  auto pair = std::make_unique<Pair>();
  telemetry_t sent{}, received{};
  sent.has_values = true;
  sent.values[0] = 42;
  const auto before = pair->environment.reads;
  for (auto _ : state) {
    (void)_;
    check(perf_endpoint_send_telemetry(&pair->server, &sent).domain == LOAD_SEND_OK);
    pair->step();
    check(perf_endpoint_read_telemetry(&pair->client, &received) == WL_OK);
    check(received.values[0] == 42);
    benchmark::DoNotOptimize(received);
  }
  counters(state, pair->environment.reads - before);
}
BENCHMARK(Telemetry);

static void Initialize(benchmark::State &state) {
  auto endpoint = std::make_unique<perf_endpoint_t>();
  Environment environment;
  for (auto _ : state) {
    (void)_;
    perf_endpoint_config_t config;
    check(perf_endpoint_config_defaults(&config, environment.get()) == WL_OK);
    config.on_echo = echo;
    check(perf_endpoint_init_config(endpoint.get(), &config) == WL_OK);
    check(perf_endpoint_close(endpoint.get()) == WL_OK);
    benchmark::ClobberMemory();
  }
  counters(state, environment.reads);
}
BENCHMARK(Initialize);

int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  benchmark::AddCustomContext("workload", "wirelink-api-v1");
  benchmark::AddCustomContext("variant", API_VARIANT);
  benchmark::AddCustomContext("compiler", API_COMPILER);
  benchmark::AddCustomContext("build_type", API_BUILD_TYPE);
  benchmark::AddCustomContext("codegen_abi", std::to_string(PERF_RUNTIME_CODEGEN_ABI_VERSION));
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
