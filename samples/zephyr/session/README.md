# Automatic sessions: P0 functional validation

This standalone ABI 26 sample is not product firmware. It checks the platform
identity source and endpoint reinitialization, then runs the same deterministic
RPC matrix as `tests/session`: reliable/reliable, reliable/unreliable,
unreliable/reliable and unreliable/unreliable.

Cases cover source failure/zero/repetition, shared configuration, client-only
rebuild with same-number old success/rejection injection, deferred replies,
operation-ID exhaustion/recovery and zero identity-source reads on the RPC hot
path. Transport is bounded RAM packet queues, not USB/UART/DMA. Generated values
and core code are shared with host tests. Simulators without a real CSPRNG
explicitly report the platform check as unavailable; H7 must not skip it.

From an initialized Zephyr workspace:

```sh
west build -b dm_mc02/stm32h723xx /path/to/wirelink/samples/zephyr/session \
  -d /path/to/wirelink/build/session-p0-h7 -- \
  -DBOARD_ROOT=/path/to/Ragtime_Firmwares/firmware \
  -DDTS_ROOT=/path/to/Ragtime_Firmwares/firmware \
  -DWIRELINK_WLC_AUTO_DOWNLOAD=OFF \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/matching/abi26/wlc
```

The H7 configuration uses RTT and STM32 hardware RNG. Require `SESSION_P0
platform RNG 128 PASS`, `platform endpoint reinit PASS`, all four matrix passes
and `SESSION_P0 ALL PASS`. Capture probe/build logs and ELF hash. RNG sampling
is a functional check, not a statistical proof of randomness or unique IDs.
