# Retired generic executor experiment

The old proxy/latest/shared service and variable-capacity executor workload was
removed with that execution contract. Historical reports and their analysis
scripts remain readable; run them only with archived old binaries. Current
owner/command/close tests are in `runtime/host/tests`, owning RPC/Session tests in
`bindings/cpp/tests`, and the current API diagnostic in [README.md](README.md).
It has a different workload identity and is not a compatible timing baseline.
