# SPDX-License-Identifier: Apache-2.0
"""Read-only Linux/Hyprland sidecar; never changes power or session state."""
import argparse
import datetime
import json
import subprocess
import time
from pathlib import Path


def read(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def command(args):
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=2)
        return result.returncode, result.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return None, ''


def lock_state(hint, locker_running):
    # Some direct Hyprlock launches do not synchronize logind's LockedHint.
    if hint == 'yes' or locker_running is True:
        return True
    if hint == 'no' and locker_running is False:
        return False
    return None


def snapshot(session, usb_path):
    begin = time.monotonic_ns()
    rc, output = command(['loginctl', 'show-session', session,
                          '-p', 'LockedHint', '-p', 'IdleHint', '-p', 'Active'])
    props = dict(line.split('=', 1) for line in output.splitlines() if '=' in line)
    if rc != 0:
        props = {}
    rc, _ = command(['pgrep', '-x', 'hyprlock'])
    locker = True if rc == 0 else False if rc == 1 else None
    rc, output = command(['hyprctl', '-j', 'monitors', 'all'])
    try:
        monitors = [{k: m.get(k) for k in ('name', 'dpmsStatus', 'disabled')}
                    for m in json.loads(output)] if rc == 0 else None
    except (ValueError, TypeError, AttributeError):
        monitors = None
    cpus = {}
    for policy in sorted(Path('/sys/devices/system/cpu/cpufreq').glob('policy*')):
        cpus[policy.name] = {key: read(policy/key) for key in (
            'scaling_cur_freq', 'scaling_governor', 'energy_performance_preference',
            'scaling_min_freq', 'scaling_max_freq')}
    thermal = {}
    for zone in sorted(Path('/sys/class/thermal').glob('thermal_zone*')):
        thermal[zone.name] = {'type': read(zone/'type'), 'temp_mC': read(zone/'temp')}
    throttle = {}
    for cpu in sorted(Path('/sys/devices/system/cpu').glob('cpu[0-9]*')):
        root = cpu/'thermal_throttle'
        if root.exists():
            throttle[cpu.name] = {key: read(root/key) for key in (
                'core_throttle_count', 'package_throttle_count')}
    usb = {}
    # Record the physical device, upstream hubs, and its PCI controller.
    device = Path('/sys/bus/usb/devices')/usb_path
    if device.exists():
        for parent in [device.resolve(), *device.resolve().parents]:
            if (parent/'power/runtime_status').exists():
                usb[parent.name] = {key: read(parent/'power'/key) for key in (
                    'control', 'runtime_status', 'runtime_suspended_time',
                    'runtime_active_time', 'autosuspend_delay_ms')}
    power = {p.name: {key: read(p/key) for key in ('type', 'online', 'status')}
             for p in sorted(Path('/sys/class/power_supply').glob('*'))}
    return dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                monotonic_ns=begin,
                boottime_ns=time.clock_gettime_ns(time.CLOCK_BOOTTIME),
                sample_duration_ns=time.monotonic_ns()-begin,
                boot_id=read('/proc/sys/kernel/random/boot_id'), session=props,
                locker_running=locker,
                locked=lock_state(props.get('LockedHint'), locker),
                monitors=monitors, cpu_policies=cpus, thermal=thermal,
                throttle=throttle, usb=usb, power=power,
                platform_profile=read('/sys/firmware/acpi/platform_profile'),
                proc_stat=read('/proc/stat'), pressure_cpu=read('/proc/pressure/cpu'))


def window_state(rows, start_ns, end_ns, expected, max_gap_ns=2_000_000_000):
    """Require bracketing samples, known state, one boot, no suspend or gaps."""
    before = [r for r in rows if r['monotonic_ns'] <= start_ns]
    after = [r for r in rows if r['monotonic_ns'] >= end_ns]
    if not before or not after:
        raise ValueError('host state does not bracket measured window')
    first, last = before[-1], after[0]
    selected = [r for r in rows
                if first['monotonic_ns'] <= r['monotonic_ns'] <= last['monotonic_ns']]
    if any(r['locked'] is not expected for r in selected):
        raise ValueError('unknown or changed lock state')
    if len({r['boot_id'] for r in selected}) != 1 or first['boot_id'] is None:
        raise ValueError('unknown or changed boot')
    for a, b in zip(selected, selected[1:]):
        elapsed = b['monotonic_ns']-a['monotonic_ns']
        sleep = (b['boottime_ns']-a['boottime_ns'])-elapsed
        if elapsed <= 0 or elapsed > max_gap_ns or abs(sleep) > 100_000_000:
            raise ValueError('host sampling gap or suspend')
    return selected


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', required=True)
    parser.add_argument('--usb-path', default='3-2.4')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=900)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error('seconds must be positive')
    with args.out.open('x') as stream:
        deadline = time.monotonic()+args.seconds
        while time.monotonic() < deadline:
            start = time.monotonic()
            stream.write(json.dumps(snapshot(args.session, args.usb_path))+'\n')
            stream.flush()
            time.sleep(max(0, 1-(time.monotonic()-start)))
