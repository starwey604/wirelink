# SPDX-License-Identifier: Apache-2.0
"""Exercise real C tutorial processes; Python only forwards/damages datagrams."""
import argparse
import queue
import select
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


def telemetry(args):
    pub_port, sub_port = endpoint_ports()
    subscriber = Process([args.subscriber, str(sub_port), str(pub_port)])
    publisher = None
    try:
        subscriber.ready("subscriber ready")
        publisher = Process([args.publisher, str(pub_port), str(sub_port)])
        code, output = subscriber.output(timeout=12)
        if code != 0 or "latest sample=5 temperature=23.50 C" not in output:
            raise RuntimeError(output)
        code, output = publisher.output()
        if code != 0 or "published sample=5" not in output:
            raise RuntimeError(output)
    finally:
        subscriber.close()
        if publisher:
            publisher.close()


def rpc(args, mode):
    client_port, server_port = endpoint_ports()
    # Each endpoint has one fixed peer, including when a fault proxy is used.
    client_proxy, server_proxy = udp_socket(), udp_socket()
    server = Process([args.server, str(server_port), str(server_proxy.getsockname()[1])])
    client = None
    first_request = None
    requests = responses = 0
    dropped_acks = 0
    try:
        server.ready("server ready")
        command = [args.client, str(client_port), str(client_proxy.getsockname()[1])]
        if mode == "reject":
            command.extend([str(2**31 - 1), "1"])
        client = Process(command)
        deadline = time.monotonic() + 5
        while client.process.poll() is None and time.monotonic() < deadline:
            readable, _, _ = select.select([client_proxy, server_proxy], [], [], 0.02)
            for incoming in readable:
                data, source = incoming.recvfrom(65535)
                from_client = incoming is client_proxy
                expected_source = client_port if from_client else server_port
                if source != ("127.0.0.1", expected_source):
                    raise RuntimeError("unexpected test source")
                outgoing = server_proxy if from_client else client_proxy
                destination = ("127.0.0.1", server_port if from_client else client_port)
                # Only classify the existing Compact-v1 header for injection;
                # never synthesize a Wirelink ACK or RPC result in Python.
                is_data = len(data) >= 4 and data[0] == 0xA5
                message_id = int.from_bytes(data[2:4], "big") if is_data else 0
                if not from_client and data and data[0] == 0xA6 and mode in (
                        "response_before_ack", "drop_ack_response"):
                    dropped_acks += 1
                    continue
                if from_client and message_id == 20:
                    requests += 1
                    if mode == "blackhole" or (mode == "drop_request" and requests == 1):
                        continue
                    if mode == "reorder" and requests == 1:
                        first_request = data
                        continue
                if not from_client and message_id == 21:
                    responses += 1
                    if mode in ("drop_response", "drop_ack_response") and responses == 1:
                        continue
                outgoing.sendto(data, destination)
                if mode == "duplicate" and message_id in (20, 21):
                    outgoing.sendto(data, destination)
                if from_client and first_request is not None and requests == 2:
                    outgoing.sendto(first_request, destination)
                    first_request = None
        code, output = client.output(timeout=1)
        if mode == "blackhole":
            if code == 0 or "RPC failed" not in output:
                raise RuntimeError("expected bounded RPC failure: " + output)
        else:
            expected = "addition rejected: status=1" if mode == "reject" else "20 + 22 = 42"
            if code != 0 or expected not in output:
                raise RuntimeError(f"{mode}: {output}\nserver: {''.join(server.lines)}")
        # Drain the final real ACK before shutting the server down.
        for incoming in select.select([client_proxy], [], [], 0.05)[0]:
            data, _ = incoming.recvfrom(65535)
            server_proxy.sendto(data, ("127.0.0.1", server_port))
        server.close()
        handled = sum("handling " in line for line in server.lines)
        if handled != (0 if mode == "blackhole" else 1):
            raise RuntimeError(f"{mode}: unexpected execution count {handled}")
        if mode in ("drop_request", "reorder") and requests < 2:
            raise RuntimeError("test did not observe a request retransmission")
        if mode == "drop_response" and responses < 2:
            raise RuntimeError("test did not observe a response retransmission")
        if mode in ("response_before_ack", "drop_ack_response") and dropped_acks == 0:
            raise RuntimeError("test did not drop the real request ACK")
        if mode == "drop_ack_response" and responses < 2:
            raise RuntimeError("test did not lose both an ACK and an executed request's response")
        print(f"RPC {mode}: OK ({requests} requests, {responses} responses)")
    finally:
        if client:
            client.close()
        server.close()
        client_proxy.close()
        server_proxy.close()


def main():
    parser = argparse.ArgumentParser()
    for name in ("publisher", "subscriber", "client", "server"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    telemetry(args)
    print("Telemetry: OK")
    for mode in ("normal", "reject", "drop_request", "drop_response", "duplicate",
                 "reorder", "response_before_ack", "drop_ack_response", "blackhole"):
        rpc(args, mode)


if __name__ == "__main__":
    main()
