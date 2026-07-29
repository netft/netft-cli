#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import socket
import struct
import subprocess
import sys
import threading
import time
from typing import Callable, Deque, Sequence


DEFAULT_XML = (
    "<netft><prodname>Loopback Net F/T</prodname><cfgcpf>1000000</cfgcpf>"
    "<cfgcpt>1000</cfgcpt><scfgfu>N</scfgfu><scfgtu>Nmm</scfgtu></netft>"
)
STOP_STREAMING = 0x0000
START_REALTIME = 0x0002
SET_SOFTWARE_BIAS = 0x0042


class _HttpServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class _ConfigurationHandler(BaseHTTPRequestHandler):
    server: _HttpServer

    def do_GET(self) -> None:
        sensor: FakeSensor = self.server.sensor  # type: ignore[attr-defined]
        with sensor._condition:
            sensor._http_request_count += 1
            sensor._condition.notify_all()
            body = sensor._xml.encode("utf-8")
            status = sensor._http_status if self.path == "/netftapi2.xml" else 404
            delay = sensor._http_delay
        if sensor._stop.wait(delay):
            return
        if status != 200:
            body = b""
        self.send_response(status)
        self.send_header("Content-Type", "application/xml")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: object) -> None:
        del format, args


class FakeSensor:
    def __init__(
        self,
        *,
        rate_hz: float = 200.0,
        http_port: int = 0,
        rdt_port: int = 0,
    ) -> None:
        self.host = "127.0.0.1"
        self._period = 1.0 / rate_hz
        self._stop = threading.Event()
        self._closed = False
        self._enabled = threading.Event()
        self._enabled.set()
        self._condition = threading.Condition()
        self._streaming = False
        self._client: tuple[str, int] | None = None
        self._rdt_sequence = 0
        self._ft_sequence = 1000
        self._skip = 0
        self._status = 0
        self._records: Deque[bytes] = deque()
        self._start_realtime_count = 0
        self._stop_streaming_count = 0
        self._software_bias_count = 0
        self._http_request_count = 0
        self._xml = DEFAULT_XML
        self._http_status = 200
        self._http_delay = 0.0

        self._udp: socket.socket | None = None
        self._http: _HttpServer | None = None
        self._udp_thread: threading.Thread | None = None
        self._http_thread: threading.Thread | None = None
        try:
            self._udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._udp.bind((self.host, rdt_port))
            self._udp.settimeout(0.01)
            self.rdt_port = self._udp.getsockname()[1]

            self._http = _HttpServer((self.host, http_port), _ConfigurationHandler)
            self._http.sensor = self  # type: ignore[attr-defined]
            self.http_port = self._http.server_address[1]
            self._udp_thread = threading.Thread(target=self._run_udp, daemon=True)
            self._http_thread = threading.Thread(
                target=self._http.serve_forever, daemon=True
            )
            self._udp_thread.start()
            self._http_thread.start()
        except BaseException:
            self._close_resources()
            raise

    def __enter__(self) -> FakeSensor:
        return self

    def __exit__(self, *exc_info: object) -> None:
        del exc_info
        self.close()

    def close(self) -> None:
        if self._closed:
            return
        self._close_resources()

    def _close_resources(self) -> None:
        self._closed = True
        self._stop.set()
        if (
            self._http is not None
            and self._http_thread is not None
            and self._http_thread.is_alive()
        ):
            self._http.shutdown()
        if self._http is not None:
            self._http.server_close()
        if self._udp is not None:
            self._udp.close()
        if self._udp_thread is not None and self._udp_thread.is_alive():
            self._udp_thread.join(timeout=2.0)
        if self._http_thread is not None and self._http_thread.is_alive():
            self._http_thread.join(timeout=2.0)

    @property
    def start_realtime_count(self) -> int:
        with self._condition:
            return self._start_realtime_count

    @property
    def stop_streaming_count(self) -> int:
        with self._condition:
            return self._stop_streaming_count

    @property
    def software_bias_count(self) -> int:
        with self._condition:
            return self._software_bias_count

    @property
    def http_request_count(self) -> int:
        with self._condition:
            return self._http_request_count

    def pause(self) -> None:
        self._enabled.clear()

    def resume(self) -> None:
        self._enabled.set()

    def skip_records(self, count: int) -> None:
        with self._condition:
            self._skip += count

    def set_status(self, status: int) -> None:
        with self._condition:
            self._status = status

    def queue_record(
        self,
        rdt_sequence: int,
        *,
        status: int = 0,
        ft_sequence: int | None = None,
        axes: tuple[int, int, int, int, int, int] = (100, -200, 300, 10, -20, 30),
    ) -> None:
        with self._condition:
            if ft_sequence is None:
                ft_sequence = self._ft_sequence
            self._ft_sequence = ft_sequence + 4
            self._records.append(
                struct.pack(
                    "!IIIiiiiii",
                    rdt_sequence,
                    ft_sequence,
                    status,
                    *axes,
                )
            )

    def set_http_response(self, xml: str, status: int = 200) -> None:
        with self._condition:
            self._xml = xml
            self._http_status = status

    def set_http_response_delay(self, delay_seconds: float) -> None:
        with self._condition:
            self._http_delay = delay_seconds

    def wait_for_start_realtime(self, count: int = 1, timeout: float = 2.0) -> bool:
        return self._wait_for(lambda: self._start_realtime_count >= count, timeout)

    def wait_for_stop_streaming(self, count: int = 1, timeout: float = 2.0) -> bool:
        return self._wait_for(lambda: self._stop_streaming_count >= count, timeout)

    def _wait_for(self, predicate: Callable[[], bool], timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        with self._condition:
            while not predicate():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False
                self._condition.wait(remaining)
            return True

    def _observe_command(self, data: bytes, peer: tuple[str, int]) -> None:
        if len(data) != 8 or data[:2] != b"\x12\x34":
            return
        command = int.from_bytes(data[2:4], "big")
        with self._condition:
            if command == START_REALTIME:
                self._client = peer
                self._rdt_sequence = 0
                self._streaming = True
                self._start_realtime_count += 1
            elif command == STOP_STREAMING:
                self._streaming = False
                self._stop_streaming_count += 1
            elif command == SET_SOFTWARE_BIAS:
                self._streaming = False
                self._software_bias_count += 1
            self._condition.notify_all()

    def _next_record(self) -> tuple[bytes, tuple[str, int]] | None:
        with self._condition:
            if not self._streaming or self._client is None or not self._enabled.is_set():
                return None
            if self._records:
                return self._records.popleft(), self._client
            self._rdt_sequence += 1 + self._skip
            self._skip = 0
            data = struct.pack(
                "!IIIiiiiii",
                self._rdt_sequence,
                self._ft_sequence,
                self._status,
                100,
                -200,
                300,
                10,
                -20,
                30,
            )
            self._ft_sequence += 4
            return data, self._client

    def _run_udp(self) -> None:
        deadline = time.monotonic()
        while not self._stop.is_set():
            try:
                data, peer = self._udp.recvfrom(64)
                self._observe_command(data, peer)
            except TimeoutError:
                pass
            except OSError:
                return
            now = time.monotonic()
            if now >= deadline:
                record = self._next_record()
                if record is not None:
                    try:
                        self._udp.sendto(*record)
                    except OSError:
                        return
                deadline = max(deadline + self._period, now + self._period)


def _option_value(command: Sequence[str], option: str) -> int:
    try:
        index = command.index(option)
    except ValueError:
        return 0
    if index + 1 >= len(command):
        raise SystemExit(f"{option} requires a value")
    return int(command[index + 1])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    command = arguments.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a command is required after --")

    http_port = _option_value(command, "--http-port")
    rdt_port = _option_value(command, "--rdt-port")
    with FakeSensor(http_port=http_port, rdt_port=rdt_port) as sensor:
        if "--http-port" not in command:
            command.extend(["--http-port", str(sensor.http_port)])
        if "--rdt-port" not in command:
            command.extend(["--rdt-port", str(sensor.rdt_port)])
        result = subprocess.run(command, check=False)
        stopped = sensor.wait_for_stop_streaming(timeout=0.5)
        print(
            f"[fake-sensor] start={sensor.start_realtime_count} "
            f"stop={sensor.stop_streaming_count} observed_stop={str(stopped).lower()}",
            file=sys.stderr,
        )
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
