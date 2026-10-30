# Static RPC, telemetry and application upload

One generated kernel per peer carries Query/Reboot RPCs and ordinary messages.
`product.wl` imports `arm.wl` and `upgrade.wl`; profiles compose at generation.
The existing business message structures/IDs and upload phase/status numbers
remain unchanged. The application messages do not define a standard bulk wire.

Build with `WIRELINK_BUILD_WLC_EXAMPLES=ON` and matching WLC 0.13.0, then run
`wirelink_composed_services` or `ctest -R 'composed_services|import_dependencies'`.
The same source runs in the Zephyr composed_services sample. It transfers 4096
bytes through packet loopback, without flash writes, actuators or a real reboot.

The application loop implements its own RAM-upload rules: BEGIN establishes the
object, CHUNK copies at most 256 borrowed bytes, END requests CRC verification
outside the RX callback, and successful commit occurs once. It injects one sink
BUSY without consuming bytes and one lost ordinary status. The application retries
with a finite budget; duplicate chunks must match the already accepted prefix.
A repeated END confirms the committed object without committing twice. Query
RPC and ordinary telemetry continue while the transfer progresses. Close aborts
an incomplete application upload once and produces CLOSED for a pending Query.

This is application-specific business logic. The kernel has no bulk sender,
receiver, reliable DATA/ACK engine, retained LATEST policy, replay cache, peer
reset hook, response ACK observer or generic service scheduler. Both RPC roles
remain available. Ordinary TX admission snapshots the message; a BUSY/WOULD_BLOCK
caller retains responsibility for retry. Physical packet leases remain separate
from logical RPC completion and are drained at shutdown.

A successful Reboot RPC only confirms the application's short business check;
it is not a transport ACK, a guarantee that the peer application persisted a
result, or permission for this demo to reboot hardware. Production retry,
idempotency, durable progress/status, authorization and safe reboot/flash policy
belong to the application. No automatic RPC retry or duplicate suppression is
implied by the upload's explicit duplicate-chunk checks.

The import-dependency test edits a copied imported schema, adds a new import
edge, then edits that new dependency. Actual generated C is rebuilt after both
changes. It exercises the installed-style current helper and no legacy runtime.
