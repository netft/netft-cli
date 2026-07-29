from __future__ import annotations

import hashlib
import http.server
import io
import os
import shutil
import subprocess
import tarfile
import threading
import time
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

import pytest


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "scripts" / "install" / "install.sh"
MAX_MEMBER_BYTES = 32 * 1024 * 1024


@dataclass(frozen=True)
class Redirect:
    location: str
    header_name: str = "Location"


@dataclass(frozen=True)
class StreamingBody:
    size: int
    chunk_size: int = 65536


@dataclass(frozen=True)
class DelayedBody:
    data: bytes
    delay_seconds: float = 1.0


def fake_binary(version: str) -> bytes:
    return f"#!/bin/sh\nprintf 'netft {version}\\n'\n".encode()


def release_archive(
    version: str,
    *,
    binary_version: str | None = None,
    extra_name: str | None = None,
    symlink: bool = False,
    traversal: bool = False,
    oversized_license: bool = False,
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
    if oversized_license:
        payload[0] = (f"{root}/LICENSE", b"\0" * (MAX_MEMBER_BYTES + 1))

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
    files: dict[str, bytes | Redirect | StreamingBody | DelayedBody]
    requests: list[str]
    bytes_sent: dict[str, int]
    response_started: threading.Event


class FixtureHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802 - stdlib callback name
        self.server.requests.append(self.path)  # type: ignore[attr-defined]
        data = self.server.files.get(self.path)  # type: ignore[attr-defined]
        if data is None:
            self.send_error(404)
            return
        if isinstance(data, Redirect):
            self.send_response(302)
            self.send_header(data.header_name, data.location)
            self.end_headers()
            return
        if isinstance(data, StreamingBody):
            self.send_response(200)
            self.end_headers()
            sent = 0
            try:
                while sent < data.size:
                    chunk = min(data.chunk_size, data.size - sent)
                    self.wfile.write(b"x" * chunk)
                    self.wfile.flush()
                    sent += chunk
                    self.server.bytes_sent[self.path] = sent  # type: ignore[attr-defined]
            except (BrokenPipeError, ConnectionResetError):
                pass
            return
        if isinstance(data, DelayedBody):
            self.send_response(200)
            self.send_header("Content-Length", str(len(data.data)))
            self.end_headers()
            self.server.response_started.set()  # type: ignore[attr-defined]
            time.sleep(data.delay_seconds)
            self.wfile.write(data.data)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *_args: object) -> None:
        pass


@contextmanager
def release_server(
    files: dict[str, bytes | Redirect | StreamingBody | DelayedBody],
) -> Iterator[tuple[str, FixtureServer]]:
    server = FixtureServer(("127.0.0.1", 0), FixtureHandler)
    server.files = files
    server.requests = []
    server.bytes_sent = {}
    server.response_started = threading.Event()
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
    base_url: str | None,
    tmp_path: Path,
    *arguments: str,
    system: str = "Linux",
    machine: str = "x86_64",
    path: str | None = None,
) -> subprocess.CompletedProcess[str]:
    environment = installer_environment(
        base_url,
        tmp_path,
        system=system,
        machine=machine,
        path=path,
    )
    return subprocess.run(
        ["/bin/sh", str(INSTALLER), *arguments],
        env=environment,
        capture_output=True,
        text=True,
        timeout=20,
    )


def installer_environment(
    base_url: str | None,
    tmp_path: Path,
    *,
    system: str = "Linux",
    machine: str = "x86_64",
    path: str | None = None,
) -> dict[str, str]:
    tools = fake_uname(tmp_path / "tools", system, machine)
    environment = os.environ.copy()
    environment.update(
        {
            "HOME": str(tmp_path / "home"),
            "PATH": f"{tools}{os.pathsep}{path or os.environ['PATH']}",
            "LC_ALL": "C",
        }
    )
    if base_url is None:
        environment.pop("NETFT_CLI_RELEASE_BASE_URL", None)
    else:
        environment["NETFT_CLI_RELEASE_BASE_URL"] = base_url
    return environment


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
        "cp",
        "grep",
        "gzip",
        "ln",
        "mkdir",
        "mktemp",
        "mv",
        "readlink",
        "rm",
        "sha256sum",
        "tar",
        "wc",
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


