"""Behavioral tests for the controlled upstream snapshot tool."""

from __future__ import annotations

import subprocess
import shutil
from pathlib import Path

import pytest

from tools import sync_core


def run(*args: str, cwd: Path) -> str:
    return subprocess.run(
        args,
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def make_tagged_fixture(path: Path, tag: str = "v0.3.0") -> Path:
    path.mkdir(parents=True)
    run("git", "init", cwd=path)
    run("git", "config", "user.name", "Test User", cwd=path)
    run("git", "config", "user.email", "test@example.com", cwd=path)
    (path / "LICENSE").write_text("Apache License\n", encoding="utf-8")
    (path / "include" / "netft").mkdir(parents=True)
    (path / "include" / "netft" / "client.hpp").write_text(
        "#pragma once\n", encoding="utf-8"
    )
    (path / "src" / "detail").mkdir(parents=True)
    (path / "src" / "detail" / "protocol.cpp").write_text(
        "int protocol() { return 0; }\n", encoding="utf-8"
    )
    (path / "app").mkdir()
    (path / "app" / "main.cpp").write_text("int main() {}\n", encoding="utf-8")
    run("git", "add", ".", cwd=path)
    run("git", "commit", "-m", "fixture", cwd=path)
    run("git", "tag", tag, cwd=path)
    run("git", "remote", "add", "origin", "https://github.com/netft/netft-cpp.git", cwd=path)
    return path


def make_exact_base_fixture(path: Path) -> Path:
    repository = Path(__file__).parents[2]
    shutil.copytree(repository / "core" / "netft", path)
    run("git", "init", cwd=path)
    run(
        "git",
        "apply",
        "--reverse",
        "--unidiff-zero",
        str(repository / "core" / "ADAPTATIONS.patch"),
        cwd=path,
    )
    run("git", "config", "user.name", "Test User", cwd=path)
    run("git", "config", "user.email", "test@example.com", cwd=path)
    run("git", "add", ".", cwd=path)
    run("git", "commit", "-m", "exact upstream fixture", cwd=path)
    run("git", "tag", "v0.3.0", cwd=path)
    run("git", "remote", "add", "origin", sync_core.REQUIRED_REPOSITORY, cwd=path)
    return path


def synchronized_fixture(path: Path) -> Path:
    destination = path / "core"
    shutil.copytree(Path(__file__).parents[2] / "core", destination)
    return destination


def test_sync_records_exact_release_plus_explicit_adaptation_and_selected_paths(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source = make_exact_base_fixture(tmp_path / "source")
    destination = tmp_path / "core"
    monkeypatch.setattr(
        sync_core, "REQUIRED_COMMIT", run("git", "rev-parse", "HEAD", cwd=source), raising=False
    )

    sync_core.sync(source, destination, "v0.3.0")

    metadata = sync_core.read_upstream(destination / "UPSTREAM")
    assert metadata["tag"] == "v0.3.0"
    assert metadata["paths"] == "LICENSE,include,src"
    assert metadata["commit"] == run("git", "rev-parse", "HEAD", cwd=source)
    assert metadata["repository"] == "https://github.com/netft/netft-cpp.git"
    assert metadata["adaptation_path"] == "ADAPTATIONS.patch"
    assert metadata["adaptation_format"] == "git-diff-unified-zero"
    assert metadata["adaptation_sha256"] == sync_core.digest(destination / "ADAPTATIONS.patch")
    assert not (destination / "netft" / "app").exists()
    sync_core.verify(destination)


def test_sync_replays_adaptation_from_exact_base_to_current_tree(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    repository = Path(__file__).parents[2]
    source = make_exact_base_fixture(tmp_path / "source")
    monkeypatch.setattr(
        sync_core, "REQUIRED_COMMIT", run("git", "rev-parse", "HEAD", cwd=source)
    )

    destination = tmp_path / "core"
    sync_core.sync(source, destination, "v0.3.0")

    expected = {
        path.relative_to(repository / "core" / "netft"): sync_core.digest(path)
        for path in sync_core.snapshot_files(repository / "core" / "netft")
    }
    actual = {
        path.relative_to(destination / "netft"): sync_core.digest(path)
        for path in sync_core.snapshot_files(destination / "netft")
    }
    assert actual == expected


def test_sync_rejects_dirty_source_tree(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source = make_tagged_fixture(tmp_path / "source")
    monkeypatch.setattr(
        sync_core, "REQUIRED_COMMIT", run("git", "rev-parse", "HEAD", cwd=source)
    )
    (source / "include" / "netft" / "client.hpp").write_text(
        "#pragma once\n// dirty\n", encoding="utf-8"
    )

    with pytest.raises(SystemExit, match="source repository is dirty"):
        sync_core.sync(source, tmp_path / "core", "v0.3.0")


def test_sync_rejects_source_head_that_differs_from_tag(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source = make_tagged_fixture(tmp_path / "source")
    monkeypatch.setattr(
        sync_core, "REQUIRED_COMMIT", run("git", "rev-parse", "HEAD", cwd=source)
    )
    (source / "README.md").write_text("next commit\n", encoding="utf-8")
    run("git", "add", "README.md", cwd=source)
    run("git", "commit", "-m", "after release", cwd=source)

    with pytest.raises(SystemExit, match="source HEAD does not match the requested tag"):
        sync_core.sync(source, tmp_path / "core", "v0.3.0")


def test_sync_rejects_unsupported_tag(tmp_path: Path) -> None:
    source = make_tagged_fixture(tmp_path / "source", tag="v0.3.1")

    with pytest.raises(SystemExit, match="unsupported upstream tag"):
        sync_core.sync(source, tmp_path / "core", "v0.3.1")


def test_sync_rejects_required_tag_at_an_unapproved_commit(tmp_path: Path) -> None:
    source = make_tagged_fixture(tmp_path / "source")

    with pytest.raises(SystemExit, match="source tag does not match the required commit"):
        sync_core.sync(source, tmp_path / "core", "v0.3.0")


def test_verify_rejects_changed_snapshot_file(tmp_path: Path) -> None:
    destination = synchronized_fixture(tmp_path)
    header = destination / "netft" / "include" / "netft" / "client.hpp"
    header.write_text(header.read_text(encoding="utf-8") + "\n", encoding="utf-8")

    with pytest.raises(SystemExit, match="checksum mismatch"):
        sync_core.verify(destination)


def test_verify_rejects_changed_adaptation_patch(tmp_path: Path) -> None:
    destination = synchronized_fixture(tmp_path)
    patch = destination / "ADAPTATIONS.patch"
    patch.write_text(patch.read_text(encoding="utf-8") + "\n", encoding="utf-8")

    with pytest.raises(SystemExit, match="adaptation checksum mismatch"):
        sync_core.verify(destination)


@pytest.mark.parametrize(
    ("field", "replacement"),
    [
        ("repository", "https://example.invalid/netft-cpp.git"),
        ("tag", "v0.3.1"),
        ("commit", "0" * 40),
        ("paths", "LICENSE,include,app"),
        ("adaptation_path", "LOCAL.patch"),
        ("adaptation_format", "unified-diff"),
        ("adaptation_sha256", "0" * 64),
    ],
)
def test_verify_rejects_tampered_upstream_metadata(
    tmp_path: Path, field: str, replacement: str
) -> None:
    destination = synchronized_fixture(tmp_path)
    upstream = destination / "UPSTREAM"
    metadata = sync_core.read_upstream(upstream)
    upstream.write_text(
        upstream.read_text(encoding="utf-8").replace(
            f"{field}={metadata[field]}", f"{field}={replacement}"
        ),
        encoding="utf-8",
    )

    with pytest.raises(SystemExit, match="invalid upstream provenance"):
        sync_core.verify(destination)
