from __future__ import annotations

import hashlib
from pathlib import Path

import pytest

from tools.check_release import (
    ReleaseError,
    reject_dynamic_libcurl,
    validate_inventory,
)


TARGETS = (
    "linux-x86_64",
    "linux-arm64",
    "macos-x86_64",
    "macos-arm64",
    "windows-x86_64",
)


def asset_name(target: str) -> str:
    extension = ".zip" if target.startswith("windows-") else ".tar.gz"
    return f"netft-cli-0.1.0-{target}{extension}"


def write_checksums(directory: Path) -> None:
    lines = []
    for asset in sorted(path for path in directory.iterdir() if path.is_file()):
        if asset.name == "SHA256SUMS":
            continue
        digest = hashlib.sha256(asset.read_bytes()).hexdigest()
        lines.append(f"{digest}  {asset.name}\n")
    (directory / "SHA256SUMS").write_text("".join(lines), encoding="utf-8")


def test_inventory_requires_all_five_platforms(tmp_path: Path) -> None:
    (tmp_path / asset_name("linux-x86_64")).write_bytes(b"release")
    write_checksums(tmp_path)

    with pytest.raises(ReleaseError):
        validate_inventory(tmp_path, version="0.1.0")


def test_inventory_accepts_exact_five_platform_assets(tmp_path: Path) -> None:
    for target in TARGETS:
        (tmp_path / asset_name(target)).write_bytes(target.encode())
    write_checksums(tmp_path)

    validated = validate_inventory(tmp_path, version="0.1.0")

    assert [path.name for path in validated] == sorted(map(asset_name, TARGETS))


def test_partial_inventory_still_verifies_checksum(tmp_path: Path) -> None:
    asset = tmp_path / asset_name("linux-x86_64")
    asset.write_bytes(b"release")
    write_checksums(tmp_path)
    asset.write_bytes(b"tampered")

    with pytest.raises(ReleaseError):
        validate_inventory(tmp_path, version="0.1.0", allow_partial=True)


def test_inventory_rejects_unknown_release_asset(tmp_path: Path) -> None:
    for target in TARGETS:
        (tmp_path / asset_name(target)).write_bytes(target.encode())
    (tmp_path / "netft-cli-0.1.0-linux-i686.tar.gz").write_bytes(b"unexpected")
    write_checksums(tmp_path)

    with pytest.raises(ReleaseError):
        validate_inventory(tmp_path, version="0.1.0")


@pytest.mark.parametrize(
    "dependencies",
    [
        ["linux-vdso.so.1", "libcurl.so.4", "libc.so.6"],
        ["/usr/lib/libcurl.4.dylib", "/usr/lib/libSystem.B.dylib"],
        ["KERNEL32.dll", "LIBCURL.DLL"],
    ],
)
def test_dependency_check_rejects_dynamic_libcurl(dependencies: list[str]) -> None:
    with pytest.raises(ReleaseError):
        reject_dynamic_libcurl(dependencies)
