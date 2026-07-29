#!/usr/bin/env python3
"""Validate netft CLI release inventory and native runtime dependencies."""

from __future__ import annotations

import argparse
import hashlib
import platform
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Final, Iterable


TARGET_EXTENSIONS: Final = {
    "linux-x86_64": ".tar.gz",
    "linux-arm64": ".tar.gz",
    "macos-x86_64": ".tar.gz",
    "macos-arm64": ".tar.gz",
    "windows-x86_64": ".zip",
}
CHECKSUM_PATTERN: Final = re.compile(r"([0-9a-f]{64})  ([^\s/]+)")
TARGET_BINARY_FORMAT: Final = {
    "linux-x86_64": ("elf", "x86_64"),
    "linux-arm64": ("elf", "arm64"),
    "macos-x86_64": ("macho", "x86_64"),
    "macos-arm64": ("macho", "arm64"),
    "windows-x86_64": ("pe", "x86_64"),
}


class ReleaseError(RuntimeError):
    """Release artifact contract violation."""


def _expected_assets(version: str) -> dict[str, str]:
    return {
        target: f"netft-cli-{version}-{target}{extension}"
        for target, extension in TARGET_EXTENSIONS.items()
    }


def _read_checksums(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise ReleaseError("SHA256SUMS is missing")
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = CHECKSUM_PATTERN.fullmatch(line)
        if match is None:
            raise ReleaseError("SHA256SUMS has an invalid entry")
        digest, name = match.groups()
        if name in result:
            raise ReleaseError(f"SHA256SUMS has a duplicate entry for {name}")
        result[name] = digest
    return result


def validate_inventory(
    directory: Path, version: str, allow_partial: bool = False
) -> list[Path]:
    """Validate exact asset names and their recorded SHA-256 digests."""
    directory = Path(directory)
    if not directory.is_dir():
        raise ReleaseError(f"release directory does not exist: {directory}")
    expected = _expected_assets(version)
    present = {
        path.name
        for path in directory.iterdir()
        if path.is_file() and path.name != "SHA256SUMS"
    }
    allowed = set(expected.values())
    unexpected = sorted(present - allowed)
    if unexpected:
        raise ReleaseError(f"unexpected release asset: {unexpected[0]}")
    if not present:
        raise ReleaseError("no release assets found")
    if not allow_partial:
        missing = [target for target, name in expected.items() if name not in present]
        if missing:
            raise ReleaseError(f"missing platform: {missing[0]}")

    checksums = _read_checksums(directory / "SHA256SUMS")
    if set(checksums) != present:
        raise ReleaseError("checksum inventory does not match release assets")
    assets = sorted((directory / name for name in present), key=lambda path: path.name)
    for asset in assets:
        actual = hashlib.sha256(asset.read_bytes()).hexdigest()
        if actual != checksums[asset.name]:
            raise ReleaseError(f"checksum mismatch for {asset.name}")
    return assets


def reject_dynamic_libcurl(dependencies: Iterable[str]) -> None:
    for dependency in dependencies:
        if re.search(r"(^|[/\\])libcurl(?:[-.]|$)", dependency, re.IGNORECASE):
            raise ReleaseError(f"dynamic libcurl dependency detected: {dependency}")


def inspect_binary_format(binary: Path) -> tuple[str, str]:
    with binary.open("rb") as source:
        header = source.read(64)
        if header.startswith(b"\x7fELF"):
            if len(header) < 20 or header[4] != 2 or header[5] not in (1, 2):
                raise ReleaseError("release binary is not a supported ELF64 file")
            byteorder = "little" if header[5] == 1 else "big"
            machine = int.from_bytes(header[18:20], byteorder)
            architectures = {62: "x86_64", 183: "arm64"}
            if machine not in architectures:
                raise ReleaseError("release binary has an unsupported ELF architecture")
            return "elf", architectures[machine]

        macho_magic = header[:4]
        fat_macho_magics = {
            b"\xca\xfe\xba\xbe",
            b"\xbe\xba\xfe\xca",
            b"\xca\xfe\xba\xbf",
            b"\xbf\xba\xfe\xca",
        }
        if macho_magic in fat_macho_magics:
            raise ReleaseError("universal Mach-O binaries are not release assets")
        macho_endian = {
            b"\xcf\xfa\xed\xfe": "little",
            b"\xfe\xed\xfa\xcf": "big",
        }.get(macho_magic)
        if macho_endian is not None:
            if len(header) < 8:
                raise ReleaseError("release binary has a truncated Mach-O header")
            cpu_type = int.from_bytes(header[4:8], macho_endian)
            architectures = {
                0x01000007: "x86_64",
                0x0100000C: "arm64",
            }
            if cpu_type not in architectures:
                raise ReleaseError("release binary has an unsupported Mach-O architecture")
            return "macho", architectures[cpu_type]

        if header.startswith(b"MZ"):
            if len(header) < 64:
                raise ReleaseError("release binary has a truncated DOS header")
            pe_offset = int.from_bytes(header[60:64], "little")
            source.seek(pe_offset)
            coff_header = source.read(6)
            if len(coff_header) != 6 or coff_header[:4] != b"PE\0\0":
                raise ReleaseError("release binary has an invalid PE header")
            machine = int.from_bytes(coff_header[4:6], "little")
            architectures = {0x8664: "x86_64", 0xAA64: "arm64"}
            if machine not in architectures:
                raise ReleaseError("release binary has an unsupported PE architecture")
            return "pe", architectures[machine]

    raise ReleaseError("release binary format is not supported")


def validate_binary_target(binary: Path, target: str) -> None:
    expected = TARGET_BINARY_FORMAT.get(target)
    if expected is None:
        raise ReleaseError(f"unsupported release target: {target}")
    actual = inspect_binary_format(binary)
    if actual != expected:
        raise ReleaseError(
            f"release binary format {actual[0]}/{actual[1]} does not match {target}"
        )


def _run_tool(command: list[str]) -> str:
    try:
        completed = subprocess.run(
            command, check=True, capture_output=True, text=True
        )
    except FileNotFoundError as error:
        raise ReleaseError(f"required dependency inspection tool is missing: {command[0]}") from error
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout).strip()
        raise ReleaseError(
            f"dependency inspection failed with {command[0]}: {detail}"
        ) from error
    return completed.stdout


