#!/usr/bin/env python3
"""Create deterministic self-contained netft CLI release archives."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import os
import re
import stat
import tarfile
import zipfile
from pathlib import Path
from typing import Final


TARGETS: Final = {
    "linux-x86_64": ".tar.gz",
    "linux-arm64": ".tar.gz",
    "macos-x86_64": ".tar.gz",
    "macos-arm64": ".tar.gz",
    "windows-x86_64": ".zip",
}
VERSION_PATTERN: Final = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")


def _source_date_epoch() -> int:
    value = os.environ.get("SOURCE_DATE_EPOCH", "0")
    try:
        epoch = int(value)
    except ValueError as error:
        raise ValueError("SOURCE_DATE_EPOCH must be an integer") from error
    if epoch < 0:
        raise ValueError("SOURCE_DATE_EPOCH must not be negative")
    return epoch


def _runtime_payload(
    binary: Path, version: str, source_root: Path
) -> list[tuple[str, bytes, int]]:
    root = f"netft-cli-{version}"
    payload = [
        (f"{root}/LICENSE", (source_root / "LICENSE").read_bytes(), 0o644),
        (
            f"{root}/LICENSES/curl.txt",
            (source_root / "LICENSES" / "curl.txt").read_bytes(),
            0o644,
        ),
        (
            f"{root}/LICENSES/netft-cpp.txt",
            (source_root / "core" / "netft" / "LICENSE").read_bytes(),
            0o644,
        ),
        (f"{root}/{binary.name}", binary.read_bytes(), 0o755),
    ]
    return sorted(payload, key=lambda item: item[0])


def _write_tar_gz(
    archive: Path, payload: list[tuple[str, bytes, int]], epoch: int
) -> None:
    with archive.open("wb") as destination:
        with gzip.GzipFile(
            filename="", mode="wb", fileobj=destination, mtime=epoch
        ) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as tar:
                for name, data, mode in payload:
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    info.mode = mode
                    info.mtime = epoch
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    tar.addfile(info, io.BytesIO(data))


def _write_zip(
    archive: Path, payload: list[tuple[str, bytes, int]], epoch: int
) -> None:
    import datetime

    timestamp = datetime.datetime.fromtimestamp(
        max(epoch, 315532800), tz=datetime.timezone.utc
    )
    date_time = (
        timestamp.year,
        timestamp.month,
        timestamp.day,
        timestamp.hour,
        timestamp.minute,
        timestamp.second - timestamp.second % 2,
    )
    with zipfile.ZipFile(
        archive, mode="w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as release:
        for name, data, mode in payload:
            info = zipfile.ZipInfo(name, date_time=date_time)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | mode) << 16
            release.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def _write_checksums(output: Path) -> None:
    assets = sorted(
        path
        for path in output.iterdir()
        if path.is_file()
        and path.name != "SHA256SUMS"
        and (
            path.name.endswith(".tar.gz")
            or path.name.endswith(".zip")
        )
    )
    lines = [
        f"{hashlib.sha256(asset.read_bytes()).hexdigest()}  {asset.name}\n"
        for asset in assets
    ]
    (output / "SHA256SUMS").write_text("".join(lines), encoding="utf-8", newline="\n")


def package_release(
    binary: Path,
    version: str,
    target: str,
    output: Path,
    source_root: Path | None = None,
) -> Path:
    """Package one native executable and return the resulting archive path."""
    binary = Path(binary)
    output = Path(output)
    source_root = (
        Path(source_root)
        if source_root is not None
        else Path(__file__).resolve().parents[1]
    )
    if target not in TARGETS:
        raise ValueError(f"unsupported release target: {target}")
    if VERSION_PATTERN.fullmatch(version) is None:
        raise ValueError(f"invalid release version: {version}")
    expected_binary = "netft.exe" if target.startswith("windows-") else "netft"
    if binary.name != expected_binary:
        raise ValueError(f"{target} release binary must be named {expected_binary}")
    if not binary.is_file():
        raise ValueError(f"release binary does not exist: {binary}")

    output.mkdir(parents=True, exist_ok=True)
    archive = output / f"netft-cli-{version}-{target}{TARGETS[target]}"
    payload = _runtime_payload(binary, version, source_root)
    epoch = _source_date_epoch()
    if TARGETS[target] == ".zip":
        _write_zip(archive, payload, epoch)
    else:
        _write_tar_gz(archive, payload, epoch)
    _write_checksums(output)
    return archive


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--target", choices=sorted(TARGETS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    archive = package_release(
        arguments.binary,
        arguments.version,
        arguments.target,
        arguments.output,
    )
    print(archive)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
