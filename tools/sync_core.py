"""Synchronize and verify the vendored netft-cpp core snapshot."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
from pathlib import Path
from typing import Sequence

REQUIRED_REPOSITORY = "https://github.com/netft/netft-cpp.git"
REQUIRED_TAG = "unreleased"
REQUIRED_COMMIT = "91f012c5d6f9b63902765ccbec3437cb286c15e1"
SELECTED = ("LICENSE", "include", "src")
ADAPTATION_NAME = "ADAPTATIONS.patch"
ADAPTATION_FORMAT = "git-diff-unified-zero"
REQUIRED_ADAPTATION_SHA256 = "4b2030500ea052b8958f3713e04079c8571cf44d8027e3e73f3b52b1e031cf44"


def canonical_adaptation() -> Path:
    return Path(__file__).resolve().parents[1] / "core" / ADAPTATION_NAME


def required_metadata() -> dict[str, str]:
    return {
        "repository": REQUIRED_REPOSITORY,
        "tag": REQUIRED_TAG,
        "commit": REQUIRED_COMMIT,
        "paths": ",".join(SELECTED),
        "adaptation_path": ADAPTATION_NAME,
        "adaptation_format": ADAPTATION_FORMAT,
        "adaptation_sha256": REQUIRED_ADAPTATION_SHA256,
    }


def git(source: Path, *args: str) -> str:
    """Run git in *source* and return its stripped standard output."""
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=source,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as error:
        message = error.stderr.strip() or error.stdout.strip() or "git command failed"
        raise SystemExit(message) from error
    return completed.stdout.strip()


def replace_selected_tree(source: Path, target: Path, selected: Sequence[str]) -> None:
    """Replace *target* with the explicitly selected paths from *source*."""
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)

    for relative_path in selected:
        source_path = source / relative_path
        if not source_path.exists():
            raise SystemExit(f"source path is missing: {relative_path}")
        destination_path = target / relative_path
        if source_path.is_dir():
            shutil.copytree(source_path, destination_path)
        else:
            shutil.copy2(source_path, destination_path)


def write_upstream(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(f"{key}={value}\n" for key, value in required_metadata().items()), encoding="utf-8"
    )


def read_upstream(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError as error:
        raise SystemExit(f"missing upstream metadata: {path}") from error

    metadata: dict[str, str] = {}
    for line in lines:
        key, separator, value = line.partition("=")
        if not separator or not key or not value:
            raise SystemExit("invalid upstream metadata")
        metadata[key] = value

    required = {
        "repository",
        "tag",
        "commit",
        "paths",
        "adaptation_path",
        "adaptation_format",
        "adaptation_sha256",
    }
    if set(metadata) != required:
        raise SystemExit("invalid upstream metadata")
    return metadata


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def snapshot_files(root: Path) -> list[Path]:
    return sorted((path for path in root.rglob("*") if path.is_file()), key=lambda path: path.as_posix())


def apply_adaptation(destination: Path, patch: Path) -> None:
    """Apply the declared git-diff adaptation to the exact copied snapshot."""
    worktree = subprocess.run(
        ["git", "-C", str(destination), "rev-parse", "--show-toplevel"],
        check=False,
        capture_output=True,
        text=True,
    )
    if worktree.returncode == 0:
        working_directory = Path(worktree.stdout.strip())
        patch_directory = (destination / "netft").relative_to(working_directory)
    else:
        working_directory = destination
        patch_directory = Path("netft")
    try:
        subprocess.run(
            [
                "git",
                "apply",
                "--unidiff-zero",
                f"--directory={patch_directory.as_posix()}",
                str(patch.resolve()),
            ],
            cwd=working_directory,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as error:
        message = error.stderr.strip() or error.stdout.strip() or "adaptation patch failed"
        raise SystemExit(message) from error


def write_manifest(root: Path, manifest: Path) -> None:
    if not root.is_dir():
        raise SystemExit(f"missing snapshot tree: {root}")
    records = [f"{digest(path)}  {path.relative_to(root).as_posix()}" for path in snapshot_files(root)]
    manifest.write_text("\n".join(records) + ("\n" if records else ""), encoding="utf-8")


def read_manifest(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError as error:
        raise SystemExit(f"missing checksum manifest: {path}") from error

    records: dict[str, str] = {}
    for line in lines:
        checksum, separator, relative_path = line.partition("  ")
        if not separator or len(checksum) != 64 or not relative_path:
            raise SystemExit("invalid checksum manifest")
        if relative_path in records:
            raise SystemExit("invalid checksum manifest")
        records[relative_path] = checksum
    return records


def verify(destination: Path = Path("core")) -> None:
    """Verify upstream provenance, the declared adaptation, and the adapted-tree manifest."""
    if read_upstream(destination / "UPSTREAM") != required_metadata():
        raise SystemExit("invalid upstream provenance")
    adaptation = destination / ADAPTATION_NAME
    if not adaptation.is_file() or digest(adaptation) != REQUIRED_ADAPTATION_SHA256:
        raise SystemExit("adaptation checksum mismatch")
    root = destination / "netft"
    expected = read_manifest(destination / "MANIFEST.sha256")
    actual = {
        path.relative_to(root).as_posix(): digest(path)
        for path in snapshot_files(root)
    }
    if expected != actual:
        raise SystemExit("checksum mismatch")


def sync(source: Path, destination: Path, tag: str) -> None:
    """Copy the exact tagged upstream core, apply the declared adaptation, and record provenance."""
    source = source.resolve()
    destination = destination.resolve()
    if tag != REQUIRED_TAG:
        raise SystemExit("unsupported upstream tag")
    if git(source, "remote", "get-url", "origin") != REQUIRED_REPOSITORY:
        raise SystemExit("source repository does not match the required repository")
    try:
        commit = git(source, "rev-parse", f"{REQUIRED_COMMIT if tag == 'unreleased' else tag}^{{commit}}")
    except SystemExit as error:
        raise SystemExit("source tag does not match the required commit") from error
    if commit != REQUIRED_COMMIT:
        raise SystemExit("source tag does not match the required commit")
    if git(source, "rev-parse", "HEAD") != commit:
        raise SystemExit("source HEAD does not match the requested tag")
    if git(source, "status", "--porcelain"):
        raise SystemExit("source repository is dirty")
    adaptation = canonical_adaptation()
    if not adaptation.is_file() or digest(adaptation) != REQUIRED_ADAPTATION_SHA256:
        raise SystemExit("canonical adaptation checksum mismatch")
    replace_selected_tree(source, destination / "netft", SELECTED)
    destination.mkdir(parents=True, exist_ok=True)
    copied_adaptation = destination / ADAPTATION_NAME
    if adaptation.resolve() != copied_adaptation.resolve():
        shutil.copy2(adaptation, copied_adaptation)
    apply_adaptation(destination, copied_adaptation)
    write_upstream(destination / "UPSTREAM")
    write_manifest(destination / "netft", destination / "MANIFEST.sha256")
    verify(destination)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)

    sync_parser = subcommands.add_parser("sync", help="copy a tagged upstream snapshot")
    sync_parser.add_argument("--source", type=Path, required=True)
    identity = sync_parser.add_mutually_exclusive_group(required=True)
    identity.add_argument("--tag")
    identity.add_argument("--commit", help="exact pinned unpublished candidate commit")
    sync_parser.add_argument("--destination", type=Path, default=Path("core"))

    verify_parser = subcommands.add_parser("verify", help="verify the local snapshot manifest")
    verify_parser.add_argument("--destination", type=Path, default=Path("core"))
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    if arguments.command == "sync":
        if arguments.commit is not None and arguments.commit != REQUIRED_COMMIT:
            raise SystemExit("unsupported upstream commit")
        sync(arguments.source, arguments.destination, arguments.tag or "unreleased")
    else:
        verify(arguments.destination)


if __name__ == "__main__":
    main()
