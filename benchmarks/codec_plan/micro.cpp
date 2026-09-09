/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <benchmark/benchmark.h>
#include <string>
static bool failed;
static void run(benchmark::State &state, unsigned kind, unsigned pattern, unsigned stage) {
  codec_plan_fixture_t f;
  if (codec_plan_init(&f, kind, pattern, stage) != WL_CODEC_OK) {
    failed = true; state.SkipWithError("init failed"); return;
  }
  for (auto _ : state) {
    (void)_;
    if (codec_plan_step(&f) != WL_CODEC_OK) { failed = true; state.SkipWithError("step failed"); break; }
    benchmark::ClobberMemory();
  }
  if (stage == 2 && f.output_length != f.expected_length) { failed = true; state.SkipWithError("size mismatch"); }
  if (codec_plan_check(&f) != WL_CODEC_OK) { failed = true; state.SkipWithError("oracle mismatch"); }
  state.counters["encoded_bytes"] = double(f.expected_length);
}
int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  benchmark::AddCustomContext("workload", "wirelink-codec-plan-v1");
  for (unsigned k = 0; k < CODEC_PLAN_KINDS; ++k)
    for (unsigned p = 0; p < CODEC_PLAN_PATTERNS; ++p)
      for (unsigned s = 0; s < CODEC_PLAN_STAGES; ++s) {
        std::string name = "k" + std::to_string(k) + "/p" + std::to_string(p) + "/s" + std::to_string(s);
        benchmark::RegisterBenchmark(name.c_str(), run, k, p, s);
      }
  benchmark::RunSpecifiedBenchmarks(); benchmark::Shutdown();
  return failed ? 1 : 0;
}
