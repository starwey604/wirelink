# SPDX-License-Identifier: Apache-2.0
"""Desktop counterparts of the Zephyr peer; functional, not a benchmark."""
import argparse

import queue
import socket
import subprocess
import threading
import time


class Process:
    def __init__(self, command):
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True)
        self.lines = []
        self.messages = queue.Queue()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.process.stdout:
            self.lines.append(line)
            self.messages.put(line)

    def ready(self, expected):
        end = time.monotonic() + 5
        while time.monotonic() < end:
            try:
                if expected in self.messages.get(timeout=0.1):
                    return
            except queue.Empty:
                if self.process.poll() is not None:
                    break
        raise RuntimeError("process did not become ready: " + "".join(self.lines))

    def output(self, timeout=5):
        code = self.process.wait(timeout=timeout)
        self.reader.join(timeout=1)
        return code, "".join(self.lines)

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.output(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.output()
        self.process.stdout.close()


def udp_socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    return sock


def endpoint_ports():
    a, b = udp_socket(), udp_socket()
    ports = a.getsockname()[1], b.getsockname()[1]
    a.close()
    b.close()
    return ports


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
