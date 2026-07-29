#!/usr/bin/env python3
"""Validate the exact remote asset state of a netft CLI draft release."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


TARGET_EXTENSIONS = {
    "linux-x86_64": ".tar.gz",
    "linux-arm64": ".tar.gz",
    "macos-x86_64": ".tar.gz",
    "macos-arm64": ".tar.gz",
    "windows-x86_64": ".zip",
}
VERSION_PATTERN = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")


class ReleaseInventoryError(RuntimeError):
    """Remote release state does not satisfy the publication contract."""


def expected_asset_names(version: str) -> set[str]:
    if VERSION_PATTERN.fullmatch(version) is None:
        raise ReleaseInventoryError("invalid release version")
    archives = {
        f"netft-cli-{version}-{target}{extension}"
        for target, extension in TARGET_EXTENSIONS.items()
    }
    return {*archives, "SHA256SUMS"}


def draft_release_id(metadata: Any) -> int:
    """Return the numeric REST ID for an existing draft of either kind."""
    if not isinstance(metadata, dict):
        raise ReleaseInventoryError("release metadata is invalid")
    release_id = metadata.get("id")
    if (
        not isinstance(release_id, int)
        or isinstance(release_id, bool)
        or release_id <= 0
        or metadata.get("draft") is not True
        or not isinstance(metadata.get("prerelease"), bool)
    ):
        raise ReleaseInventoryError("release is not a valid draft")
    return release_id


def _validate_draft_identity(metadata: Any, expected_release_id: int) -> None:
    release_id = draft_release_id(metadata)
    if (
        not isinstance(expected_release_id, int)
        or isinstance(expected_release_id, bool)
        or expected_release_id <= 0
        or release_id != expected_release_id
    ):
        raise ReleaseInventoryError("release identity changed")
    if metadata.get("prerelease") is not False:
        raise ReleaseInventoryError("release is a prerelease")


def _validated_assets(
    metadata: Any, expected_release_id: int
) -> list[dict[str, Any]]:
    _validate_draft_identity(metadata, expected_release_id)
    if not isinstance(metadata, dict):
        raise ReleaseInventoryError("release is not a draft")
    assets = metadata.get("assets")
    if not isinstance(assets, list):
        raise ReleaseInventoryError("release asset metadata is invalid")
    validated: list[dict[str, Any]] = []
    seen_ids: set[int] = set()
    for asset in assets:
        if not isinstance(asset, dict):
            raise ReleaseInventoryError("release asset metadata is invalid")
        asset_id = asset.get("id")
        name = asset.get("name")
        if (
            not isinstance(asset_id, int)
            or isinstance(asset_id, bool)
            or asset_id <= 0
            or not isinstance(name, str)
            or not name
            or asset_id in seen_ids
        ):
            raise ReleaseInventoryError("release asset metadata is invalid")
        seen_ids.add(asset_id)
        validated.append(asset)
    return validated


def validate_draft(metadata: Any, expected_release_id: int) -> None:
    """Require the same stable draft identity without constraining assets."""
    _validate_draft_identity(metadata, expected_release_id)
    _validated_assets(metadata, expected_release_id)


def draft_asset_ids(metadata: Any, expected_release_id: int) -> list[int]:
    """Return IDs only after proving that their release is still a draft."""
    return [
        asset["id"] for asset in _validated_assets(metadata, expected_release_id)
    ]


def validate_remote_inventory(
    metadata: Any, version: str, expected_release_id: int
) -> None:
    """Require one exact six-file asset inventory on a draft release."""
    assets = _validated_assets(metadata, expected_release_id)
    names = [asset["name"] for asset in assets]
    expected = expected_asset_names(version)
    if len(names) != len(set(names)):
        raise ReleaseInventoryError("release contains duplicate asset names")
    if set(names) != expected:
        raise ReleaseInventoryError("release asset inventory is not exact")


def _load_metadata(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ReleaseInventoryError("unable to read release metadata") from error


def main() -> int:
    parser = argparse.ArgumentParser()
    subcommands = parser.add_subparsers(dest="command", required=True)
    asset_ids = subcommands.add_parser("asset-ids")
    asset_ids.add_argument("--metadata", type=Path, required=True)
    asset_ids.add_argument("--expected-release-id", type=int, required=True)
    release_id = subcommands.add_parser("release-id")
    release_id.add_argument("--metadata", type=Path, required=True)
    draft = subcommands.add_parser("validate-draft")
    draft.add_argument("--metadata", type=Path, required=True)
    draft.add_argument("--expected-release-id", type=int, required=True)
    validate = subcommands.add_parser("validate-remote")
    validate.add_argument("--metadata", type=Path, required=True)
    validate.add_argument("--version", required=True)
    validate.add_argument("--expected-release-id", type=int, required=True)
    arguments = parser.parse_args()

    try:
        metadata = _load_metadata(arguments.metadata)
        if arguments.command == "asset-ids":
            for asset_id in draft_asset_ids(
                metadata, arguments.expected_release_id
            ):
                print(asset_id)
        elif arguments.command == "release-id":
            print(draft_release_id(metadata))
        elif arguments.command == "validate-draft":
            validate_draft(metadata, arguments.expected_release_id)
        else:
            validate_remote_inventory(
                metadata, arguments.version, arguments.expected_release_id
            )
    except ReleaseInventoryError as error:
        raise SystemExit(f"release inventory validation failed: {error}") from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
