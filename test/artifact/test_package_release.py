from __future__ import annotations

import gzip
import hashlib
import os
import stat
import tarfile
import zipfile
from pathlib import Path

import pytest

from tools.package_release import package_release


ROOT = Path(__file__).resolve().parents[2]
EXPECTED_PAYLOAD = [
    "netft-cli-0.1.0/LICENSE",
    "netft-cli-0.1.0/LICENSES/curl.txt",
    "netft-cli-0.1.0/LICENSES/netft-cpp.txt",
    "netft-cli-0.1.0/netft",
]


def fake_binary(path: Path, name: str = "netft") -> Path:
    binary = path / name
    binary.write_bytes(b"#!/bin/sh\nprintf 'netft 0.1.0\\n'\n")
    binary.chmod(0o755)
    return binary


def archive_members(archive: Path) -> list[str]:
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as release:
            return release.namelist()
    with tarfile.open(archive, "r:gz") as release:
        return release.getnames()


def test_package_contains_only_runtime_payload(tmp_path: Path) -> None:
    archive = package_release(
        fake_binary(tmp_path),
        version="0.1.0",
        target="linux-x86_64",
        output=tmp_path / "dist",
        source_root=ROOT,
    )

    assert archive_members(archive) == EXPECTED_PAYLOAD


def test_package_is_reproducible_and_normalizes_metadata(
    tmp_path: Path, monkeypatch
) -> None:
    binary = fake_binary(tmp_path)
    monkeypatch.setenv("SOURCE_DATE_EPOCH", "1785369600")
    first = package_release(
        binary,
        version="0.1.0",
        target="linux-x86_64",
        output=tmp_path / "first",
        source_root=ROOT,
    )
    os.utime(binary, (1900000000, 1900000000))
    second = package_release(
        binary,
        version="0.1.0",
        target="linux-x86_64",
        output=tmp_path / "second",
        source_root=ROOT,
    )

    assert hashlib.sha256(first.read_bytes()).digest() == hashlib.sha256(
        second.read_bytes()
    ).digest()
    with gzip.open(first, "rb") as compressed:
        with tarfile.open(fileobj=compressed, mode="r:") as release:
            members = release.getmembers()
    assert [member.mtime for member in members] == [1785369600] * len(members)
    assert [stat.S_IMODE(member.mode) for member in members] == [
        0o644,
        0o644,
        0o644,
        0o755,
    ]


def test_windows_package_uses_executable_name_and_reproducible_zip_metadata(
    tmp_path: Path, monkeypatch
) -> None:
    monkeypatch.setenv("SOURCE_DATE_EPOCH", "1785369600")
    archive = package_release(
        fake_binary(tmp_path, "netft.exe"),
        version="0.1.0",
        target="windows-x86_64",
        output=tmp_path / "dist",
        source_root=ROOT,
    )

    with zipfile.ZipFile(archive) as release:
        assert release.namelist() == [
            *EXPECTED_PAYLOAD[:-1],
            "netft-cli-0.1.0/netft.exe",
        ]
        assert len({entry.date_time for entry in release.infolist()}) == 1


def test_package_rejects_target_binary_name_mismatch(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        package_release(
            fake_binary(tmp_path),
            version="0.1.0",
            target="windows-x86_64",
            output=tmp_path / "dist",
            source_root=ROOT,
        )
