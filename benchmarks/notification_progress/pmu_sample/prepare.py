# SPDX-License-Identifier: Apache-2.0
"""Prepare a private PMU experiment from the retained product source archives."""
import argparse
import difflib
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('cache_phase_prepare', HERE.parent/'cache_phase/prepare.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


def prepare(baseline, out, host_main):
    original = {}
    with tarfile.open(baseline/'benchmark.tar.gz') as archive:
        for relative in ['usb/device/main.c', 'usb/static_kernel/device/CMakeLists.txt']:
            member = next(m for m in archive.getmembers() if m.name.removeprefix('./') == relative)
            original['wirelink-cross-benchmark/'+relative] = archive.extractfile(member).read().decode()
    base.prepare(baseline, out, host_main)
    research = out/'research'
    for name in ['probe.c', 'probe.h', 'probe_hw.h', 'calibrate.S']:
        shutil.copyfile(HERE/name, research/name)
    path = out/'wirelink-cross-benchmark/usb/device/main.c'
    s = path.read_text()
    s = base.replace(s, '''      uint32_t start = k_cycle_get_32();
      err = send_message(&tx);
      uint32_t cycles = k_cycle_get_32() - start;''', '''      bool pmu_picked = PMU_SELECTED(tx_count);
      pmu_snapshot_t pmu_before, pmu_after;
      uint32_t pmu_outer0 = 0U, pmu_outer1 = 0U;
      if (pmu_picked) PMU_READ(ERI_PERFMON_PM0, pmu_outer0);
      uint32_t start = k_cycle_get_32();
      if (pmu_picked) PMU_SNAPSHOT(pmu_before);
      err = send_message(&tx);
      if (pmu_picked) PMU_SNAPSHOT(pmu_after);
      uint32_t cycles = k_cycle_get_32() - start;
      if (pmu_picked) PMU_READ(ERI_PERFMON_PM0, pmu_outer1);
      PMU_FINISH(tx_count, start, cycles, err, pmu_picked, pmu_before, pmu_after,
                 pmu_outer1 - pmu_outer0);''')
    path.write_text(s)
    path = out/'wirelink-cross-benchmark/usb/static_kernel/device/CMakeLists.txt'
    path.write_text(path.read_text()+f'\ntarget_sources(app PRIVATE "{research}/calibrate.S")\n')
    path = out/'host-main.cpp'
    s = path.read_text().replace('WL_CACHE_PROBE', 'WL_PMU_PROBE').replace('cache_probe', 'pmu_probe')
    s = base.replace(s, '''    if (probe_mode() >= 0)
      emit_control("pmu_probe_config", control(0x7f20, unsigned(probe_mode())));''', '''    if (probe_mode() >= 0) {
      auto calibration = control(0x7f20, unsigned(probe_mode()));
      emit_control("pmu_probe_config", calibration);
      std::string json(reinterpret_cast<const char *>(calibration.data), calibration.length);
      check(json.find("\\\"calibrated\\\":true") != std::string::npos, "PMU calibration failed");
    }''')
    path.write_text(s)
    original['host-main.cpp'] = host_main.read_text()
    patches = []
    for relative, old in original.items():
        patches.extend(difflib.unified_diff(old.splitlines(True), (out/relative).read_text().splitlines(True),
                                           fromfile=relative, tofile=relative))
    (out/'probe.patch').write_text(''.join(patches))
    (out/'probe-inputs.json').write_text(json.dumps(dict(baseline=str(baseline), host_main=str(host_main),
        product_changes=False, mode0='Phase PMU/cache counters and Sample attempt accounting, no API snapshots',
        mode1='Same image; 1/64 rotating sparse PMU snapshots inside original CCOUNT elapsed scope',
        calibration='Counter cycles versus CCOUNT, shallow/deep CALL8 and division, empty snapshot bracket; pre-USB IRQ-locked only',
        clock_scope='Inner event snapshots exclude timestamp helpers; outer window counts include them. Selected timings include snapshot overhead; no subtraction into exclusive cost. Snapshot macros add no CALL8 frames.',
        phase_scope='All contexts and control boundary traffic; streams also include preparation/drain'), indent=2)+'\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['baseline', 'out', 'host-main']:
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    prepare(a.baseline.resolve(), a.out.resolve(), a.host_main.resolve())
