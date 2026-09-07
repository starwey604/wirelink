# H2: RPC platform, storage and CPU probes

Standalone ABI 26 validation, not product firmware. Two Zephyr tasks exchange
native packets through bounded RAM queues with latched data/credit notifications.
One task owns each endpoint. This is not USB/UART/DMA hardware validation.

The sample checks one/four-slot endpoint creation from a two-block pool, allocation
and init failure rollback, pool exhaustion, 100 synchronous string-bearing calls,
rejection, a 2023-byte response, 20 asynchronous completions, and a separate task
interrupting a long synchronous wait. Both owners stop/join before destruction;
copied responses remain valid, and no endpoint allocator is called during RPCs.

It reports cycle-counter frequency, endpoint bytes, wait counts and unused stack.
`scheduled_RAM` measures 100 round trips with interrupts/scheduling enabled and a
completion-containing owner pass, not isolated RPC completion CPU cost. Separate
bounded IRQ-off batches measure an idle step, encode/decode of 2023 bytes and
volatile full-value copy after other tasks have joined. No subtractive timer
overhead correction is applied. Cortex-M uses DWT CYCCNT for CPU passes/batches,
not SysTick's interrupt-dependent wrap accounting while IRQs are masked. The
IRQ-enabled round-trip measurement uses the system cycle clock to include sleep.
Native_sim's simulated cycles/stacks are not
hardware measurements; QEMU numbers are also functional diagnostics only.

Build from an initialized Zephyr workspace:

```sh
west build -b dm_mc02/stm32h723xx /path/to/wirelink/samples/zephyr/rpc_platform \
  -d /path/to/wirelink/build/rpc-h2-h7-four -- \
  -DBOARD_ROOT=/path/to/Ragtime_Firmwares/firmware \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/matching/abi26/wlc
```

Repeat with a separate directory and `-DWIRELINK_RPC_TEST_CAPACITY=1`. The sample
matrix runs native_sim, RISC-V and x86_64 QEMU; it does not enlarge the 64-KiB M3
RAM to accommodate two maximum-size endpoints, packet queues and measurement buffers.

Before hardware, require software/CI gates. Record core/compiler SHAs, ELF hash,
board clocks/cache configuration, RAM and build flags. Direct flash is authorized;
no old-firmware backup. Capture RTT including `RPC_H2 ALL PASS`, run/reset both
capacities, and separately report observations rather than comparing QEMU or old
idle figures as if they shared the same measurement setup. Probe connection
failure may need the user to replug/hold RESET. Product physical-link validation
remains H3.
