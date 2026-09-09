# SPDX-License-Identifier: Apache-2.0
"""Run the real many-service client/server; Python is not a protocol peer."""
import argparse
import time

from udp_processes import Process, endpoint_ports


def check_pair(client_path, server_path, extended=False):
    client_port, server_port = endpoint_ports()
    server = Process([str(server_path), str(server_port), str(client_port)])
    client = None
    try:
        server.ready("device service server ready")
        # Exercise telemetry sent before the client binds. Winsock normally
        # reports the resulting ICMP Port Unreachable on the server's receive.
        time.sleep(0.2)
        if server.process.poll() is not None:
            raise RuntimeError("server stopped before client startup:\n" + "".join(server.lines))
        client = Process([str(client_path), str(client_port), str(server_port)])
        code, output = client.output(timeout=15)
        expected = ["device RPCs: OK", "calculator RPCs: OK", "deferred self-test: OK",
                    "mixed async batch: OK", "outbound-only telemetry: OK", "12 services: OK"]
        if extended:
            expected.append("new GetBuildLabel RPC: OK")
        if code != 0 or any(marker not in output for marker in expected):
            raise RuntimeError(f"client exit={code}\n{output}\nserver:\n{''.join(server.lines)}")
        if server.process.poll() is not None:
            raise RuntimeError("server stopped early:\n" + "".join(server.lines))
        print(output, end="")
    finally:
        if client:
            client.close()
        server.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True)
    parser.add_argument("--server", required=True)
    args = parser.parse_args()
    check_pair(args.client, args.server)


if __name__ == "__main__":
    main()
