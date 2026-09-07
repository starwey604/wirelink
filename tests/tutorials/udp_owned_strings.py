# SPDX-License-Identifier: Apache-2.0
"""Check saved owned strings using the compiled tutorial's two UDP processes."""
import argparse

from udp_processes import Process, endpoint_ports


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--server", required=True)
    args = parser.parse_args()
    client_port, server_port = endpoint_ports()
    server = Process([args.server, str(server_port), str(client_port)])
    try:
        server.ready("device info server ready")
        # Each process makes two calls; keep the same server to cover peer
        # session changes and prove the saved query count is from the first.
        for query in (1, 3, 5):
            client = Process([args.client, str(client_port), str(server_port)])
            try:
                code, output = client.output()
                expected = f"saved name=demo-sensor firmware=dev query={query}"
                if code != 0 or expected not in output:
                    raise RuntimeError(output)
            finally:
                client.close()
    finally:
        server.close()


if __name__ == "__main__":
    main()
