# SPDX-License-Identifier: Apache-2.0
"""Generated Wirelink SDK with synchronous and asyncio clients. Message data outlives the connection."""
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


class Mode(_runtime.OpenIntEnum):
    MODE_IDLE = 0
    MODE_ACTIVE = 1


@_dataclass(frozen=True, slots=True)
class InfoRequest:

    def __post_init__(self) -> None:
        pass

    def _to_native(self) -> tuple[_Any, ...]:
        return ()

    @_builtins.classmethod
    def _from_native(cls, value: tuple[_Any, ...]) -> InfoRequest:
        return cls(
        )


@_dataclass(frozen=True, slots=True)
class Settings:
    enabled: bool
    token: bytes
    gains: tuple[float, ...]
    label: str | None = None
    mode: Mode | None = None
    counters: tuple[int, ...] | None = None
    floor: int | None = None
    ceiling: int | None = None
    class_: str | None = None
    timeout_: int | None = None

    def __post_init__(self) -> None:
        if self.label is not None:
            object.__setattr__(self, "label", _runtime.string(self.label, "label", 12))
        object.__setattr__(self, "enabled", _runtime.boolean(self.enabled, "enabled"))
        if self.mode is not None:
            object.__setattr__(self, "mode", Mode(_runtime.integer(self.mode, "mode", -(2**31), 2**31 - 1)))
        object.__setattr__(self, "token", _runtime.blob(self.token, "token", 8))
        object.__setattr__(self, "gains", tuple(_runtime.real(element, "gains", 32) for element in _runtime.array(self.gains, "gains", 3)))
        if self.counters is not None:
            object.__setattr__(self, "counters", tuple(_runtime.integer(element, "counters", 0, 2**64 - 1) for element in _runtime.array(self.counters, "counters", 2)))
        if self.floor is not None:
            object.__setattr__(self, "floor", _runtime.integer(self.floor, "floor", -(2**63), 2**63 - 1))
        if self.ceiling is not None:
            object.__setattr__(self, "ceiling", _runtime.integer(self.ceiling, "ceiling", 0, 2**64 - 1))
        if self.class_ is not None:
            object.__setattr__(self, "class_", _runtime.string(self.class_, "class_", 8))
        if self.timeout_ is not None:
            object.__setattr__(self, "timeout_", _runtime.integer(self.timeout_, "timeout_", 0, 65535))

    @_builtins.property
    def label_or_default(self) -> str:
        return "设备" if self.label is None else self.label

    @_builtins.property
    def mode_or_default(self) -> Mode:
        return Mode(1) if self.mode is None else self.mode

    @_builtins.property
    def floor_or_default(self) -> int:
        return -9223372036854775808 if self.floor is None else self.floor

    @_builtins.property
    def ceiling_or_default(self) -> int:
        return 18446744073709551615 if self.ceiling is None else self.ceiling

    def _to_native(self) -> tuple[_Any, ...]:
        return (self.label, self.enabled, self.mode, self.token, self.gains, self.counters, self.floor, self.ceiling, self.class_, self.timeout_, )

    @_builtins.classmethod
    def _from_native(cls, value: tuple[_Any, ...]) -> Settings:
        return cls(
            label=value[0],
            enabled=value[1],
            mode=value[2],
            token=value[3],
            gains=value[4],
            counters=value[5],
            floor=value[6],
            ceiling=value[7],
            class_=value[8],
            timeout_=value[9],
        )


@_dataclass(frozen=True, slots=True)
class InfoResponse:
    name: str
    settings: Settings

    def __post_init__(self) -> None:
        object.__setattr__(self, "name", _runtime.string(self.name, "name", 32))
        object.__setattr__(self, "settings", _runtime.message(self.settings, "settings", Settings))

    def _to_native(self) -> tuple[_Any, ...]:
        return (self.name, self.settings._to_native(), )

    @_builtins.classmethod
    def _from_native(cls, value: tuple[_Any, ...]) -> InfoResponse:
        return cls(
            name=value[0],
            settings=Settings._from_native(value[1]),
        )


@_dataclass(frozen=True, slots=True)
class ConfigureRequest:
    settings: Settings
    opaque: bytes | None = None
    backup: Settings | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "settings", _runtime.message(self.settings, "settings", Settings))
        if self.opaque is not None:
            object.__setattr__(self, "opaque", _runtime.blob(self.opaque, "opaque", 4))
        if self.backup is not None:
            object.__setattr__(self, "backup", _runtime.message(self.backup, "backup", Settings))

    def _to_native(self) -> tuple[_Any, ...]:
        return (self.settings._to_native(), self.opaque, None if self.backup is None else self.backup._to_native(), )

    @_builtins.classmethod
    def _from_native(cls, value: tuple[_Any, ...]) -> ConfigureRequest:
        return cls(
            settings=Settings._from_native(value[0]),
            opaque=value[1],
            backup=None if value[2] is None else Settings._from_native(value[2]),
        )


