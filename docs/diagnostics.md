# Diagnostics

`Wirelink::diagnostics` is an optional, separately linked C11 formatter for
bring-up logs and support bundles. It owns no heap or I/O and is not part of
`Wirelink::wirelink`, so firmware that does not link it pays no code cost.

Initialize a caller-owned writer, append any snapshots, then pass its text to
the product logger:

```c
char text[256];
wl_diag_writer_t writer;
wl_rx_counters_t rx;

wl_rx_get_counters(&link, &rx);
wl_diag_writer_init(&writer, text, sizeof(text));
if (wl_diag_format_rx_counters(&writer, &rx) == WL_OK) {
  product_log(text);
}
```

Records use stable, grep-friendly `name key=value` text and end in a newline.
Formatters cover RX, adapters, FIFO, LATEST, outbox, Bulk sender/receiver, RPC
client results, and peer transitions. Several records may be appended to one
writer.

On truncation, a nonempty output buffer remains NUL terminated and the
formatter returns `WL_ERR_BUF_TOO_SMALL`. `writer.required + 1` is the buffer
size needed for the complete text. Use `data=NULL, capacity=0` for a sizing
pass, or reset the same writer with `wl_diag_writer_reset()`.