def test_install_sh_refuses_directory_destination(tmp_path: Path) -> None:
    destination = tmp_path / "bin"
    installed = destination / "netft"
    installed.mkdir(parents=True)
    marker = installed / "marker"
    marker.write_text("preserve", encoding="utf-8")

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
    assert marker.read_text(encoding="utf-8") == "preserve"


def test_install_sh_refuses_symlink_installation_directory(
    tmp_path: Path,
) -> None:
    real_destination = tmp_path / "real-bin"
    real_destination.mkdir()
    destination = tmp_path / "bin"
    destination.symlink_to(real_destination, target_is_directory=True)

    with release_server(fixture_files()) as (base_url, server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(destination),
        )

    assert result.returncode != 0
    assert server.requests == []
    assert list(real_destination.iterdir()) == []


def test_install_sh_does_not_remove_foreign_lock_symlink(
    tmp_path: Path,
) -> None:
    destination = tmp_path / "bin"
    destination.mkdir()
    foreign = tmp_path / "foreign-lock"
    foreign.mkdir()
    lock = destination / ".netft-install.lock"
    lock.symlink_to(foreign, target_is_directory=True)

    with release_server(fixture_files()) as (base_url, server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(destination),
        )

    assert result.returncode != 0
    assert server.requests == []
    assert lock.is_symlink()
    assert foreign.is_dir()
    assert list(foreign.iterdir()) == []


def test_install_sh_rejects_oversized_uninstalled_member_and_preserves_binary(
    tmp_path: Path,
) -> None:
    archive = release_archive("0.1.0", oversized_license=True)
    destination = tmp_path / "bin"
    destination.mkdir()
    old = destination / "netft"
    old.write_bytes(fake_binary("0.0.9"))
    old.chmod(0o755)

    with release_server(fixture_files(archive=archive)) as (base_url, _server):
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


def test_install_sh_lock_contention_preserves_previous_binary(
    tmp_path: Path,
) -> None:
    destination = tmp_path / "bin"
    destination.mkdir()
    (destination / ".netft-install.lock").mkdir()
    old = destination / "netft"
    old.write_bytes(fake_binary("0.0.9"))
    old.chmod(0o755)

    with release_server(fixture_files()) as (base_url, server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(destination),
        )

    assert result.returncode != 0
    assert server.requests == []
    assert old.read_bytes() == fake_binary("0.0.9")


