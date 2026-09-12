# SPDX-License-Identifier: Apache-2.0
import dataclasses
import os
from pathlib import Path
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import pytest
import device_sdk as sdk

ROOT = Path(__file__).resolve().parents[3]
SERVER = Path(os.environ.get("WIRELINK_DEVICE_SERVER", ROOT / "build/device-sdk/tests/device_c_server"))
CALCULATOR_SERVER = Path(os.environ.get("WIRELINK_CALCULATOR_SERVER", ROOT / "build/bindings-sdk/tests/calculator_c_server"))


@pytest.fixture
def client():
    with subprocess.Popen([str(SERVER)], stdout=subprocess.PIPE, text=True) as server:
        try:
            port = int(server.stdout.readline())
            with sdk.Client.connect(sdk.Udp(peer=("127.0.0.1", port))) as connection:
                yield connection
        finally:
            server.terminate()
            server.wait(timeout=5)


def settings(**kwargs):
    return sdk.Settings(enabled=True, token=b"\0\x80\xff", gains=(1.0, -2.5, 3.25), **kwargs)


def test_c_service_returns_owned_values_and_unknown_enum(client):
    result = client.get_info()
    assert result.name == "device\0x"
    assert result.settings.mode == 12345
    assert isinstance(result.settings.mode, sdk.Mode)
    assert result.settings.mode.name is None
    assert result.settings.label is None
    assert result.settings.label_or_default == "设备"
    assert result.settings.floor_or_default == -(2**63)
    assert result.settings.ceiling_or_default == 2**64 - 1
    client.close()
    assert result.settings.token == b"\0\x80\xff"
    with pytest.raises(dataclasses.FrozenInstanceError):
        result.settings.enabled = False
    with pytest.raises(sdk.ClosedError):
        client.get_info_request(sdk.InfoRequest())


def test_presence_defaults_and_nested_round_trip(client):
    original = settings(class_="a\0b", timeout_=65535)
    backup = settings(label="", counters=(0, 2**64 - 1), mode=12345)
    for i in range(100):
        result = client.configure_request(sdk.ConfigureRequest(original, opaque=b"", backup=backup))
        assert result.count == i + 1
        assert result.settings == original
        assert result.backup == backup
        assert result.opaque == b""
    client.close()
    assert result.settings.label is None
    assert result.backup.label == ""
    assert result.backup.label_or_default == ""
    assert result.backup.counters == (0, 2**64 - 1)
    assert result.settings.mode_or_default == sdk.Mode.MODE_ACTIVE


def test_c_rejection_retains_domains_and_connection_recovers(client):
    bad = dataclasses.replace(settings(), token=b"\xff")
    with pytest.raises(sdk.RejectedError) as caught:
        client.configure(settings=bad)
    assert caught.value.rejection == 7
    assert caught.value.codec_error == 0
    assert caught.value.local_error == 0
    assert client.configure(settings=settings()).count == 1


def test_concurrent_generated_calls(client):
    def work(_):
        return [client.configure(settings=settings()).count for _ in range(30)]
    with ThreadPoolExecutor(max_workers=4) as pool:
        counts = [number for result in pool.map(work, range(4)) for number in result]
    assert sorted(counts) == list(range(1, 121))


def test_utf8_byte_bound_nul_and_snapshot_normalization(client):
    token = bytearray(b"\0abc")
    gains = [1, 2, 3]
    value = sdk.Settings(enabled=True, token=token, gains=gains, label="设备设备")
    token[0] = 255
    gains[0] = 99
    assert value.token == b"\0abc" and value.gains == (1.0, 2.0, 3.0)
    assert client.configure(settings=value).settings == value
    with pytest.raises(ValueError, match="UTF-8 bytes"):
        settings(label="设备设备x")
    with pytest.raises(ValueError):
        settings(label="\ud800")


