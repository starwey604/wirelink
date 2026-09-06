# SPDX-License-Identifier: Apache-2.0
"""Exercise a native-owned endpoint through exported C, with no Python callbacks."""
import ctypes as c
import sys

lib = c.CDLL(sys.argv[1])
for name, args, return_type in [
    ("create", [c.c_uint32, c.c_int], c.c_void_p),
    ("close", [c.c_void_p], None),
    ("destroy", [c.c_void_p], None),
    ("start", [c.c_void_p, c.c_int32, c.c_int32, c.c_uint32], c.c_int),
    ("step", [c.c_void_p], c.c_int),
    ("advance", [c.c_void_p, c.c_uint32], c.c_int),
    ("result", [c.c_void_p, c.POINTER(c.c_int32)], c.c_int),
    ("reads", [c.c_void_p], c.c_uint32),
    ("handled", [c.c_void_p], c.c_uint32),
]:
    function = getattr(lib, "clock_bridge_" + name)
    function.argtypes, function.restype = args, return_type
    globals()[name] = function


def check(condition):
    if not condition:
        raise RuntimeError("clock bridge contract failed")


def pump(pair, count=6):
    for _ in range(count):
        before = reads(pair)
        check(step(pair) == 0)
        check(reads(pair) == before + 2)  # Inline reply reuses the server pass.


for native in (0, 1):
    pair = create(60000, native)
    check(pair and reads(pair) == 0)
    try:
        check(start(pair, 20, 22, 1000) == 0 and reads(pair) == 1)
        pump(pair)
        value = c.c_int32()
        before = reads(pair)
        check(result(pair, c.byref(value)) == 1 and value.value == 42)
        check(reads(pair) == before)  # Completion already recycled the call.
        if not native:
            check(advance(pair, 100000) == 0)  # New request after an idle gap.
        check(start(pair, 2147483647, 1, 1000) == 0)
        pump(pair)
        check(result(pair, c.byref(value)) == 2)
        check(handled(pair) == 2)
        before = reads(pair)
        close(pair)
        close(pair)
        check(step(pair) == -1 and reads(pair) == before)
    finally:
        destroy(pair)

# An unfinished RPC crosses the uint32 clock wrap and expires at its deadline.
pair = create(0xFFFFFFFD, 0)
check(pair)
try:
    check(start(pair, -2147483648, 0, 10) == 0)
    pump(pair)
    value = c.c_int32()
    check(advance(pair, 9) == 0)
    pump(pair)
    check(result(pair, c.byref(value)) == 0)
    check(advance(pair, 1) == 0)
    pump(pair)
    check(result(pair, c.byref(value)) == 3)
finally:
    destroy(pair)
print("Python -> exported C -> generated endpoint: OK (native/manual/wrap/reject/close)")
