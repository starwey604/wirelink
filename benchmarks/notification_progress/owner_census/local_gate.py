# SPDX-License-Identifier: Apache-2.0
"""Predeclared representativeness gate, not a product acceptance criterion."""
from counter_rows import require


def contrast(a, b, state_record):
    def rate(metrics, direction, key):
        return next(s[key] for s in metrics['streams'] if s['direction'] == direction)
    ratios = {}
    for name, direction, key in [('h2d', 1, 'h2d_bytes_s'), ('bidir_d2h', 3, 'd2h_bytes_s')]:
        x, y = rate(a, direction, key), rate(b, direction, key)
        require(x > 0 and y > 0, 'positive control rates required')
        ratios[name] = y / x
    return dict(valid=state_record['valid'], state=state_record['state'], ratios=ratios)


def reference_ready(reference):
    if not reference['valid']:
        return dict(pass_gate=False, reason='reference pair state/gap invalid')
    if not all(v < .95 for v in reference['ratios'].values()):
        return dict(pass_gate=False, reason='reference does not show both target regressions')
    return dict(pass_gate=True, reason='reference shows both target regressions')


def representative(reference, local):
    ready = reference_ready(reference)
    if not ready['pass_gate']:
        return ready
    if not local['valid']:
        return dict(pass_gate=False, reason='local pilot state/gap invalid')
    if reference['state'] != local['state']:
        return dict(pass_gate=False, reason='reference/local graphical or power state differs')
    if not all(v < .95 for v in local['ratios'].values()):
        return dict(pass_gate=False, reason='five-counter pilot does not preserve both target regressions')
    return dict(pass_gate=True, reason='five-counter pilot preserves both target regression directions')
