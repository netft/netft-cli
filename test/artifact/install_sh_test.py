from __future__ import annotations

import hashlib
import http.server
import io
import os
import shutil
import subprocess
import tarfile
import threading
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

import pytest


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "scripts" / "install" / "install.sh"


def fake_binary(version: str) -> bytes:
    return f"#!/bin/sh\nprintf 'netft {version}\\n'\n".encode()


def release_archive(
    version: str,
    *,
    binary_version: str | None = None,
    extra_name: str | None = None,
    symlink: bool = False,
    traversal: bool = False,
) -> bytes:
    root = f"netft-cli-{version}"
    payload = [
        (f"{root}/LICENSE", b"license"),
        (f"{root}/LICENSES/curl.txt", b"curl"),
        (f"{root}/LICENSES/netft-cpp.txt", b"netft-cpp"),
        (f"{root}/netft", fake_binary(binary_version or version)),
    ]
    if extra_name is not None:
        payload.append((extra_name, b"unexpected"))
    if traversal:
        payload[0] = (f"{root}/../outside", b"outside")

    destination = io.BytesIO()
    with tarfile.open(fileobj=destination, mode="w:gz") as archive:
        for index, (name, data) in enumerate(payload):
            member = tarfile.TarInfo(name)
            member.mode = 0o755 if name.endswith("/netft") else 0o644
            if symlink and index == len(payload) - 1:
                member.type = tarfile.SYMTYPE
                member.linkname = "/tmp/netft-installer-escape"
                member.size = 0
                archive.addfile(member)
            else:
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
    return destination.getvalue()


def checksum_file(files: dict[str, bytes], extra_lines: list[str] | None = None) -> bytes:
    lines = [
        f"{hashlib.sha256(data).hexdigest()}  {name}\n"
        for name, data in sorted(files.items())
    ]
    lines.extend(extra_lines or [])
    return "".join(lines).encode()


class FixtureServer(http.server.ThreadingHTTPServer):
    files: dict[str, bytes]
    requests: list[str]


class FixtureHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - stdlib callback name
        self.server.requests.append(self.path)  # type: ignore[attr-defined]
        data = self.server.files.get(self.path)  # type: ignore[attr-defined]
        if data is None:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *_args: object) -> None:
        pass


@contextmanager
def release_server(files: dict[str, bytes]) -> Iterator[tuple[str, FixtureServer]]:
    server = FixtureServer(("127.0.0.1", 0), FixtureHandler)
    server.files = files
    server.requests = []
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        host, port = server.server_address
        yield f"http://{host}:{port}/releases", server
    finally:
        server.shutdown()
        thread.join()
        server.server_close()


def asset_name(version: str, target: str) -> str:
    extension = ".zip" if target.startswith("windows-") else ".tar.gz"
    return f"netft-cli-{version}-{target}{extension}"


def release_inventory(
    version: str, target: str, current_content: bytes
) -> dict[str, bytes]:
    targets = (
        "linux-x86_64",
        "linux-arm64",
        "macos-x86_64",
        "macos-arm64",
        "windows-x86_64",
    )
    return {
        asset_name(version, item): (
            current_content if item == target else item.encode()
        )
        for item in targets
    }


def fixture_files(
    *,
    version: str = "0.1.0",
    target: str = "linux-x86_64",
    archive: bytes | None = None,
    checksum_lines: list[str] | None = None,
    latest: bool = False,
) -> dict[str, bytes]:
    name = asset_name(version, target)
    content = archive if archive is not None else release_archive(version)
    sums = checksum_file(
        release_inventory(version, target, content),
        checksum_lines,
    )
    prefix = "/releases/latest/download" if latest else f"/releases/download/v{version}"
    return {f"{prefix}/{name}": content, f"{prefix}/SHA256SUMS": sums}


def fake_uname(directory: Path, system: str, machine: str) -> Path:
    directory.mkdir(exist_ok=True)
    wrapper = directory / "uname"
    wrapper.write_text(
        "#!/bin/sh\n"
        'case "$1" in\n'
        f"  -s) printf '%s\\n' '{system}' ;;\n"
        f"  -m) printf '%s\\n' '{machine}' ;;\n"
        "  *) exit 2 ;;\n"
        "esac\n",
        encoding="utf-8",
    )
    wrapper.chmod(0o755)
    return directory