def parse_elf_dependencies(output: str) -> list[str]:
    return re.findall(r"\(NEEDED\).*Shared library: \[([^\]]+)\]", output)


def parse_macho_dependencies(output: str) -> list[str]:
    return [
        line.strip().split(" ", 1)[0]
        for line in output.splitlines()[1:]
        if line.strip()
    ]


def parse_pe_dependencies(output: str, tool: str) -> list[str]:
    if tool == "llvm-readobj":
        return re.findall(r"^\s*Name:\s*(\S+)\s*$", output, re.MULTILINE)
    if tool == "dumpbin":
        return re.findall(
            r"^\s+(\S+\.dll)\s*$", output, re.IGNORECASE | re.MULTILINE
        )
    raise ReleaseError(f"unsupported PE dependency inspection tool: {tool}")


def _elf_dependencies(binary: Path) -> list[str]:
    return parse_elf_dependencies(_run_tool(["readelf", "-d", str(binary)]))


def _macho_dependencies(binary: Path) -> list[str]:
    return parse_macho_dependencies(_run_tool(["otool", "-L", str(binary)]))


def _pe_dependencies(binary: Path) -> list[str]:
    llvm_readobj = shutil.which("llvm-readobj")
    if llvm_readobj is not None:
        output = _run_tool([llvm_readobj, "--coff-imports", str(binary)])
        return parse_pe_dependencies(output, "llvm-readobj")
    dumpbin = shutil.which("dumpbin")
    if dumpbin is not None:
        output = _run_tool([dumpbin, "/dependents", str(binary)])
        return parse_pe_dependencies(output, "dumpbin")
    raise ReleaseError(
        "PE dependency inspection requires llvm-readobj or dumpbin"
    )


def inspect_dynamic_dependencies(binary: Path, target: str) -> list[str]:
    if target.startswith("linux-"):
        return _elf_dependencies(binary)
    if target.startswith("macos-"):
        return _macho_dependencies(binary)
    if target == "windows-x86_64":
        return _pe_dependencies(binary)
    raise ReleaseError(f"unsupported release target: {target}")


