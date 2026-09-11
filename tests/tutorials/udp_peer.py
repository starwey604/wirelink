# SPDX-License-Identifier: Apache-2.0
"""Desktop counterparts of the Zephyr peer; functional, not a benchmark."""
import argparse

from udp_processes import Process, endpoint_ports


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--server", required=True)
    args = parser.parse_args()
    client_port, server_port = endpoint_ports()
    client_args = [args.client, "127.0.0.1", str(client_port), "127.0.0.1", str(server_port)]
    server = Process([args.server, "127.0.0.1", str(server_port), "127.0.0.1", str(client_port)])
    try:
        server.ready("UDP peer server ready")
        # A new client process/session reuses the same address and port.
        for _ in range(3):
            client = Process(client_args)
            try:
                code, output = client.output(timeout=5)
                if code != 0 or "20 + 22 = 42" not in output or "telemetry sample=" not in output:
                    raise RuntimeError(output)
            finally:
                client.close()
    finally:
        server.close()
    client = Process(client_args)
    try:
        code, output = client.output(timeout=5)
        if code == 0 or "RPC:" not in output:
            raise RuntimeError("absent peer must fail: " + output)
    finally:
        client.close()


if __name__ == "__main__":
    main()
