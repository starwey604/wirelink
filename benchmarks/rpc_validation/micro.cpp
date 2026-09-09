/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <benchmark/benchmark.h>
#include <string>

static bool failed;
static void run(benchmark::State &state, unsigned kind, unsigned stage, size_t bytes) {
  rpc_validation_fixture_t fixture;
  if (rpc_validation_init(&fixture, kind, stage, bytes) != WL_CODEC_OK) {
    failed = true; state.SkipWithError("init failed"); return;
  }
  for (auto _ : state) {
    (void)_;
    if (rpc_validation_step(&fixture) != WL_CODEC_OK) {
      failed = true; state.SkipWithError("step failed"); break;
    }
    benchmark::ClobberMemory();
  }
  if (rpc_validation_check(&fixture) != WL_CODEC_OK) {
    failed = true; state.SkipWithError("oracle mismatch");
  }
  state.counters["encoded_bytes"] = double(fixture.input_length);
}
int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  benchmark::AddCustomContext("workload", "wirelink-rpc-validation-v2");
  const char *stages[] = {"decode", "canonical", "convert", "pipeline", "owned_decode"};
  for (unsigned kind = 0; kind < 3; ++kind)
    for (unsigned stage = 0; stage < RPC_VALIDATION_STAGES; ++stage)
      for (size_t bytes : {0U, 16U, 32U, 256U, 512U}) {
        if (kind == 0 && bytes > 32) continue;
        std::string name = std::string(stages[stage]) + "/k" + std::to_string(kind) + "/b" + std::to_string(bytes);
        benchmark::RegisterBenchmark(name.c_str(), run, kind, stage, bytes);
      }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return failed ? 1 : 0;
}
