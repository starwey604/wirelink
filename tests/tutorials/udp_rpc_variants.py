# SPDX-License-Identifier: Apache-2.0
"""Cross the ordinary clients and immediate/deferred servers under UDP faults."""
import argparse
from types import SimpleNamespace

from udp_processes import rpc


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--async-client", required=True)
    parser.add_argument("--server", required=True)
    parser.add_argument("--deferred-server", required=True)
    args = parser.parse_args()
    for client, server in ((args.async_client, args.server),
                           (args.client, args.deferred_server),
                           (args.async_client, args.deferred_server)):
        for mode in ("normal", "reject", "drop_ack_response", "blackhole"):
            rpc(SimpleNamespace(client=client, server=server), mode)


if __name__ == "__main__":
    main()
