# SPDX-License-Identifier: Apache-2.0
"""Generated synchronous Wirelink SDK. Message data outlives the connection."""
from __future__ import annotations

import builtins as _builtins
from dataclasses import dataclass as _dataclass
from types import TracebackType as _TracebackType
from typing import Any as _Any
from . import _native, _runtime
from ._runtime import (Udp, WirelinkError, ClosedError, RpcTimeoutError, CancelledError,
    RejectedError, QueueFullError, TransportError, CodecError, InvalidArgumentError)

__version__ = "0.1.0.dev1"
core_version: str = _native.core_version
codegen_abi: int = _native.codegen_abi
binding_api: int = _native.binding_api


@_dataclass(frozen=True, slots=True)
class AddRequest:
    left: int
    right: int

    def __post_init__(self) -> None:
        object.__setattr__(self, "left", _runtime.integer(self.left, "left", -(2**31), 2**31 - 1))
        object.__setattr__(self, "right", _runtime.integer(self.right, "right", -(2**31), 2**31 - 1))

    def _to_native(self) -> tuple[_Any, ...]:
        return (self.left, self.right, )

    @_builtins.classmethod
    def _from_native(cls, value: tuple[_Any, ...]) -> AddRequest:
        return cls(
            left=value[0],
            right=value[1],
        )


@_dataclass(frozen=True, slots=True)
class AddResponse:
    sum: int

    def __post_init__(self) -> None:
        object.__setattr__(self, "sum", _runtime.integer(self.sum, "sum", -(2**31), 2**31 - 1))

    def _to_native(self) -> tuple[_Any, ...]:
        return (self.sum, )

    @_builtins.classmethod
    def _from_native(cls, value: tuple[_Any, ...]) -> AddResponse:
        return cls(
            sum=value[0],
        )


class Client:
    """Owns a UDP connection. Calls and close may run from multiple threads.

    Connecting starts local I/O without a peer handshake. Responses own all data.
    """

    def __init__(self, native_client: _native.Client) -> None:
        if not isinstance(native_client, _native.Client):
            raise TypeError("create a Client using Client.connect(Udp(...))")
        self._client = native_client

    @_builtins.classmethod
    def connect(cls, transport: Udp) -> Client:
        if not isinstance(transport, Udp):
            raise TypeError("transport must be Udp")
        native, error = _native.connect(*transport.peer, *transport.bind)
        if error is not None:
            _runtime._raise(error)
        assert native is not None
        return cls(native)

    @_builtins.property
    def is_open(self) -> bool:
        return self._client.is_open

    @_builtins.property
    def local_port(self) -> int:
        return self._client.local_port

    def close(self) -> None:
        self._client.close()

    def __enter__(self) -> Client:
        return self

    def __exit__(self, kind: type[BaseException] | None, error: BaseException | None,
                 traceback: _TracebackType | None) -> None:
        self.close()

    def add(self, *, left: int, right: int, timeout: float = 1.0) -> AddResponse:
        return self.add_request(AddRequest(
            left=left,
            right=right,
        ), timeout=timeout)

    def add_request(self, request: AddRequest, *, timeout: float = 1.0) -> AddResponse:
        if not isinstance(request, AddRequest):
            raise TypeError("request must be AddRequest")
        value, error = self._client.add(request._to_native(), _runtime.timeout_ms(timeout))
        if error is not None:
            _runtime._raise(error)
        assert value is not None
        return AddResponse._from_native(value)


__all__ = [
    "Client", "Udp", "WirelinkError", "ClosedError", "RpcTimeoutError", "CancelledError",
    "RejectedError", "QueueFullError", "TransportError", "CodecError", "InvalidArgumentError",
    "core_version", "codegen_abi", "binding_api", "__version__",
    "AddRequest",
    "AddResponse",
]
