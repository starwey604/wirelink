# SPDX-License-Identifier: Apache-2.0
"""Byte accounting for the frozen 32B/5s managed-RPC USB screen."""
import json


def require(condition, message):
    if not condition:
        raise ValueError(message)


def varuint_size(value):
    return max(1, (value.bit_length() + 6) // 7)


def control_request_size(arguments):
    # Frozen managed RPC: native prefix4 + session8 + operation3. Operations
    # exceed8191 after1000 warmup/10000 RPC and stay below1048576 in this screen.
    # All six tagged uint32 fields are present, including zero arguments. The
    # command ID starts at1e9 and remains five varuint bytes throughout.
    return 15 + sum(1 + varuint_size(v) for v in [1000000000, *arguments])


def control_response_size(row):
    # Prefix/metadata15, request_id6, status2, bytes-key1, varlength and raw JSON.
    payload = json.dumps(row['device'], separators=(',', ':')).encode()
    return 24 + varuint_size(len(payload)) + len(payload)


def successful_progress(begin, end, stream, device, cpu):
    """Solve completed USB byte ledgers for successful Progress frame counts.

    Scope is start+1s preparation+5s window+drain+STOP/CPU export, NOT the CPU
    window alone. Counts exclude the single host DRAIN and device END markers.
    No failed admission attempts/service/wake counts can be recovered this way.
    """
    require(stream['size'] == 32 and stream['window_ns'] == 5000000000,
            'only registered 32B/5s screen supported')
    requests = (control_request_size([2, 5, stream['direction'], 32, 5000])
                + control_request_size([3, stream['sent'], stream['received'], 0, 0])
                + control_request_size([7, 0, 0, 0, 0]))
    responses = 25 + control_response_size(device) + control_response_size(cpu)
    residuals = dict(
        host=end['usb_tx'] - begin['usb_tx'] - requests - stream['sent'] * 43 - 23,
        device=end['usb_rx'] - begin['usb_rx'] - responses - stream['received'] * 43 - 23)
    require(all(n >= 0 and n % 23 == 0 for n in residuals.values()),
            'USB bytes do not close into whole Progress frames')
    counts = {k: v // 23 for k, v in residuals.items()}
    return dict(normal_frames=counts, control_request_bytes=requests,
                control_response_bytes=responses,
                host_chunks_per_normal_progress=stream['received'] / counts['host']
                if counts['host'] else None,
                device_chunks_per_normal_progress=stream['sent'] / counts['device']
                if counts['device'] else None)
