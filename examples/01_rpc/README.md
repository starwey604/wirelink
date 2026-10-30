# Four concurrent RPC calls

Build `wirelink_calculator_demo` with `WIRELINK_BUILD_WLC_EXAMPLES=ON` and matching
local WLC. The example starts four calls, invalidates each source request after
admission, then advances both endpoints from one application loop. A reverse
call proves both roles are retained and distinguishes business rejection from
local failure. The short handler returns a response directly.

The old deferred-server example was removed with deferred reply tokens. Long
work belongs to an application job-ID/status protocol; see the multi-business
example's migration. Cancellation and timeout are local and cannot undo remote
side effects; no retry or replay suppression runs automatically.