def run_installer(
    base_url: str,
    tmp_path: Path,
    *arguments: str,
    system: str = "Linux",
    machine: str = "x86_64",
    path: str | None = None,
) -> subprocess.CompletedProcess[str]:
    tools = fake_uname(tmp_path / "tools", system, machine)
    environment = os.environ.copy()
    environment.update(
        {
            "HOME": str(tmp_path / "home"),
            "NETFT_CLI_RELEASE_BASE_URL": base_url,
            "PATH": f"{tools}{os.pathsep}{path or os.environ['PATH']}",
            "LC_ALL": "C",
        }
    )
    return subprocess.run(
        ["/bin/sh", str(INSTALLER), *arguments],
        env=environment,
        capture_output=True,
        text=True,
        timeout=20,
    )


@pytest.mark.parametrize(
    ("system", "machine", "target"),
    [
        ("Linux", "x86_64", "linux-x86_64"),
        ("Linux", "aarch64", "linux-arm64"),
        ("Darwin", "x86_64", "macos-x86_64"),
        ("Darwin", "arm64", "macos-arm64"),
    ],
)
def test_install_sh_maps_supported_platforms(
    tmp_path: Path, system: str, machine: str, target: str
) -> None:
    files = fixture_files(target=target)
    with release_server(files) as (base_url, server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(tmp_path / "bin"),
            system=system,
            machine=machine,
        )

    assert result.returncode == 0, result.stderr
    assert any(asset_name("0.1.0", target) in request for request in server.requests)
    assert (tmp_path / "bin" / "netft").read_bytes() == fake_binary("0.1.0")


def test_install_sh_normalizes_explicit_v_version(tmp_path: Path) -> None:
    files = fixture_files()
    with release_server(files) as (base_url, server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "v0.1.0",
            "--bin-dir",
            str(tmp_path / "bin"),
        )

    assert result.returncode == 0, result.stderr
    assert "/releases/download/v0.1.0/SHA256SUMS" in server.requests


def test_install_sh_latest_uses_stable_latest_contract(tmp_path: Path) -> None:
    files = fixture_files(version="0.1.0", latest=True)
    with release_server(files) as (base_url, server):
        result = run_installer(
            base_url, tmp_path, "--bin-dir", str(tmp_path / "bin")
        )

    assert result.returncode == 0, result.stderr
    assert "/releases/latest/download/SHA256SUMS" in server.requests
    assert (tmp_path / "bin" / "netft").read_bytes() == fake_binary("0.1.0")


@pytest.mark.parametrize(
    "version",
    [
        "",
        "1.2",
        "01.2.3",
        "1.02.3",
        "1.2.03",
        "1.2.3-rc.1",
        "1.2.3/../../escape",
        "1.2.3;touch-pwned",
        "v",
    ],
)
def test_install_sh_rejects_nonstable_or_unsafe_version(
    tmp_path: Path, version: str
) -> None:
    with release_server({}) as (base_url, server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            version,
            "--bin-dir",
            str(tmp_path / "bin"),
        )

    assert result.returncode != 0
    assert server.requests == []
    assert not (tmp_path / "escape").exists()
    assert not (tmp_path / "pwned").exists()


def test_install_sh_rejects_explicit_empty_destination(tmp_path: Path) -> None:
    with release_server({}) as (base_url, server):
        result = run_installer(base_url, tmp_path, "--bin-dir", "")

    assert result.returncode != 0
    assert server.requests == []


def test_install_sh_preserves_previous_binary_on_checksum_failure(
    tmp_path: Path,
) -> None:
    archive = release_archive("0.1.0")
    name = asset_name("0.1.0", "linux-x86_64")
    files = {
        f"/releases/download/v0.1.0/{name}": archive + b"corrupt",
        "/releases/download/v0.1.0/SHA256SUMS": checksum_file(
            release_inventory("0.1.0", "linux-x86_64", archive)
        ),
    }
    destination = tmp_path / "bin"
    destination.mkdir()
    old = destination / "netft"
    old.write_bytes(fake_binary("0.0.9"))
    old.chmod(0o755)

    with release_server(files) as (base_url, _server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(destination),
        )

    assert result.returncode != 0
    assert old.read_bytes() == fake_binary("0.0.9")