def test_install_sh_cleanup_preserves_externally_recreated_lock_symlink(
    tmp_path: Path,
) -> None:
    files = fixture_files()
    checksum_path = "/releases/download/v0.1.0/SHA256SUMS"
    files[checksum_path] = DelayedBody(files[checksum_path])
    destination = tmp_path / "bin"
    destination.mkdir()

    with release_server(files) as (base_url, server):
        process = subprocess.Popen(
            [
                "/bin/sh",
                str(INSTALLER),
                "--version",
                "0.1.0",
                "--bin-dir",
                str(destination),
            ],
            env=installer_environment(base_url, tmp_path),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        assert server.response_started.wait(timeout=5)
        lock = destination / ".netft-install.lock"
        assert lock.is_symlink()
        own_token = os.readlink(lock)
        assert "/" not in own_token
        assert not (destination / own_token).exists()
        lock.unlink()
        lock.symlink_to("foreign-owner-token")
        stdout, stderr = process.communicate(timeout=20)

    assert process.returncode == 0, stdout + stderr
    assert lock.is_symlink()
    assert os.readlink(lock) == "foreign-owner-token"


@pytest.mark.parametrize(
    "base_url",
    [
        "https://127.0.0.1:443/releases",
        "http://127.0.0.1:0/releases",
        "http://127.0.0.1:65536/releases",
        "http://127.0.0.1:999999/releases",
        "http://user@127.0.0.1:1234/releases",
        "http://127.0.0.1:1234/releases#fragment",
    ],
)
def test_release_base_override_is_validated_before_creating_destination(
    tmp_path: Path, base_url: str
) -> None:
    destination = tmp_path / "bin"

    result = run_installer(
        base_url,
        tmp_path,
        "--version",
        "0.1.0",
        "--bin-dir",
        str(destination),
    )

    assert result.returncode != 0
    assert not destination.exists()


def restricted_tool_path(directory: Path, *, include_wget: bool = True) -> Path:
    directory.mkdir()
    commands = [
        "awk",
        "chmod",
        "cmp",
        "cp",
        "grep",
        "gzip",
        "ln",
        "mkdir",
        "mktemp",
        "mv",
        "readlink",
        "rm",
        "sha256sum",
        "tar",
        "wc",
    ]
    if include_wget:
        commands.append("wget")
    for command in commands:
        executable = shutil.which(command)
        assert executable is not None
        (directory / command).symlink_to(executable)
    return directory


@pytest.mark.parametrize(
    "location_template",
    [
        "http://localhost:{port}/escaped",
        "http://127.0.0.2:{port}/escaped",
        "http://[::1]:{port}/escaped",
        "https://example.com/escaped",
    ],
)
def test_wget_loopback_fixture_rejects_redirect_escape(
    tmp_path: Path, location_template: str
) -> None:
    restricted = restricted_tool_path(tmp_path / "restricted")
    files: dict[str, bytes | Redirect] = {}
    with release_server(files) as (base_url, server):
        port = server.server_address[1]
        files["/releases/download/v0.1.0/SHA256SUMS"] = Redirect(
            location_template.format(port=port)
        )
        files["/escaped"] = checksum_file(
            release_inventory("0.1.0", "linux-x86_64", b"unused")
        )
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(tmp_path / "bin"),
            path=str(restricted),
        )

    assert result.returncode != 0
    assert server.requests == ["/releases/download/v0.1.0/SHA256SUMS"]


@pytest.mark.parametrize(
    "redirect_location",
    [
        "http://example.test/release",
        "https://user@example.com/release",
        "https://example.com/release#fragment",
        "https://example.com%2fevil.test/release",
        "https://example.com/release\x01",
    ],
)
def test_wget_production_rejects_unsafe_redirect_before_following(
    tmp_path: Path, redirect_location: str
) -> None:
    restricted = restricted_tool_path(
        tmp_path / "restricted",
        include_wget=False,
    )
    marker = tmp_path / "followed-http"
    wget = restricted / "wget"
    wget.write_text(
        "#!/bin/sh\n"
        f"marker='{marker}'\n"
        "for argument in \"$@\"; do\n"
        "  if [ \"$argument\" = '--max-redirect=0' ]; then\n"
        "    printf '  HTTP/1.1 302 Found\\n' >&2\n"
        f"    printf '  Location: {redirect_location}\\n' >&2\n"
        "    exit 8\n"
        "  fi\n"
        "done\n"
        ": >\"$marker\"\n"
        "exit 1\n",
        encoding="utf-8",
    )
    wget.chmod(0o755)

    result = run_installer(
        None,
        tmp_path,
        "--version",
        "0.1.0",
        "--bin-dir",
        str(tmp_path / "bin"),
        path=str(restricted),
    )

    assert result.returncode != 0
    assert not marker.exists()


@pytest.mark.parametrize("header_name", ["location", "lOcAtIoN"])
def test_real_wget_accepts_case_insensitive_loopback_location_header(
    tmp_path: Path, header_name: str
) -> None:
    restricted = restricted_tool_path(tmp_path / "restricted")
    archive = release_archive("0.1.0")
    checksums = checksum_file(
        release_inventory("0.1.0", "linux-x86_64", archive)
    )
    name = asset_name("0.1.0", "linux-x86_64")
    files: dict[str, bytes | Redirect | StreamingBody] = {}
    with release_server(files) as (base_url, server):
        files["/releases/download/v0.1.0/SHA256SUMS"] = Redirect(
            f"{base_url}/redirected/SHA256SUMS",
            header_name=header_name,
        )
        files["/releases/redirected/SHA256SUMS"] = checksums
        files[f"/releases/download/v0.1.0/{name}"] = archive
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
    assert server.requests[:2] == [
        "/releases/download/v0.1.0/SHA256SUMS",
        "/releases/redirected/SHA256SUMS",
    ]


