# SPDX-License-Identifier: Apache-2.0
from concurrent.futures import ThreadPoolExecutor
from dataclasses import FrozenInstanceError
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

import pytest
from calculator_sdk import (
    AddRequest, Client, Udp, CancelledError, ClosedError, QueueFullError,
    RejectedError, RpcTimeoutError, TransportError, core_version, codegen_abi,
)


@pytest.fixture
def peer():
    executable = os.environ.get("WIRELINK_CALCULATOR_SERVER")
    if not executable:
        pytest.fail("set WIRELINK_CALCULATOR_SERVER to the built calculator_c_server")
    with subprocess.Popen([executable], stdout=subprocess.PIPE, text=True) as server:
        try:
            port = int(server.stdout.readline())
            yield ("127.0.0.1", port)
        finally:
            server.terminate()
            server.wait(timeout=5)


def test_values_and_context(peer):
    assert core_version == "0.8.0" and codegen_abi == 32
    with Client.connect(Udp(peer=peer)) as client:
        assert client.is_open and client.local_port > 0
        for i in range(100):
            response = client.add(left=i, right=42-i)
            assert response.sum == 42
        assert client.add_request(AddRequest(-10, 52)).sum == 42
        with pytest.raises(RejectedError) as rejected:
            client.add(left=2**31 - 1, right=1)
        assert rejected.value.rejection == 1
        assert rejected.value.local_error == rejected.value.transport_error == 0
    assert response.sum == 42 and not client.is_open
    client.close()
    with pytest.raises(ClosedError):
        client.add(left=0, right=0)
    with pytest.raises(FrozenInstanceError):
        response.sum = 1


def test_concurrent_calls(peer):
    with Client.connect(Udp(peer=peer)) as client:
        def work(index):
            for i in range(25):
                assert client.add(left=index, right=i, timeout=5).sum == index + i
        with ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(work, range(4)))


def test_close_pending_releases_gil():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as blackhole:
        blackhole.bind(("127.0.0.1", 0))
        blackhole.settimeout(3)
        client = Client.connect(Udp(peer=blackhole.getsockname()))
        with ThreadPoolExecutor(max_workers=3) as pool:
            pending = pool.submit(client.add, left=1, right=2, timeout=60)
            # Receipt proves the call was admitted. If add retained the GIL,
            # this thread could not receive/close until its 60-second timeout.
            assert blackhole.recv(1024)
            start = time.monotonic()
            a, b = pool.submit(client.close), pool.submit(client.close)
            a.result(timeout=3)
            b.result(timeout=3)
            with pytest.raises(CancelledError):
                pending.result(timeout=3)
            assert time.monotonic() - start < 3
        client.close()


def test_timeout_and_capacity():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as blackhole:
        blackhole.bind(("127.0.0.1", 0))
        with Client.connect(Udp(peer=blackhole.getsockname())) as client:
            with pytest.raises(RpcTimeoutError):
                client.add(left=1, right=2, timeout=0.05)
            assert client.is_open
            with ThreadPoolExecutor(max_workers=20) as pool:
                pending = [pool.submit(client.add, left=i, right=0, timeout=60) for i in range(20)]
                try:
                    deadline = time.monotonic() + 3
                    while not any(f.done() and isinstance(f.exception(), QueueFullError) for f in pending):
                        assert time.monotonic() < deadline
                        time.sleep(0.001)
                finally:
                    client.close()
                errors = [f.exception(timeout=3) for f in pending]
            assert any(isinstance(e, QueueFullError) for e in errors)
            assert all(isinstance(e, (QueueFullError, CancelledError, ClosedError)) for e in errors)


def test_open_rollback(peer):
    with Client.connect(Udp(peer=peer)) as client:
        with pytest.raises(TransportError) as failure:
            Client.connect(Udp(peer=peer, bind=("127.0.0.1", client.local_port)))
        assert failure.value.os_error != 0
        assert client.add(left=20, right=22).sum == 42


@pytest.mark.parametrize("bad", [2**31, -(2**31)-1, True, 1.5, "2"])
def test_integer_validation(bad):
    with pytest.raises((TypeError, ValueError)):
        AddRequest(bad, 0)


@pytest.mark.parametrize("timeout", [0, -1, float("nan"), float("inf"), 2147484, 10**400, True, "1"])
def test_timeout_validation(peer, timeout):
    with Client.connect(Udp(peer=peer)) as client:
        with pytest.raises((TypeError, ValueError)):
            client.add(left=1, right=2, timeout=timeout)


@pytest.mark.parametrize("peer", [("127.0.0.1", 0), ("127.0.0.1", 65536), ("127.0.0.1", True), ("a\0b", 1)])
def test_transport_validation(peer):
    with pytest.raises((ValueError, TypeError)):
        Udp(peer=peer)


def test_implicit_destruction_in_subprocess(peer):
    code = """
import sys
from calculator_sdk import Client, Udp
client = Client.connect(Udp(peer=('127.0.0.1', int(sys.argv[1]))))
assert client.add(left=20, right=22).sum == 42
"""
    subprocess.run([sys.executable, "-c", code, str(peer[1])], check=True, timeout=5)


def test_typing_marker():
    import calculator_sdk
    package = Path(calculator_sdk.__file__).parent
    assert (package / "py.typed").is_file()
    assert (package / "_native.pyi").is_file()