def _native_target() -> str | None:
    systems = {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}
    architectures = {
        "x86_64": "x86_64",
        "AMD64": "x86_64",
        "aarch64": "arm64",
        "arm64": "arm64",
    }
    system = systems.get(platform.system())
    architecture = architectures.get(platform.machine())
    if system is None or architecture is None:
        return None
    target = f"{system}-{architecture}"
    return target if target in TARGET_EXTENSIONS else None


def _safe_member(name: str, expected_root: str) -> bool:
    path = PurePosixPath(name)
    return (
        not path.is_absolute()
        and ".." not in path.parts
        and len(path.parts) >= 2
        and path.parts[0] == expected_root
    )


def _expected_members(version: str, target: str) -> list[str]:
    root = f"netft-cli-{version}"
    binary = "netft.exe" if target.startswith("windows-") else "netft"
    return [
        f"{root}/LICENSE",
        f"{root}/LICENSES/curl.txt",
        f"{root}/LICENSES/netft-cpp.txt",
        f"{root}/{binary}",
    ]


def _extract_checked(archive: Path, target: str, version: str, destination: Path) -> Path:
    expected = _expected_members(version, target)
    expected_root = f"netft-cli-{version}"
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as release:
            entries = release.infolist()
            names = [entry.filename for entry in entries]
            if names != expected:
                raise ReleaseError(f"archive payload mismatch for {archive.name}")
            if any(not _safe_member(name, expected_root) for name in names):
                raise ReleaseError(f"unsafe archive path in {archive.name}")
            if any(entry.is_dir() for entry in entries):
                raise ReleaseError(f"unexpected directory entry in {archive.name}")
            for entry in entries:
                output = destination.joinpath(*PurePosixPath(entry.filename).parts)
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(release.read(entry))
    else:
        try:
            release = tarfile.open(archive, "r:gz")
        except tarfile.TarError as error:
            raise ReleaseError(f"invalid archive: {archive.name}") from error
        with release:
            entries = release.getmembers()
            names = [entry.name for entry in entries]
            if names != expected:
                raise ReleaseError(f"archive payload mismatch for {archive.name}")
            if any(not _safe_member(name, expected_root) for name in names):
                raise ReleaseError(f"unsafe archive path in {archive.name}")
            if any(not entry.isfile() for entry in entries):
                raise ReleaseError(f"non-file archive member in {archive.name}")
            for entry in entries:
                source = release.extractfile(entry)
                if source is None:
                    raise ReleaseError(f"unable to read archive member: {entry.name}")
                output = destination.joinpath(*PurePosixPath(entry.name).parts)
                output.parent.mkdir(parents=True, exist_ok=True)
                with source:
                    output.write_bytes(source.read())
    binary_name = "netft.exe" if target.startswith("windows-") else "netft"
    binary = destination / expected_root / binary_name
    binary.chmod(binary.stat().st_mode | stat.S_IXUSR)
    return binary


def _target_from_asset(asset: Path, version: str) -> str:
    for target, name in _expected_assets(version).items():
        if asset.name == name:
            return target
    raise ReleaseError(f"unexpected release asset: {asset.name}")


def _check_version(binary: Path, version: str) -> None:
    try:
        completed = subprocess.run(
            [str(binary), "--version"],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise ReleaseError(f"unable to run packaged binary: {error}") from error
    if completed.stdout.splitlines() != [f"netft {version}"]:
        raise ReleaseError("packaged binary reported the wrong version")


def validate_archive(asset: Path, version: str) -> None:
    target = _target_from_asset(asset, version)
    with tempfile.TemporaryDirectory(prefix="netft-cli-release-") as temporary:
        binary = _extract_checked(asset, target, version, Path(temporary))
        validate_binary_target(binary, target)
        reject_dynamic_libcurl(inspect_dynamic_dependencies(binary, target))
        if target == _native_target():
            _check_version(binary, version)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--allow-partial", action="store_true")
    arguments = parser.parse_args()
    assets = validate_inventory(
        arguments.directory, arguments.version, arguments.allow_partial
    )
    for asset in assets:
        validate_archive(asset, arguments.version)
    print(f"validated {len(assets)} release artifact(s)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ReleaseError as error:
        raise SystemExit(f"release validation failed: {error}") from error
