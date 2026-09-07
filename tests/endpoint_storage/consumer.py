# SPDX-License-Identifier: Apache-2.0
"""Native-owned endpoint lifetimes: no Python callbacks or borrowed protocol values."""
import ctypes as c
from contextlib import contextmanager
import sys

lib = c.CDLL(sys.argv[1])
for name, args, result in [
    ("create", [c.c_int], c.c_void_p),
    ("call", [c.c_void_p, c.c_int32, c.POINTER(c.c_int32)], c.c_int),
    ("close", [c.c_void_p], c.c_int),
    ("destroy", [c.c_void_p], None),
    ("allocations", [c.c_void_p], c.c_uint),
    ("deallocations", [c.c_void_p], c.c_uint),
]:
    function = getattr(lib, "storage_bridge_" + name)
    function.argtypes, function.restype = args, result
    globals()[name] = function


@contextmanager
def endpoint():
    pointer = create(0)
    assert pointer
    try:
        yield pointer
    finally:
        destroy(pointer)


assert not create(1)
saved = c.c_int32()
for _ in range(20):
    with endpoint() as pair:
        for value in range(100):
            assert call(pair, value, c.byref(saved)) == 0 and saved.value == value
            assert allocations(pair) == 2 and deallocations(pair) == 0
        assert close(pair) == 0 and close(pair) == 0
        assert deallocations(pair) == 2
        assert call(pair, 1, c.byref(saved)) == -1 and saved.value == 99
assert saved.value == 99
print("Python -> native ownership: rollback/pairing/2000 sync/zero hot allocations PASS")