@pytest.mark.parametrize(
    "archive",
    [
        release_archive("0.1.0", traversal=True),
        release_archive("0.1.0", symlink=True),
        release_archive(
            "0.1.0", extra_name="netft-cli-0.1.0/unexpected"
        ),
        release_archive("0.1.0", binary_version="9.9.9"),
    ],
)
def test_install_sh_rejects_unsafe_archive_and_preserves_binary(
    tmp_path: Path, archive: bytes
) -> None:
    files = fixture_files(archive=archive)
    destination = tmp_path / "bin"
    destination.mkdir()
    old = destination / "netft"
    old.write_bytes(fake_binary("0.0.9"))
    old.chmod(0o755)

    with release_server(files) as (base_url, _server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(destination),
        )

    assert result.returncode != 0
    assert old.read_bytes() == fake_binary("0.0.9")
    assert not (tmp_path / "outside").exists()


@pytest.mark.parametrize(
    "extra_lines",
    [
        [
            f"{'0' * 64}  netft-cli-0.1.0-linux-x86_64.tar.gz\n",
        ],
        [f"{'0' * 64}  ../../malicious\n"],
        ["not-a-checksum\n"],
    ],
)
def test_install_sh_rejects_duplicate_or_malicious_checksum_inventory(
    tmp_path: Path, extra_lines: list[str]
) -> None:
    files = fixture_files(checksum_lines=extra_lines)
    with release_server(files) as (base_url, _server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(tmp_path / "bin"),
        )

    assert result.returncode != 0
    assert not (tmp_path / "bin" / "netft").exists()


def test_install_sh_atomically_replaces_binary_in_custom_destination(
    tmp_path: Path,
) -> None:
    files = fixture_files()
    destination = tmp_path / "custom bin"
    destination.mkdir()
    old = destination / "netft"
    old.write_bytes(fake_binary("0.0.9"))
    old.chmod(0o755)

    with release_server(files) as (base_url, _server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(destination),
        )

    assert result.returncode == 0, result.stderr
    assert old.read_bytes() == fake_binary("0.1.0")
    assert list(destination.glob(".netft-install.*")) == []


def test_install_sh_default_destination_prints_path_hint_without_editing_shell(
    tmp_path: Path,
) -> None:
    home = tmp_path / "home"
    home.mkdir()
    shell_file = home / ".profile"
    shell_file.write_text("keep me\n", encoding="utf-8")
    files = fixture_files()

    with release_server(files) as (base_url, _server):
        result = run_installer(base_url, tmp_path, "--version", "0.1.0")

    assert result.returncode == 0, result.stderr
    assert (home / ".local" / "bin" / "netft").is_file()
    assert str(home / ".local" / "bin") in result.stdout
    assert shell_file.read_text(encoding="utf-8") == "keep me\n"


def test_install_sh_supports_wget_when_curl_is_unavailable(tmp_path: Path) -> None:
    restricted = tmp_path / "restricted-tools"
    restricted.mkdir()
    for command in (
        "awk",
        "chmod",
        "cmp",
        "grep",
        "gzip",
        "mkdir",
        "mktemp",
        "mv",
        "rm",
        "sha256sum",
        "tar",
        "wget",
    ):
        executable = shutil.which(command)
        assert executable is not None
        (restricted / command).symlink_to(executable)

    with release_server(fixture_files()) as (base_url, _server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(tmp_path / "bin"),
            path=str(restricted),
        )

    assert result.returncode == 0, result.stderr
    assert (tmp_path / "bin" / "netft").read_bytes() == fake_binary("0.1.0")


def test_install_sh_propagates_download_failure_and_preserves_binary(
    tmp_path: Path,
) -> None:
    files = {
        "/releases/download/v0.1.0/SHA256SUMS": checksum_file(
            release_inventory("0.1.0", "linux-x86_64", b"missing")
        )
    }
    destination = tmp_path / "bin"
    destination.mkdir()
    old = destination / "netft"
    old.write_bytes(fake_binary("0.0.9"))
    old.chmod(0o755)

    with release_server(files) as (base_url, _server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(destination),
        )

    assert result.returncode != 0
    assert old.read_bytes() == fake_binary("0.0.9")


def test_install_sh_refuses_symbolic_link_destination(tmp_path: Path) -> None:
    destination = tmp_path / "bin"
    destination.mkdir()
    previous = tmp_path / "previous"
    previous.write_bytes(fake_binary("0.0.9"))
    previous.chmod(0o755)
    installed = destination / "netft"
    installed.symlink_to(previous)

    with release_server(fixture_files()) as (base_url, _server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(destination),
        )

    assert result.returncode != 0
    assert installed.is_symlink()
    assert previous.read_bytes() == fake_binary("0.0.9")
