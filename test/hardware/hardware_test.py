#!/usr/bin/env python3
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import subprocess
from typing import Any


EXECUTABLE = Path(os.environ.get("NETFT_EXECUTABLE", "build/netft")).resolve()


def run_netft(*arguments: str, timeout: float = 10.0) -> str:
    result = subprocess.run(
        [str(EXECUTABLE), *arguments],
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"netft exited with status {result.returncode}: {result.stderr.strip()}"
        )
    if result.stderr:
        raise RuntimeError("netft wrote diagnostics during a successful hardware check")
    return result.stdout


def require_number(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RuntimeError(f"{field} is not numeric")
    result = float(value)
    if not math.isfinite(result):
        raise RuntimeError(f"{field} is not finite")
    return result


def validate_sample(
    record: dict[str, Any], force_unit: str, torque_unit: str
) -> int:
    raw = record.get("raw")
    if not isinstance(raw, list) or len(raw) != 6:
        raise RuntimeError("sample raw wrench must contain six values")
    for index, value in enumerate(raw):
        require_number(value, f"raw[{index}]")

    for group, expected_unit in (("force", force_unit), ("torque", torque_unit)):
        values = record.get(group)
        if not isinstance(values, dict):
            raise RuntimeError(f"sample {group} value is missing")
        if values.get("unit") != expected_unit:
            raise RuntimeError(f"sample {group} unit differs from sensor configuration")
        scaled = values.get("value")
        if not isinstance(scaled, list) or len(scaled) != 3:
            raise RuntimeError(f"sample {group} wrench must contain three values")
        for index, value in enumerate(scaled):
            require_number(value, f"{group}.value[{index}]")

    require_number(record.get("receive_rate_hz"), "receive_rate_hz")
    if not isinstance(record.get("state"), str):
        raise RuntimeError("sample state is missing")
    sequence = record.get("rdt_sequence")
    if isinstance(sequence, bool) or not isinstance(sequence, int):
        raise RuntimeError("rdt_sequence is not an integer")
    return sequence


def configuration_units(configuration: dict[str, Any]) -> tuple[str, str]:
    calibration = configuration.get("calibration")
    if not isinstance(calibration, dict):
        raise RuntimeError("sensor calibration is missing")
    force = calibration.get("force")
    torque = calibration.get("torque")
    if not isinstance(force, dict) or not isinstance(torque, dict):
        raise RuntimeError("sensor calibration units are missing")
    force_unit = force.get("unit")
    torque_unit = torque.get("unit")
    if not isinstance(force_unit, str) or not isinstance(torque_unit, str):
        raise RuntimeError("sensor calibration units are invalid")
    require_number(force.get("counts_per_unit"), "force.counts_per_unit")
    require_number(torque.get("counts_per_unit"), "torque.counts_per_unit")
    return force_unit, torque_unit


def validate_monitor(
    output: str, force_unit: str, torque_unit: str
) -> list[int]:
    records = [json.loads(line) for line in output.splitlines() if line]
    if len(records) < 2:
        raise RuntimeError("bounded monitor returned fewer than two samples")
    sequences = [
        validate_sample(record, force_unit, torque_unit) for record in records
    ]
    if any(current <= previous for previous, current in zip(sequences, sequences[1:])):
        raise RuntimeError("monitor sequences did not increase")
    return sequences


def validate_bias(
    output: str, force_unit: str, torque_unit: str
) -> None:
    record = json.loads(output)
    if not isinstance(record, dict):
        raise RuntimeError("bias output is not an object")
    before = record.get("before")
    after = record.get("after")
    if not isinstance(before, dict) or not isinstance(after, dict):
        raise RuntimeError("bias output is missing before or after samples")
    validate_sample(before, force_unit, torque_unit)
    validate_sample(after, force_unit, torque_unit)


def main() -> int:
    host = os.environ.get("NETFT_SENSOR_HOST")
    if not host:
        raise SystemExit("NETFT_SENSOR_HOST is required")
    if not EXECUTABLE.is_file():
        raise SystemExit(f"netft executable not found: {EXECUTABLE}")

    configuration = json.loads(run_netft("info", host, "--format", "json"))
    if not isinstance(configuration, dict):
        raise RuntimeError("sensor configuration is not an object")
    force_unit, torque_unit = configuration_units(configuration)

    monitor_output = run_netft(
        "monitor",
        host,
        "--duration",
        "500ms",
        "--rate",
        "20",
        "--timeout",
        "2s",
        "--format",
        "ndjson",
    )
    validate_monitor(monitor_output, force_unit, torque_unit)

    bias_allowed = os.environ.get("NETFT_ALLOW_BIAS") == "1"
    if bias_allowed:
        bias_output = run_netft(
            "bias", host, "--yes", "--timeout", "2s", "--format", "json"
        )
        validate_bias(bias_output, force_unit, torque_unit)

    bias_state = "executed" if bias_allowed else "skipped"
    print(f"hardware validation passed (bias: {bias_state})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
