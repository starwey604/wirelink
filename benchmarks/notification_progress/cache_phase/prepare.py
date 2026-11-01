# SPDX-License-Identifier: Apache-2.0
"""Prepare a phase-only cache probe in independent baseline source snapshots."""
import argparse
import difflib
import json
from pathlib import Path
import shutil
import tarfile

HERE = Path(__file__).resolve().parent


def replace(text, old, new):
    if text.count(old) != 1:
        raise ValueError('source anchor drift: '+repr(old))
    return text.replace(old, new)


def prepare(baseline, out, host_main):
    out.mkdir(parents=True, exist_ok=False)
    for name, dest in [('wirelink', 'wirelink'), ('benchmark', 'wirelink-cross-benchmark'),
                       ('astrial', 'astrial')]:
        with tarfile.open(baseline/(name+'.tar.gz')) as archive:
            archive.extractall(out/dest, filter='data')
    research = out/'research'
    research.mkdir()
    for name in ['probe.h', 'probe.c']:
        shutil.copyfile(HERE/name, research/name)
    patches = []

    def edit(path, transform):
        file = out/path
        old = file.read_text()
        new = transform(old)
        file.write_text(new)
        patches.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                           fromfile=path, tofile=path))

    def main(s):
        s = replace(s, '#include "backend.h"', '#include "backend.h"\n#include "probe.h"')
        s = replace(s, '    ub_cpu_start();\n    break;',
                    '    ub_cpu_start();\n    probe_start();\n    break;')
        s = replace(s, '  case UB_STOP:\n', '''  case 0x7f20:
    v->length = (uint32_t)probe_config((char *)v->data, sizeof(v->data), r->v[1]);
    if (!v->length) v->v[0] = 3;
    break;
  case 0x7f21:
    v->length = (uint32_t)probe_json((char *)v->data, sizeof(v->data), r->v[1]);
    if (!v->length) v->v[0] = 3;
    break;
  case UB_STOP:
    probe_stop();
''')
        s = replace(s, '  int err = ub_cpu_init();', '  probe_init();\n  int err = ub_cpu_init();')
        return s

    edit('wirelink-cross-benchmark/usb/device/main.c', main)
    edit('wirelink-cross-benchmark/usb/static_kernel/device/CMakeLists.txt',
         lambda s: s+f'\ntarget_sources(app PRIVATE "{research}/probe.c")\n'
                     f'target_include_directories(app PRIVATE "{research}")\n')
    s = host_main.read_text()
    definition = '''static int probe_mode() {
  const char *s = std::getenv("WL_CACHE_PROBE");
  check(s && (std::strcmp(s, "-1") == 0 || std::strcmp(s, "0") == 0 ||
              std::strcmp(s, "1") == 0), "WL_CACHE_PROBE must be -1, 0 or 1");
  return std::atoi(s);
}
static void probe_stats() {
  if (probe_mode() < 0) return;
  for (unsigned page = 0; page < 5; ++page) {
    auto result = control(0x7f21, page);
    emit_control("cache_probe", result);
    std::string json(reinterpret_cast<const char *>(result.data), result.length);
    if (json.find("\\\"done\\\":true") != std::string::npos) return;
  }
  throw std::runtime_error("cache probe page bound");
}
'''
    s = replace(s, 'static void output_samples(', definition+'static void output_samples(')
    anchor = '    if (mode == "boundary") {'
    s = replace(s, anchor, '''    if (probe_mode() >= 0)
      emit_control("cache_probe_config", control(0x7f20, unsigned(probe_mode())));
'''+anchor)
    for phase in ['rpc', 'idle', 'low', 'sample', 'stream']:
        anchor = f'emit_control("{phase}_cpu", control(UB_CPU_STATS));'
        s = replace(s, anchor, anchor+'\n      probe_stats();')
    (out/'host-main.cpp').write_text(s)
    patches.extend(difflib.unified_diff(host_main.read_text().splitlines(True), s.splitlines(True),
                                       fromfile=str(host_main), tofile='host-main.cpp'))
    (out/'probe.patch').write_text(''.join(patches))
    (out/'probe-inputs.json').write_text(json.dumps(dict(baseline=str(baseline),
        host_main=str(host_main), product_changes=False,
        clock_scope='No Sample or owner-loop hooks. Whole-phase counter reads only, IRQ-inclusive.',
        sample='No per-call probes; all original API samples retained. Counter state is owner-only.'), indent=2)+'\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['baseline', 'out', 'host-main']:
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    prepare(a.baseline.resolve(), a.out.resolve(), a.host_main.resolve())
