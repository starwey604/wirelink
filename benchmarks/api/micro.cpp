/* SPDX-License-Identifier: Apache-2.0 */
#include "fixture.h"
#include <benchmark/benchmark.h>
static void run(benchmark::State &state, int mode) {
  api_fixture_init();
  for (auto _ : state) {
    (void)_;
    if (mode == 0) api_fixture_idle();
    else if (mode == 1 || mode == 2) api_fixture_rpc(mode == 2);
    else api_fixture_sample(static_cast<size_t>(state.range(0)));
    benchmark::ClobberMemory();
  }
  api_fixture_close();
  state.counters["calls_per_op"] = mode == 1 || mode == 2 ? 8 : 0;
  state.counters["samples_per_op"] = mode == 3 ? 2 : 0;
  state.counters["endpoint_bytes"] = static_cast<double>(api_fixture_endpoint_bytes());
  state.counters["fixture_bytes"] = static_cast<double>(api_fixture_total_bytes());
}
int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 2;
  benchmark::AddCustomContext("workload", "wirelink-kernel-api-v1");
  benchmark::AddCustomContext("clock", "caller_manual_tick");
  benchmark::RegisterBenchmark("kernel_idle_pair", run, 0);
  benchmark::RegisterBenchmark("kernel_rpc_4x2", run, 1);
  benchmark::RegisterBenchmark("kernel_rejected_4x2", run, 2);
  benchmark::RegisterBenchmark("kernel_sample_pair", run, 3)->Arg(32)->Arg(120)->Arg(512)->Arg(1024);
  benchmark::RunSpecifiedBenchmarks(); benchmark::Shutdown();
}