def test_wget_accepts_authentic_production_multi_redirect_shape(
    tmp_path: Path,
) -> None:
    restricted = restricted_tool_path(
        tmp_path / "restricted",
        include_wget=False,
    )
    archive = tmp_path / "release.tar.gz"
    archive.write_bytes(release_archive("0.1.0"))
    checksums = tmp_path / "SHA256SUMS"
    checksums.write_bytes(
        checksum_file(
            release_inventory(
                "0.1.0",
                "linux-x86_64",
                archive.read_bytes(),
            )
        )
    )
    request_log = tmp_path / "wget-requests"
    wget = restricted / "wget"
    wget.write_text(
        "#!/bin/sh\n"
        "output=\n"
        "url=\n"
        "for argument in \"$@\"; do\n"
        "  case \"$argument\" in\n"
        "    --output-document=*) output=${argument#*=} ;;\n"
        "    http://*|https://*) url=$argument ;;\n"
        "  esac\n"
        "done\n"
        f"printf '%s\\n' \"$url\" >>'{request_log}'\n"
        "case \"$url\" in\n"
        "  https://github.com/netft/netft-cli/releases/download/v0.1.0/SHA256SUMS)\n"
        "    printf '  HTTP/1.1 100 Continue\\r\\n' >&2\n"
        "    printf '  location: https://download.test/stale\\r\\n' >&2\n"
        "    printf '  HTTP/1.1 302 Found\\r\\n' >&2\n"
        "    printf '  lOcAtIoN: https://download.test/first\\r\\n' >&2\n"
        "    printf 'Location: https://download.test/first [following]\\n' >&2\n"
        "    exit 8 ;;\n"
        "  https://download.test/first)\n"
        "    printf '  HTTP/2 302\\r\\n' >&2\n"
        "    printf '  location: https://download.test/final?sig=a%%2Fb\\r\\n' >&2\n"
        "    printf 'Location: https://download.test/final?sig=a%%2Fb [following]\\n' >&2\n"
        "    exit 8 ;;\n"
        "  https://download.test/final?sig=a%2Fb)\n"
        "    printf '  HTTP/2 200\\r\\n' >&2\n"
        f"    cp '{checksums}' \"$output\"; exit 0 ;;\n"
        "  *netft-cli-0.1.0-linux-x86_64.tar.gz)\n"
        "    printf '  HTTP/1.1 200 OK\\r\\n' >&2\n"
        f"    cp '{archive}' \"$output\"; exit 0 ;;\n"
        "esac\n"
        "exit 9\n",
        encoding="utf-8",
    )
    wget.chmod(0o755)

    result = run_installer(
        None,
        tmp_path,
        "--version",
        "0.1.0",
        "--bin-dir",
        str(tmp_path / "bin"),
        path=str(restricted),
    )

    assert result.returncode == 0, (
        result.stderr + "\n" + request_log.read_text(encoding="utf-8")
    )


@pytest.mark.parametrize("downloader", ["curl", "wget"])
def test_download_limit_stops_unbounded_checksum_body_during_transfer(
    tmp_path: Path, downloader: str
) -> None:
    body = StreamingBody(8 * 1024 * 1024)
    files: dict[str, bytes | Redirect | StreamingBody] = {
        "/releases/download/v0.1.0/SHA256SUMS": body
    }
    destination = tmp_path / "bin"
    destination.mkdir()
    old = destination / "netft"
    old.write_bytes(fake_binary("0.0.9"))
    old.chmod(0o755)
    tool_path = (
        str(restricted_tool_path(tmp_path / "restricted"))
        if downloader == "wget"
        else None
    )

    with release_server(files) as (base_url, server):
        result = run_installer(
            base_url,
            tmp_path,
            "--version",
            "0.1.0",
            "--bin-dir",
            str(destination),
            path=tool_path,
        )

    assert result.returncode != 0
    assert server.bytes_sent[
        "/releases/download/v0.1.0/SHA256SUMS"
    ] < body.size
    assert old.read_bytes() == fake_binary("0.0.9")
