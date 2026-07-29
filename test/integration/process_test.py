from __future__ import annotations

import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import threading
from collections.abc import Iterator
from contextlib import contextmanager

import pytest

from run_fake_sensor import FakeSensor


EXECUTABLE = Path(os.environ.get("NETFT_EXECUTABLE", "build/integration/netft")).resolve()


def run_process(*arguments: str, timeout: float = 5.0) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(EXECUTABLE), *arguments],
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


@pytest.fixture(scope="module", autouse=True)
def require_executable() -> None:
    if not EXECUTABLE.is_file():
        pytest.fail(f"netft executable not found: {EXECUTABLE}")


def test_help_and_version_use_stdout() -> None:
    for option in ("--help", "--version"):
        result = run_process(option)
        assert result.returncode == 0
        assert result.stdout
        assert not result.stderr


def test_invalid_host_is_usage_error() -> None:
    result = run_process("info", "http://127.0.0.1")
    assert result.returncode == 2
    assert not result.stdout
    assert "usage" in result.stderr.splitlines()[0]


def test_unreachable_host_is_discovery_error() -> None:
    with reserved_tcp_port() as port:
        result = run_process(
            "info", "127.0.0.1", "--http-port", str(port), "--timeout", "50ms"
        )
    assert result.returncode == 3
    assert not result.stdout
    assert "discovery" in result.stderr.splitlines()[0]


@contextmanager
def reserved_tcp_port() -> Iterator[int]:
    reservation = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        reservation.bind(("127.0.0.1", 0))
        yield int(reservation.getsockname()[1])
    finally:
        reservation.close()


def test_monitor_timeout_is_stream_error() -> None:
    with FakeSensor() as sensor:
        sensor.pause()
        result = run_process(
            "monitor",
            sensor.host,
            "--http-port",
            str(sensor.http_port),
            "--rdt-port",
            str(sensor.rdt_port),
            "--timeout",
            "50ms",
            "--duration",
            "100ms",
            "--format",
            "ndjson",
        )
    assert result.returncode == 4
    assert not result.stdout
    assert "stream" in result.stderr.splitlines()[0]


def test_serious_status_is_sensor_error() -> None:
    with FakeSensor() as sensor:
        sensor.set_status(0x00000002)
        result = run_process(
            "bias",
            sensor.host,
            "--http-port",
            str(sensor.http_port),
            "--rdt-port",
            str(sensor.rdt_port),
            "--timeout",
            "500ms",
            "--yes",
            "--format",
            "json",
        )
    assert result.returncode == 5
    assert "sensor" in result.stderr.splitlines()[0]


def test_unwritable_output_is_io_error(tmp_path: Path) -> None:
    result = run_process("info", "127.0.0.1", "--output", str(tmp_path))
    assert result.returncode == 6
    assert not result.stdout
    assert "io" in result.stderr.splitlines()[0]


def test_fake_sensor_cleans_up_if_second_thread_cannot_start(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    original_start = threading.Thread.start
    started_threads: list[threading.Thread] = []
    call_count = 0

    def fail_second_start(thread: threading.Thread) -> None:
        nonlocal call_count
        call_count += 1
        if call_count == 2:
            raise RuntimeError("injected thread startup failure")
        original_start(thread)
        started_threads.append(thread)

    monkeypatch.setattr(threading.Thread, "start", fail_second_start)
    with pytest.raises(RuntimeError):
        FakeSensor()

    assert len(started_threads) == 1
    started_threads[0].join(timeout=1.0)
    assert not started_threads[0].is_alive()


def test_monitor_writes_parseable_ndjson_to_stdout_only() -> None:
    with FakeSensor() as sensor:
        result = run_process(
            "monitor",
            sensor.host,
            "--http-port",
            str(sensor.http_port),
            "--rdt-port",
            str(sensor.rdt_port),
            "--duration",
            "100ms",
            "--rate",
            "50",
            "--format",
            "ndjson",
        )
        assert sensor.stop_streaming_count >= 1
    assert result.returncode == 0
    assert not result.stderr
    records = [json.loads(line) for line in result.stdout.splitlines()]
    assert records
    assert all(isinstance(record["raw"], list) for record in records)
    assert all(record["host"] == "127.0.0.1" for record in records)


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX signal process assertion")
def test_sigint_returns_130_and_stops_streaming() -> None:
    with FakeSensor() as sensor:
        process = subprocess.Popen(
            [
                str(EXECUTABLE),
                "monitor",
                sensor.host,
                "--http-port",
                str(sensor.http_port),
                "--rdt-port",
                str(sensor.rdt_port),
                "--duration",
                "30s",
                "--format",
                "ndjson",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        assert sensor.wait_for_start_realtime()
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=5.0)
        assert process.returncode == 130
        assert sensor.wait_for_stop_streaming()
    assert not stderr
    for line in stdout.splitlines():
        json.loads(line)
