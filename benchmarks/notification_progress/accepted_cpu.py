# SPDX-License-Identifier: Apache-2.0
"""Read selected retained B-current raw CPU counters; never rewrite old evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics


def need(value, reason):
    if not value:
        raise ValueError(reason)


def checked(root, relative, index):
    path = root / relative
    record = index['files'][relative]
    data = path.read_bytes()
    need(len(data) == record['size'] and hashlib.sha256(data).hexdigest() == record['sha256'],
         'retained evidence mismatch: ' + relative)
    return data


def analyse(baseline):
    baseline = json.loads(Path(baseline).read_text())
    index_path = Path(baseline['accepted_evidence_index'])
    raw_index = index_path.read_bytes()
    need(hashlib.sha256(raw_index).hexdigest() == baseline['accepted_evidence_sha256'], 'accepted index hash')
    index = json.loads(raw_index)
    root = index_path.parent
    prefix = 'repair-environment-rebase/'
    registration = json.loads(checked(root, prefix+'final-registration.json', index))
    need(registration['root_head'] == baseline['measured_code_head'], 'accepted source boundary')
    need(hashlib.sha256((root/prefix/'final-registration.json').read_bytes()).hexdigest() ==
         baseline['registration_sha256'], 'accepted registration')
    rows = []
    fields = ['active_cycles', 'owner_cycles_inclusive_irq', 'usbd_cycles_inclusive_irq',
              'udc_cycles_inclusive_irq', 'library_worker_cycles_inclusive_irq',
              'usb_irq_span_cycles_inclusive']
    for repetition in [1, 2, 3]:
        capture = f'final-r{repetition}-B-o1'
        relative = prefix+'captures/'+capture+'/'
        manifest = json.loads(checked(root, relative+'manifest.json', index))
        need(manifest['full_valid'] and manifest['complete'] and not manifest['qualification_only'], 'accepted full capture')
        need(manifest['registration_sha256'] == baseline['registration_sha256'], 'capture registration')
        data = [json.loads(line) for line in checked(root, relative+'host.jsonl', index).decode().splitlines()]
        def one(name):
            matches = [row for row in data if row['event'] == name]
            need(len(matches) == 1, 'missing/duplicate event: '+name)
            return matches[0]
        need(one('rpc_device')['device']['rpc_calls'] == 10000 and
             one('sample_device')['device']['sent'] == 1000, 'phase denominators')
        for phase, count in [('rpc', 10000), ('sample', 1000)]:
            cpu = one(phase+'_cpu')['device']
            need(cpu['available'] and cpu['validated'] and cpu['frequency_hz'] == 240000000, 'CPU observer')
            row = dict(capture=capture, phase=phase, units=count, usb_irqs=cpu['usb_irqs'])
            row['us_per_unit'] = {field: cpu[field]/240/count for field in fields}
            rows.append(row)
    summaries = {}
    for phase in ['rpc', 'sample']:
        group = [row for row in rows if row['phase'] == phase]
        summaries[phase] = dict(median_us_per_unit={field: statistics.median(row['us_per_unit'][field]
            for row in group) for field in fields}, usb_irqs=[row['usb_irqs'] for row in group])
    return dict(format='wirelink-accepted-cpu-components-v1',
        scope='Selected B-current raw bytes checked against accepted index; not a full historical index/build-artifact replay.',
        accepted_index_sha256=baseline['accepted_evidence_sha256'],
        additivity='Inclusive thread/IRQ observations overlap or have different accounting scopes; do not sum or subtract them into exclusive framework cost.',
        hardware_notify_counts_available=False, rows=rows, summaries=summaries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    need(not args.out.exists(), 'refusing to overwrite a research result')
    result = analyse(args.baseline)
    args.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result['summaries'], indent=2))


if __name__ == '__main__':
    main()
