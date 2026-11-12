# Developing Wirelink

The C11 kernel lives in `src/`, with public interfaces in `include/wirelink/`.
Generated typed routes, platform adapters and host runtimes all use this kernel.
The application supplies storage, owner execution and monotonic time.

Start with the [development checks](development-current.md) for build commands,
optional test configurations and installed-package validation. Use the
[kernel API](static-endpoint-kernel-api.md) for scheduling and ownership rules,
and the [adapter lifecycle](adapters.md) when adding a transport.