@pytest.mark.parametrize("kwargs,exception", [
    ({"label": b"abc"}, TypeError),
    ({"mode": True}, TypeError),
    ({"mode": 2**31}, ValueError),
    ({"counters": (1,)}, ValueError),
    ({"counters": (1, 2**64)}, ValueError),
    ({"counters": (True, 0)}, TypeError),
    ({"floor": -(2**63) - 1}, ValueError),
    ({"ceiling": 2**64}, ValueError),
    ({"class_": "123456789"}, ValueError),
    ({"timeout_": 65536}, ValueError),
])
def test_optional_field_validation(kwargs, exception):
    with pytest.raises(exception):
        settings(**kwargs)


@pytest.mark.parametrize("field,value,exception", [
    ("enabled", 1, TypeError),
    ("token", b"012345678", ValueError),
    ("token", 8, TypeError),
    ("gains", (1, 2), ValueError),
    ("gains", (True, 2, 3), TypeError),
    ("gains", (1e100, 2, 3), ValueError),
    ("gains", (10**400, 2, 3), ValueError),
])
def test_required_field_validation(field, value, exception):
    kwargs = dict(enabled=True, token=b"", gains=(1, 2, 3))
    kwargs[field] = value
    with pytest.raises(exception):
        sdk.Settings(**kwargs)


def test_all_numeric_types_and_empty_request():
    value = sdk.Numbers(
        i8=-128, u8=255, i16=-32768, u16=65535, i32=-(2**31), u32=2**32 - 1,
        i64=-(2**63), u64=2**64 - 1, f32=2**32 - 1, f64=2**64 - 1,
        single=-0.0, double_=float("inf"), doubles=(1.5, -0.0), words=(0, 2**32 - 1))
    assert value.u64 == 2**64 - 1 and value.words[1] == 2**32 - 1
    assert value.offset_or_default == -7
    assert value.minimum_or_default == -(2**31)
    assert value.flag_or_default is True
    assert value.text_or_default == 'a"@NAME@'
    for field in ("i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64"):
        with pytest.raises((TypeError, ValueError)):
            dataclasses.replace(value, **{field: 2**100})
        with pytest.raises(TypeError):
            dataclasses.replace(value, **{field: True})
    assert sdk.InfoRequest() == sdk.InfoRequest()
    with pytest.raises(TypeError):
        sdk.ConfigureRequest(settings={})
    with pytest.raises(TypeError):
        sdk.ConfigureRequest(settings=settings(), backup={})


def test_unknown_enum_does_not_grow_a_global_cache():
    size = len(sdk.Mode._value2member_map_)
    for number in range(1000, 2000):
        assert int(sdk.Mode(number)) == number
    assert len(sdk.Mode._value2member_map_) == size


@pytest.mark.parametrize("order", ["calculator_sdk,device_sdk", "device_sdk,calculator_sdk"])
def test_two_independent_sdks_in_one_interpreter(order):
    script = """
import importlib
import subprocess
import sys
for name in sys.argv[1].split(','):
    importlib.import_module(name)
import calculator_sdk as calc
import device_sdk as device
assert calc._native.Error is not device._native.Error
with subprocess.Popen([sys.argv[2]], stdout=subprocess.PIPE, text=True) as c_server, \
     subprocess.Popen([sys.argv[3]], stdout=subprocess.PIPE, text=True) as d_server:
    try:
        cp = int(c_server.stdout.readline())
        dp = int(d_server.stdout.readline())
        with calc.Client.connect(calc.Udp(peer=('127.0.0.1', cp))) as c, \
             device.Client.connect(device.Udp(peer=('127.0.0.1', dp))) as d:
            assert c.add(left=20, right=22).sum == 42
            assert d.get_info().settings.mode == 12345
            try:
                c.add(left=2**31 - 1, right=1)
            except calc.RejectedError as error:
                assert error.rejection == 1
            else:
                raise AssertionError('expected calculator rejection')
            try:
                d.configure(settings=device.Settings(enabled=True, token=b'\\xff', gains=(1, 2, 3)))
            except device.RejectedError as error:
                assert error.rejection == 7
            else:
                raise AssertionError('expected device rejection')
    finally:
        c_server.terminate()
        d_server.terminate()
        c_server.wait(timeout=5)
        d_server.wait(timeout=5)
"""
    subprocess.run([sys.executable, "-I", "-c", script, order, str(CALCULATOR_SERVER), str(SERVER)],
                   check=True, timeout=20)
