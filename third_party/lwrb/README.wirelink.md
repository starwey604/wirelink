# LwRB vendoring record

Wirelink vendors the minimal C implementation of MaJerle/LwRB required by the
optional RX ring backend.

- Upstream: https://github.com/MaJerle/lwrb
- Version: v3.3.0
- Commit: `d42c6a96e1bec397eb6320d0197e602a5c3312e2`
- Imported files: `src/lwrb.c`, `include/lwrb/lwrb.h`, and `LICENSE`
- Local modifications to upstream source: none

The backend deliberately leaves `LWRB_DISABLE_ATOMIC` undefined. Wirelink
requires the LwRB read and write indices to be lock-free on the selected
target.
