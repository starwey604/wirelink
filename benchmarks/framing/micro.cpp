/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <benchmark/benchmark.h>
#include <string>

static bool failed;

static void run(benchmark::State &state, unsigned mode, int envelope, int integrity, unsigned pattern, size_t bytes) {
  framing_fixture_t fixture;
  if (framing_init(&fixture, mode, envelope, integrity, pattern, bytes) != WL_OK ||
      framing_step(&fixture) != WL_OK || framing_check(&fixture) != WL_OK) {
    failed = true;
    state.SkipWithError("fixture validation failed"); return;
  }
  const auto initial_calls = fixture.calls;
  for (auto _ : state) {
    (void)_;
    if (framing_step(&fixture) != WL_OK) { failed = true; state.SkipWithError("workload failed"); break; }
    benchmark::ClobberMemory();
  }
  if (framing_check(&fixture) != WL_OK) {
    failed = true;
    state.SkipWithError("final bytes differ from reference");
  }
  state.counters["sink_calls_per_op"] = state.iterations() ?
      double(fixture.calls - initial_calls) / double(state.iterations()) : 0;
  state.counters["encoded_bytes"] = double(fixture.exact_len);
  state.counters["context_bytes"] = double(sizeof(wl_ctx_t));
}

int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  benchmark::AddCustomContext("workload", "wirelink-framing-v1");
  for (unsigned mode = 0; mode < FRAMING_MODES; ++mode)
    for (int envelope : {WL_ENVELOPE_COBS_STREAM, WL_ENVELOPE_NATIVE_PACKET, WL_ENVELOPE_BUS_LENGTH16})
      for (int integrity : {WL_INTEGRITY_NONE, WL_INTEGRITY_CRC16, WL_INTEGRITY_CRC32C})
        for (unsigned pattern : {0U, 1U, 2U})
          for (size_t bytes : {0U, 32U, 120U, 254U, 512U, 2048U}) {
            const std::string name = std::string(framing_mode_name(mode)) + "/e" + std::to_string(envelope) +
                "/i" + std::to_string(integrity) + "/p" + std::to_string(pattern) + "/b" + std::to_string(bytes);
            benchmark::RegisterBenchmark(name.c_str(), run, mode, envelope, integrity, pattern, bytes);
          }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return failed ? 1 : 0;
}
