from __future__ import annotations

import hashlib
import io
import platform
import shutil
import subprocess
import tarfile
from pathlib import Path

import pytest

from tools.check_release import (
    ReleaseError,
    _extract_checked,
    inspect_binary_format,
    parse_elf_dependencies,
    parse_macho_dependencies,
    parse_pe_dependencies,
    reject_dynamic_libcurl,
    validate_archive,
    validate_binary_target,
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


@pytest.mark.parametrize(
    ("header", "target"),
    [
        (b"\x7fELF\x02\x01" + b"\0" * 12 + b"\x3e\0", "linux-arm64"),
        (b"\xcf\xfa\xed\xfe\x07\0\0\x01", "macos-arm64"),
        (
            b"MZ" + b"\0" * 58 + b"\x40\0\0\0" + b"PE\0\0\x64\xaa",
            "windows-x86_64",
        ),
    ],
)
def test_binary_target_rejects_architecture_mismatch(
    tmp_path: Path, header: bytes, target: str
) -> None:
    binary = tmp_path / "netft"
    binary.write_bytes(header + b"\0" * 128)

    with pytest.raises(ReleaseError):
        validate_binary_target(binary, target)


def test_binary_target_rejects_format_mismatch(tmp_path: Path) -> None:
    binary = tmp_path / "netft"
    binary.write_bytes(b"\x7fELF\x02\x01" + b"\0" * 12 + b"\x3e\0" + b"\0" * 128)

    with pytest.raises(ReleaseError):
        validate_binary_target(binary, "macos-x86_64")


def test_binary_header_parsers_identify_all_release_formats(tmp_path: Path) -> None:
    elf = tmp_path / "elf"
    elf.write_bytes(b"\x7fELF\x02\x01" + b"\0" * 12 + b"\xb7\0" + b"\0" * 128)
    macho = tmp_path / "macho"
    macho.write_bytes(b"\xcf\xfa\xed\xfe\x07\0\0\x01" + b"\0" * 128)
    pe = tmp_path / "pe"
    pe.write_bytes(
        b"MZ" + b"\0" * 58 + b"\x40\0\0\0" + b"PE\0\0\x64\x86" + b"\0" * 128
    )

    assert inspect_binary_format(elf) == ("elf", "arm64")
    assert inspect_binary_format(macho) == ("macho", "x86_64")
    assert inspect_binary_format(pe) == ("pe", "x86_64")


@pytest.mark.parametrize(
    "magic",
    [
        b"\xca\xfe\xba\xbe",
        b"\xbe\xba\xfe\xca",
        b"\xca\xfe\xba\xbf",
        b"\xbf\xba\xfe\xca",
    ],
)
def test_universal_macho_is_rejected(tmp_path: Path, magic: bytes) -> None:
    binary = tmp_path / "netft"
    binary.write_bytes(magic + b"\0" * 128)

    with pytest.raises(ReleaseError):
        inspect_binary_format(binary)


def test_dependency_tool_outputs_are_parsed_structurally() -> None:
    assert parse_elf_dependencies(
        " 0x1 (NEEDED) Shared library: [libpthread.so.0]\n"
        " 0x1 (NEEDED) Shared library: [libcurl.so.4]\n"
    ) == ["libpthread.so.0", "libcurl.so.4"]
    assert parse_macho_dependencies(
        "netft:\n"
        "\t/usr/lib/libSystem.B.dylib (compatibility version 1, current version 1)\n"
        "\t/usr/lib/libcurl.4.dylib (compatibility version 1, current version 1)\n"
    ) == ["/usr/lib/libSystem.B.dylib", "/usr/lib/libcurl.4.dylib"]
    assert parse_pe_dependencies(
        "Import {\n  Name: KERNEL32.dll\n}\nImport {\n  Name: LIBCURL.dll\n}\n",
        tool="llvm-readobj",
    ) == ["KERNEL32.dll", "LIBCURL.dll"]
    assert parse_pe_dependencies(
        "Image has the following dependencies:\n\n"
        "    KERNEL32.dll\n    LIBCURL.dll\n",
        tool="dumpbin",
    ) == ["KERNEL32.dll", "LIBCURL.dll"]


def write_tar(path: Path, members: list[tuple[tarfile.TarInfo, bytes]]) -> None:
    with tarfile.open(path, "w:gz") as archive:
        for info, data in members:
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))


def expected_tar_members(version: str = "0.1.0") -> list[tuple[tarfile.TarInfo, bytes]]:
    root = f"netft-cli-{version}"
    return [
        (tarfile.TarInfo(f"{root}/LICENSE"), b"license"),
        (tarfile.TarInfo(f"{root}/LICENSES/curl.txt"), b"curl"),
        (tarfile.TarInfo(f"{root}/LICENSES/netft-cpp.txt"), b"core"),
        (tarfile.TarInfo(f"{root}/netft"), b"binary"),
    ]


@pytest.mark.parametrize("kind", ["wrong-root", "traversal", "non-file"])
def test_archive_extraction_rejects_unsafe_or_nonruntime_members(
    tmp_path: Path, kind: str
) -> None:
    members = expected_tar_members()
    if kind == "wrong-root":
        members[0][0].name = "other/LICENSE"
    elif kind == "traversal":
        members[0][0].name = "netft-cli-0.1.0/../outside"
    else:
        members[-1][0].type = tarfile.SYMTYPE
        members[-1][0].linkname = "/tmp/outside"
    archive = tmp_path / "netft-cli-0.1.0-linux-x86_64.tar.gz"
    write_tar(archive, members)

    with pytest.raises(ReleaseError):
        _extract_checked(archive, "linux-x86_64", "0.1.0", tmp_path / "extract")
    assert not (tmp_path / "outside").exists()


@pytest.mark.skipif(
    platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"),
    reason="native Linux x86_64 ELF test",
)
@pytest.mark.parametrize("source", ["wrong", "failed"])
def test_validate_archive_rejects_wrong_or_failed_version_executable(
    tmp_path: Path, source: str
) -> None:
    compiler = shutil.which("cc")
    if compiler is None:
        pytest.skip("C compiler unavailable")
    binary = tmp_path / "netft"
    source_file = tmp_path / "netft.c"
    if source == "wrong":
        source_file.write_text(
            '#include <stdio.h>\nint main(void) { puts("different"); return 0; }\n',
            encoding="utf-8",
        )
    else:
        source_file.write_text("int main(void) { return 7; }\n", encoding="utf-8")
    subprocess.run([compiler, str(source_file), "-o", str(binary)], check=True)
    from tools.package_release import package_release

    archive = package_release(
        binary,
        version="0.1.0",
        target="linux-x86_64",
        output=tmp_path / "dist",
        source_root=Path(__file__).resolve().parents[2],
    )

    with pytest.raises(ReleaseError):
        validate_archive(archive, "0.1.0")


def test_partial_inventory_does_not_skip_archive_validation(tmp_path: Path) -> None:
    asset = tmp_path / asset_name("linux-x86_64")
    write_tar(asset, expected_tar_members("9.9.9"))
    write_checksums(tmp_path)

    assets = validate_inventory(tmp_path, "0.1.0", allow_partial=True)
    with pytest.raises(ReleaseError):
        validate_archive(assets[0], "0.1.0")
