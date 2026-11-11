# SPDX-License-Identifier: Apache-2.0
"""Window CPU normalized by concurrent goodput; not per-frame tracing."""
PRIMARY = 'mcu_stream1_active_us_per_frame'


def stream_costs(stream, observer):
    rate = (stream['h2d_bytes_s'] + stream['d2h_bytes_s']) / stream['size']
    direction = stream['direction']
    values = {f'host_stream{direction}_cpu_us_per_frame':
              stream['host_window_cpu_ns'] / stream['actual_host_window_ns'] * 1e6 / rate}
    if observer:
        cpu = stream['device_cpu']
        values[f'mcu_stream{direction}_active_us_per_frame'] = (
            cpu['active_cycles'] / cpu['wall_cycles'] * 1e6 / rate)
    return values
