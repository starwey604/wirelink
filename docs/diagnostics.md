# Explicit diagnostics

`Wirelink::diagnostics` is an optional allocation-free formatter. It does not
query/advance the core, log from a hot path or own I/O. The application supplies
a `wl_diag_writer_t`, buffer and an already-owned snapshot to a formatter:

```c
char text[256];
wl_diag_writer_t writer;
wl_kernel_result_t result = {WL_KERNEL_REJECTED, -31};
wl_diag_writer_init(&writer, text, sizeof(text));
wl_err_t formatted = wl_diag_format_kernel_result(&writer, &result);
```

The application checks `formatted` before giving text to its logger. Current
formatters cover kernel results and independent FIFO/LATEST/outbox statistics.
Old link RX, adapter-unification, framework Bulk/replay records are removed.
Adapter-specific counters stay in their own public headers and are sampled by
the owner; do not infer a universal counter ABI.

Records are key/value text. Nonempty buffers remain NUL-terminated on
truncation, which returns BUF_TOO_SMALL; `required + 1` gives required capacity.
Use a NULL/zero-capacity writer for sizing. No formatter restores removed
execution state. Explicit host profiling is an outer application tool; optional
observer builds have their own workload/measurement identity and cost.
