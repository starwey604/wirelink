# SPDX-License-Identifier: Apache-2.0
import asyncio
import gc
import socket
import threading
import weakref

import pytest
from calculator_sdk import (
    AddRequest, AsyncClient, Udp, CancelledError, ClosedError,
    QueueFullError, RejectedError, RpcTimeoutError, binding_api,
)
from test_client import peer  # Reuse the independent C service fixture.


def run(coroutine):
    async def guarded():
        return await asyncio.wait_for(coroutine, timeout=15)
    return asyncio.run(guarded(), debug=True)


def test_async_values_concurrency_and_context(peer):
    async def exercise():
        assert binding_api == 2
        async with AsyncClient.connect(Udp(peer=peer)) as client:
            assert client.is_open and client.local_port > 0
            for _ in range(25):
                responses = await asyncio.gather(*(
                    client.add(left=i, right=42-i, timeout=5) for i in range(4)
                ))
                assert [response.sum for response in responses] == [42] * 4
            saved = await client.add_request(AddRequest(-10, 52))
            with pytest.raises(RejectedError) as failure:
                await client.add(left=2**31 - 1, right=1)
            assert failure.value.rejection == 1
            with pytest.raises(TypeError):
                await client.add_request((1, 2))
            with pytest.raises(ValueError):
                await client.add(left=1, right=2, timeout=0)
        assert saved.sum == 42 and not client.is_open
        assert not client._worker.is_alive()
        await client.close()
        with pytest.raises(ClosedError):
            await client.add(left=1, right=2)
    run(exercise())


def test_cancel_one_task_and_close_others():
    async def exercise(address, blackhole):
        client = AsyncClient.connect(Udp(peer=address))
        try:
            first = asyncio.create_task(client.add(left=1, right=2, timeout=60))
            # Receipt proves admission and that the event loop remains available.
            assert await asyncio.get_running_loop().sock_recv(blackhole, 1024)
            second = asyncio.create_task(client.add(left=20, right=22, timeout=60))
            await asyncio.sleep(0)
            first.cancel()
            with pytest.raises(asyncio.CancelledError):
                await first
            assert not second.done() and client.is_open
            close_a = asyncio.create_task(client.close())
            close_b = asyncio.create_task(client.close())
            await asyncio.gather(close_a, close_b)
            with pytest.raises(CancelledError):
                await second
            assert not client._pending and not client._worker.is_alive()
        finally:
            await client.close()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as blackhole:
        blackhole.bind(("127.0.0.1", 0))
        blackhole.setblocking(False)
        run(exercise(blackhole.getsockname(), blackhole))


def test_timeout_external_timeout_and_capacity():
    async def exercise(address):
        async with AsyncClient.connect(Udp(peer=address)) as client:
            with pytest.raises(RpcTimeoutError):
                await client.add(left=1, right=2, timeout=0.02)
            with pytest.raises(asyncio.TimeoutError):
                await asyncio.wait_for(client.add(left=1, right=2, timeout=60), 0.02)
            assert client.is_open
            calls = [asyncio.create_task(client.add(left=i, right=0, timeout=60))
                     for i in range(24)]
            done, _ = await asyncio.wait(calls, return_when=asyncio.FIRST_COMPLETED)
            assert done and all(isinstance(call.exception(), QueueFullError) for call in done)
            # Closing must settle even the requests whose completions have not
            # yet crossed from the native owner into the event loop.
            await client.close()
            results = await asyncio.gather(*calls, return_exceptions=True)
            assert any(isinstance(result, CancelledError) for result in results)
            assert all(isinstance(result, (CancelledError, QueueFullError)) for result in results)
            assert not client._pending
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as blackhole:
        blackhole.bind(("127.0.0.1", 0))
        run(exercise(blackhole.getsockname()))


def test_cancel_close_waiter_does_not_interrupt_cleanup():
    async def exercise(address, blackhole):
        client = AsyncClient.connect(Udp(peer=address))
        pending = asyncio.create_task(client.add(left=1, right=2, timeout=60))
        assert await asyncio.get_running_loop().sock_recv(blackhole, 1024)
        closing = asyncio.create_task(client.close())
        await asyncio.sleep(0)
        closing.cancel()
        await asyncio.gather(closing, return_exceptions=True)
        await client.close()
        with pytest.raises(CancelledError):
            await pending
        assert not client._worker.is_alive() and not client._pending
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as blackhole:
        blackhole.bind(("127.0.0.1", 0))
        blackhole.setblocking(False)
        run(exercise(blackhole.getsockname(), blackhole))


def test_completion_thread_is_bounded_and_collectable():
    async def exercise(address):
        before = {thread.ident for thread in threading.enumerate()}
        client = AsyncClient.connect(Udp(peer=address))
        worker = client._worker
        calls = [asyncio.create_task(client.add(left=i, right=0, timeout=60)) for i in range(8)]
        await asyncio.sleep(0)
        created = [thread for thread in threading.enumerate() if thread.ident not in before]
        assert created == [worker]  # No Python thread per RPC.
        await client.close()
        await asyncio.gather(*calls, return_exceptions=True)
        assert not worker.is_alive()
        # An idle forgotten connection must not be kept alive by its pump.
        other = AsyncClient.connect(Udp(peer=address))
        worker = other._worker
        reference = weakref.ref(other)
        del other
        gc.collect()
        await asyncio.to_thread(worker.join, 2)
        assert reference() is None and not worker.is_alive()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as blackhole:
        blackhole.bind(("127.0.0.1", 0))
        run(exercise(blackhole.getsockname()))


def test_async_client_rejects_wrong_loop_and_requires_running_loop():
    with pytest.raises(RuntimeError, match="running event loop"):
        AsyncClient.connect(Udp(peer=("127.0.0.1", 1)))

    async def exercise():
        client = AsyncClient.connect(Udp(peer=("127.0.0.1", 1)))
        async def wrong_loop():
            with pytest.raises(RuntimeError, match="different event loop"):
                await client.add(left=1, right=2)
            with pytest.raises(RuntimeError, match="different event loop"):
                await client.close()
        try:
            await asyncio.to_thread(asyncio.run, wrong_loop())
        finally:
            await client.close()
    run(exercise())


def test_fast_completion_and_cancellation_races(peer):
    async def exercise():
        async with AsyncClient.connect(Udp(peer=peer)) as client:
            for _ in range(100):
                task = asyncio.create_task(client.add(left=20, right=22, timeout=3))
                await asyncio.sleep(0)
                task.cancel()
                result = (await asyncio.gather(task, return_exceptions=True))[0]
                assert isinstance(result, asyncio.CancelledError) or result.sum == 42
                # Give native cancelled calls time to release admission before
                # starting another bounded batch; pending slots include delivery.
                while client._pending:
                    await asyncio.sleep(0.001)
            assert (await client.add(left=20, right=22)).sum == 42
    run(exercise())


def test_close_after_default_executor_shutdown():
    async def exercise(address, blackhole):
        client = AsyncClient.connect(Udp(peer=address))
        pending = asyncio.create_task(client.add(left=1, right=2, timeout=60))
        assert await asyncio.get_running_loop().sock_recv(blackhole, 1024)
        await asyncio.get_running_loop().shutdown_default_executor()
        await client.close()
        with pytest.raises(CancelledError):
            await pending
        assert not client._worker.is_alive()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as blackhole:
        blackhole.bind(("127.0.0.1", 0))
        blackhole.setblocking(False)
        run(exercise(blackhole.getsockname(), blackhole))
