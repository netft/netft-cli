from __future__ import annotations

import importlib.util
from pathlib import Path
from types import ModuleType

import pytest


ROOT = Path(__file__).resolve().parents[2]


def load_release_notes_module() -> ModuleType:
    path = ROOT / "tools" / "release_notes.py"
    assert path.is_file()
    spec = importlib.util.spec_from_file_location("release_notes", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_release_inventory_module() -> ModuleType:
    path = ROOT / "tools" / "release_inventory.py"
    assert path.is_file()
    spec = importlib.util.spec_from_file_location("release_inventory", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_remote_release_inventory_requires_exact_draft_assets() -> None:
    release_inventory = load_release_inventory_module()
    names = sorted(release_inventory.expected_asset_names("0.1.0"))
    metadata = {
        "id": 41,
        "name": "v0.1.0",
        "tag_name": "v0.1.0",
        "draft": True,
        "prerelease": False,
        "assets": [
            {"id": index + 1, "name": name}
            for index, name in enumerate(names)
        ],
    }

    release_inventory.validate_remote_inventory(
        metadata, "0.1.0", expected_release_id=41
    )

    for changed in (
        {**metadata, "draft": False},
        {**metadata, "prerelease": True},
        {key: value for key, value in metadata.items() if key != "prerelease"},
        {**metadata, "id": 42},
        {**metadata, "name": "v9.9.9"},
        {**metadata, "tag_name": "v9.9.9"},
        {**metadata, "tag_name": "untagged-"},
        {key: value for key, value in metadata.items() if key != "tag_name"},
        {**metadata, "assets": metadata["assets"][:-1]},
        {
            **metadata,
            "assets": [
                *metadata["assets"],
                {"id": 99, "name": "stale-extra.zip"},
            ],
        },
        {
            **metadata,
            "assets": [
                *metadata["assets"],
                {"id": 99, "name": metadata["assets"][0]["name"]},
            ],
        },
    ):
        with pytest.raises(release_inventory.ReleaseInventoryError):
            release_inventory.validate_remote_inventory(
                changed, "0.1.0", expected_release_id=41
            )

    release_inventory.validate_remote_inventory(
        {**metadata, "tag_name": "untagged-draftidentity"},
        "0.1.0",
        expected_release_id=41,
    )


def test_remote_asset_downloads_are_bound_to_validated_asset_ids() -> None:
    release_inventory = load_release_inventory_module()
    names = sorted(release_inventory.expected_asset_names("0.1.0"))
    metadata = {
        "id": 41,
        "name": "v0.1.0",
        "tag_name": "v0.1.0",
        "draft": True,
        "prerelease": False,
        "assets": [
            {"id": 100 + index, "name": name}
            for index, name in enumerate(reversed(names))
        ],
    }

    assert release_inventory.remote_asset_downloads(
        metadata, "0.1.0", expected_release_id=41
    ) == sorted(
        (
            (100 + index, name)
            for index, name in enumerate(reversed(names))
        ),
        key=lambda item: item[1],
    )

    with pytest.raises(release_inventory.ReleaseInventoryError):
        release_inventory.remote_asset_downloads(
            {**metadata, "id": 42}, "0.1.0", expected_release_id=41
        )
    with pytest.raises(release_inventory.ReleaseInventoryError):
        release_inventory.remote_asset_downloads(
            {**metadata, "tag_name": "v0.1.1"},
            "0.1.0",
            expected_release_id=41,
        )


def test_draft_cleanup_returns_only_valid_asset_ids() -> None:
    release_inventory = load_release_inventory_module()
    metadata = {
        "id": 41,
        "draft": True,
        "prerelease": False,
        "assets": [
            {"id": 17, "name": "stale"},
            {"id": 23, "name": "older"},
        ],
    }

    assert release_inventory.draft_asset_ids(
        metadata, expected_release_id=41
    ) == [17, 23]

    with pytest.raises(release_inventory.ReleaseInventoryError):
        release_inventory.draft_asset_ids(
            {**metadata, "draft": False}, expected_release_id=41
        )

    malformed = {
        **metadata,
        "assets": [
            {"id": "17", "name": "outside"}
        ],
    }
    with pytest.raises(release_inventory.ReleaseInventoryError):
        release_inventory.draft_asset_ids(
            malformed, expected_release_id=41
        )


def test_existing_draft_identity_can_be_normalized_from_prerelease() -> None:
    release_inventory = load_release_inventory_module()
    existing = {
        "id": 41,
        "draft": True,
        "prerelease": True,
        "assets": [],
    }

    assert release_inventory.draft_release_id(existing) == 41
    with pytest.raises(release_inventory.ReleaseInventoryError):
        release_inventory.draft_release_id({**existing, "draft": False})


def test_release_notes_join_wrapped_bullet_continuations() -> None:
    release_notes = load_release_notes_module()
    first = "alpha beta"
    continuation = "gamma delta"
    changelog = """\
# Changelog

## 0.1.0 - 2026-07-30

### Added

- alpha beta
  gamma delta.
- epsilon.

## 0.0.1 - 2026-07-01

- zeta.
"""

    notes = release_notes.extract_release_notes(changelog, "0.1.0")

    assert f"- {first} {continuation}." in notes.splitlines()
    assert "## 0.0.1" not in notes


def test_release_notes_do_not_insert_fixed_width_breaks() -> None:
    release_notes = load_release_notes_module()
    continuation = "token " * 30
    changelog = (
        "## 0.1.0 - 2026-07-30\n\n"
        f"- alpha\n  {continuation.strip()}.\n"
    )

    notes = release_notes.extract_release_notes(changelog, "0.1.0")

    bullet_lines = [line for line in notes.splitlines() if line.startswith("- ")]
    assert len(bullet_lines) == 1
    assert len(bullet_lines[0]) > 120


def test_stable_version_rules_match_installer_contract() -> None:
    from tools.package_release import VERSION_PATTERN as package_pattern
    from tools.release_inventory import expected_asset_names, ReleaseInventoryError
    for version in ("0.0.0", "0.2.2", "10.20.30"):
        assert package_pattern.fullmatch(version)
        assert len(expected_asset_names(version)) == 6
    for version in ("00.2.1", "0.02.1", "0.2.01", "0.2.1-rc.1", "v0.2.1", "0.2.1+build"):
        assert not package_pattern.fullmatch(version)
        with pytest.raises(ReleaseInventoryError):
            expected_asset_names(version)