@_dataclass(frozen=True, slots=True)
class ConfigureResponse:
    settings: Settings
    count: int
    opaque: bytes | None = None
    backup: Settings | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "settings", _runtime.message(self.settings, "settings", Settings))
        if self.opaque is not None:
            object.__setattr__(self, "opaque", _runtime.blob(self.opaque, "opaque", 4))
        if self.backup is not None:
            object.__setattr__(self, "backup", _runtime.message(self.backup, "backup", Settings))
        object.__setattr__(self, "count", _runtime.integer(self.count, "count", 0, 2**32 - 1))

    def _to_native(self) -> tuple[_Any, ...]:
        return (self.settings._to_native(), self.opaque, None if self.backup is None else self.backup._to_native(), self.count, )

    @_builtins.classmethod
    def _from_native(cls, value: tuple[_Any, ...]) -> ConfigureResponse:
        return cls(
            settings=Settings._from_native(value[0]),
            opaque=value[1],
            backup=None if value[2] is None else Settings._from_native(value[2]),
            count=value[3],
        )


@_dataclass(frozen=True, slots=True)
class Numbers:
    i8: int
    u8: int
    i16: int
    u16: int
    i32: int
    u32: int
    i64: int
    u64: int
    f32: int
    f64: int
    single: float
    double_: float
    words: tuple[int, ...]
    doubles: tuple[float, ...] | None = None
    offset: int | None = None
    minimum: int | None = None
    flag: bool | None = None
    text: str | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "i8", _runtime.integer(self.i8, "i8", -128, 127))
        object.__setattr__(self, "u8", _runtime.integer(self.u8, "u8", 0, 255))
        object.__setattr__(self, "i16", _runtime.integer(self.i16, "i16", -32768, 32767))
        object.__setattr__(self, "u16", _runtime.integer(self.u16, "u16", 0, 65535))
        object.__setattr__(self, "i32", _runtime.integer(self.i32, "i32", -(2**31), 2**31 - 1))
        object.__setattr__(self, "u32", _runtime.integer(self.u32, "u32", 0, 2**32 - 1))
        object.__setattr__(self, "i64", _runtime.integer(self.i64, "i64", -(2**63), 2**63 - 1))
        object.__setattr__(self, "u64", _runtime.integer(self.u64, "u64", 0, 2**64 - 1))
        object.__setattr__(self, "f32", _runtime.integer(self.f32, "f32", 0, 2**32 - 1))
        object.__setattr__(self, "f64", _runtime.integer(self.f64, "f64", 0, 2**64 - 1))
        object.__setattr__(self, "single", _runtime.real(self.single, "single", 32))
        object.__setattr__(self, "double_", _runtime.real(self.double_, "double_", 64))
        if self.doubles is not None:
            object.__setattr__(self, "doubles", tuple(_runtime.real(element, "doubles", 64) for element in _runtime.array(self.doubles, "doubles", 2)))
        object.__setattr__(self, "words", tuple(_runtime.integer(element, "words", 0, 2**32 - 1) for element in _runtime.array(self.words, "words", 2)))
        if self.offset is not None:
            object.__setattr__(self, "offset", _runtime.integer(self.offset, "offset", -(2**63), 2**63 - 1))
        if self.minimum is not None:
            object.__setattr__(self, "minimum", _runtime.integer(self.minimum, "minimum", -(2**31), 2**31 - 1))
        if self.flag is not None:
            object.__setattr__(self, "flag", _runtime.boolean(self.flag, "flag"))
        if self.text is not None:
            object.__setattr__(self, "text", _runtime.string(self.text, "text", 16))

    @_builtins.property
    def offset_or_default(self) -> int:
        return -7 if self.offset is None else self.offset

    @_builtins.property
    def minimum_or_default(self) -> int:
        return -2147483648 if self.minimum is None else self.minimum

    @_builtins.property
    def flag_or_default(self) -> bool:
        return True if self.flag is None else self.flag

    @_builtins.property
    def text_or_default(self) -> str:
        return "a\"@NAME@" if self.text is None else self.text

    def _to_native(self) -> tuple[_Any, ...]:
        return (self.i8, self.u8, self.i16, self.u16, self.i32, self.u32, self.i64, self.u64, self.f32, self.f64, self.single, self.double_, self.doubles, self.words, self.offset, self.minimum, self.flag, self.text, )

    @_builtins.classmethod
    def _from_native(cls, value: tuple[_Any, ...]) -> Numbers:
        return cls(
            i8=value[0],
            u8=value[1],
            i16=value[2],
            u16=value[3],
            i32=value[4],
            u32=value[5],
            i64=value[6],
            u64=value[7],
            f32=value[8],
            f64=value[9],
            single=value[10],
            double_=value[11],
            doubles=value[12],
            words=value[13],
            offset=value[14],
            minimum=value[15],
            flag=value[16],
            text=value[17],
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

    def configure(self, *, settings: Settings, opaque: bytes | None = None, backup: Settings | None = None, timeout: float = 1.0) -> ConfigureResponse:
        return self.configure_request(ConfigureRequest(
            settings=settings,
            opaque=opaque,
            backup=backup,
        ), timeout=timeout)

    def configure_request(self, request: ConfigureRequest, *, timeout: float = 1.0) -> ConfigureResponse:
        if not isinstance(request, ConfigureRequest):
            raise TypeError("request must be ConfigureRequest")
        value, error = self._client.configure(request._to_native(), _runtime.timeout_ms(timeout))
        if error is not None:
            _runtime._raise(error)
        assert value is not None
        return ConfigureResponse._from_native(value)

    def get_info(self, *, timeout: float = 1.0) -> InfoResponse:
        return self.get_info_request(InfoRequest(
        ), timeout=timeout)

    def get_info_request(self, request: InfoRequest, *, timeout: float = 1.0) -> InfoResponse:
        if not isinstance(request, InfoRequest):
            raise TypeError("request must be InfoRequest")
        value, error = self._client.get_info(request._to_native(), _runtime.timeout_ms(timeout))
        if error is not None:
            _runtime._raise(error)
        assert value is not None
        return InfoResponse._from_native(value)


class AsyncClient(_runtime.AsyncConnection):
    """An asyncio connection bound to the event loop used by connect().

    Use async with or await close(). RPC cancellation is local and cannot undo
    work already performed by the peer. Responses own all their data.
    """

    def __init__(self, native_client: _native.Client) -> None:
        if not isinstance(native_client, _native.Client):
            raise TypeError("create an AsyncClient using AsyncClient.connect(Udp(...))")
        super().__init__(native_client)

    @_builtins.classmethod
    def connect(cls, transport: Udp) -> AsyncClient:
        _runtime.asyncio.get_running_loop()
        if not isinstance(transport, Udp):
            raise TypeError("transport must be Udp")
        native, error = _native.connect(*transport.peer, *transport.bind)
        if error is not None:
            _runtime._raise(error)
        assert native is not None
        try:
            return cls(native)
        except BaseException:
            native.close()
            raise

    async def __aenter__(self) -> AsyncClient:
        self._check_loop()
        if self._closing:
            _runtime._raise(_native.closed_error())
        return self

    async def __aexit__(self, kind: type[BaseException] | None,
                        error: BaseException | None, traceback: _TracebackType | None) -> None:
        await self.close()

    async def configure(self, *, settings: Settings, opaque: bytes | None = None, backup: Settings | None = None, timeout: float = 1.0) -> ConfigureResponse:
        return await self.configure_request(ConfigureRequest(
            settings=settings,
            opaque=opaque,
            backup=backup,
        ), timeout=timeout)

    async def configure_request(self, request: ConfigureRequest, *, timeout: float = 1.0) -> ConfigureResponse:
        if not isinstance(request, ConfigureRequest):
            raise TypeError("request must be ConfigureRequest")
        return await self._invoke(self._client.configure_async, request._to_native(),
                                  ConfigureResponse._from_native, _runtime.timeout_ms(timeout))

    async def get_info(self, *, timeout: float = 1.0) -> InfoResponse:
        return await self.get_info_request(InfoRequest(
        ), timeout=timeout)

    async def get_info_request(self, request: InfoRequest, *, timeout: float = 1.0) -> InfoResponse:
        if not isinstance(request, InfoRequest):
            raise TypeError("request must be InfoRequest")
        return await self._invoke(self._client.get_info_async, request._to_native(),
                                  InfoResponse._from_native, _runtime.timeout_ms(timeout))


__all__ = [
    "Client", "AsyncClient", "Udp", "WirelinkError", "ClosedError", "RpcTimeoutError", "CancelledError",
    "RejectedError", "QueueFullError", "TransportError", "CodecError", "InvalidArgumentError",
    "core_version", "codegen_abi", "binding_api", "__version__",
    "Mode",
    "InfoRequest",
    "Settings",
    "InfoResponse",
    "ConfigureRequest",
    "ConfigureResponse",
    "Numbers",
]
