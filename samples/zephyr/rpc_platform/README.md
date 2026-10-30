# RPC owner platform and storage probes

A standalone validation application, with one owner task per C kernel. Two sole
producers copy native packets into the peer's two RX units; SENT means the copy
actually finished. Full RX applies backpressure and latched data/credit wakes.
There is no USB/UART/DMA or physical network in this test.

The application wrapper has a one-command pointer queue and callback-result
handoff after the core pass returns. It has no operation matching table or RPC
deadline executor. The kernel owns four calls/four replies. The wrapper accounts
for time in its outer queue before core admission; failed admission creates no
core call or later callback. Borrowed responses are copied before callback exit,
and callers can release job storage only after callback/step completion.

Checks include fixed-pool init failure rollback and exhaustion; four concurrent
calls with the peer owner paused; fifth-call rejection; actual RX-full/credit
recovery; 100 synchronous string/NUL calls; business rejection; a 2023-byte
response; expired outer-queue admission; and a third task waking a long wait to
close. Both owners and all producer tasks join before freeing pool storage.
Copied values remain valid and hot RPC work performs no allocations. Idle owners
must sleep instead of exchanging endless notifications. The old one-slot knob
is explicitly rejected.

Cycle/stack output is diagnostic. Scheduled RAM round trips include task and
wait work; bounded IRQ-off idle/codec/full-value-copy batches run only after
other tasks join. The idle core has zero clock reads. Native virtual cycles may
be zero; QEMU/native values and reported stacks are not physical hardware CPU
measurements or comparisons with the removed runtime.

```sh
west twister -T /path/to/wirelink/samples/zephyr/rpc_platform \
  -p native_sim -p qemu_riscv32 -p qemu_x86_64 \
  --extra-args WIRELINK_WLC_EXECUTABLE=/path/to/matching/wlc
```

Expected marker: `RPC_PLATFORM ALL PASS`. A production application must provide
a fresh initialization identity and define its own retry/idempotency/long-job
protocol. This in-process fixture uses fresh monotonically allocated identities.
