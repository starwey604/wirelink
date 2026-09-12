# SPDX-License-Identifier: Apache-2.0
"""Synchronous calculator reference SDK over Wirelink UDP."""
from __future__ import annotations

from dataclasses import dataclass
import math
from types import TracebackType
from typing import NoReturn

from . import _native

__version__ = "0.1.0.dev1"
core_version: str = _native.core_version
codegen_abi: int = _native.codegen_abi
binding_api: int = _native.binding_api


class WirelinkError(Exception):
    """A native failure, retaining the separate RPC diagnostic domains."""

    def __init__(self, error: _native.Error) -> None:
        self.kind = error.kind
        self.status = error.status
        self.rejection = error.rejection
        self.local_error = error.local_error
        self.transport_error = error.transport_error
        self.runtime_error = error.runtime_error
        self.codec_error = error.codec_error
        self.os_error = error.os_error
        self.os_message = error.os_message
        super().__init__(
            f"{self.kind}: status={self.status}, rejection={self.rejection}, "
            f"local={self.local_error}, transport={self.transport_error}, "
            f"runtime={self.runtime_error}, codec={self.codec_error}, os={self.os_error}"
        )


class ClosedError(WirelinkError):
    """The connection is closed."""


class RpcTimeoutError(WirelinkError, TimeoutError):
    """The call's deadline expired, including time spent in the owner queue."""


class CancelledError(WirelinkError):
    """An admitted call was cancelled by connection shutdown."""


class RejectedError(WirelinkError):
    """The service rejected the request; rejection contains its business code."""


class QueueFullError(WirelinkError):
    """The bounded call capacity is exhausted."""


class TransportError(WirelinkError):
    """A local transport operation or link transaction failed."""


class CodecError(WirelinkError):
    """The payload could not be encoded or decoded."""


class InvalidArgumentError(WirelinkError, ValueError):
    """Native configuration validation failed."""


def _raise(error: _native.Error) -> NoReturn:
    exception = {
        "closed": ClosedError,
        "timed_out": RpcTimeoutError,
        "cancelled": CancelledError,
        "rejected": RejectedError,
        "queue_full": QueueFullError,
        "transport": TransportError,
        "codec": CodecError,
        "invalid_argument": InvalidArgumentError,
    }.get(error.kind, WirelinkError)
    raise exception(error)


def _integer(value: int, name: str, minimum: int, maximum: int) -> None:
    if not isinstance(value, int) or isinstance(value, bool):
        raise TypeError(f"{name} must be an integer")
    if not minimum <= value <= maximum:
        raise ValueError(f"{name} must be in [{minimum}, {maximum}]")


@dataclass(frozen=True, slots=True)
class Udp:
    peer: tuple[str, int]
    bind: tuple[str, int] = ("127.0.0.1", 0)

    def __post_init__(self) -> None:
        for name, address, minimum in (("peer", self.peer, 1), ("bind", self.bind, 0)):
            if not isinstance(address, tuple) or len(address) != 2:
                raise TypeError(f"{name} must be an (IP address, port) tuple")
            if not isinstance(address[0], str) or not address[0] or "\0" in address[0]:
                raise ValueError(f"{name} IP address must be a nonempty string without NUL")
            _integer(address[1], f"{name} port", minimum, 65535)


@dataclass(frozen=True, slots=True)
class AddRequest:
    left: int
    right: int

    def __post_init__(self) -> None:
        _integer(self.left, "left", -(2**31), 2**31 - 1)
        _integer(self.right, "right", -(2**31), 2**31 - 1)


@dataclass(frozen=True, slots=True)
class AddResponse:
    sum: int


class Client:
    """An owning connection. Calls and close are safe from multiple threads.

    connect starts local I/O; it does not perform a peer handshake. A received
    AddResponse is independent of the connection and remains valid after close.
    """

    def __init__(self, native_client: _native.Client) -> None:
        if not isinstance(native_client, _native.Client):
            raise TypeError("create a Client using Client.connect(Udp(...))")
        self._client = native_client

    @classmethod
    def connect(cls, transport: Udp) -> Client:
        if not isinstance(transport, Udp):
            raise TypeError("transport must be Udp")
        native, error = _native.connect(*transport.peer, *transport.bind)
        if error is not None:
            _raise(error)
        assert native is not None
        return cls(native)

    @property
    def is_open(self) -> bool:
        return self._client.is_open

    @property
    def local_port(self) -> int:
        return self._client.local_port

    def add(self, *, left: int, right: int, timeout: float = 1.0) -> AddResponse:
        return self.add_request(AddRequest(left, right), timeout=timeout)

    def add_request(self, request: AddRequest, *, timeout: float = 1.0) -> AddResponse:
        if not isinstance(request, AddRequest):
            raise TypeError("request must be AddRequest")
        if not isinstance(timeout, (int, float)) or isinstance(timeout, bool):
            raise TypeError("timeout must be a finite positive number of seconds")
        if not 0 < timeout <= (2**31 - 1) / 1000 or not math.isfinite(timeout):
            raise ValueError("timeout must be positive and at most 2147483.647 seconds")
        value, error = self._client.add(request.left, request.right, math.ceil(timeout * 1000))
        if error is not None:
            _raise(error)
        assert value is not None
        return AddResponse(value)

    def close(self) -> None:
        self._client.close()

    def __enter__(self) -> Client:
        # A context never reopens a closed connection; operations report ClosedError.
        return self

    def __exit__(self, kind: type[BaseException] | None, error: BaseException | None,
                 traceback: TracebackType | None) -> None:
        self.close()


__all__ = [
    "Client", "Udp", "AddRequest", "AddResponse", "WirelinkError", "ClosedError",
    "RpcTimeoutError", "CancelledError", "RejectedError", "QueueFullError",
    "TransportError", "CodecError", "InvalidArgumentError", "core_version",
    "codegen_abi", "binding_api",
]
