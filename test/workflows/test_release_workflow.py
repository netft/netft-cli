from __future__ import annotations

import importlib.util
import re
import tomllib
from pathlib import Path
from types import ModuleType
from typing import Any, Iterator

import pytest
import yaml


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_DIRECTORY = ROOT / ".github" / "workflows"
TARGETS = {
    "linux-x86_64": "ubuntu-24.04",
    "linux-arm64": "ubuntu-24.04-arm",
    "macos-x86_64": "macos-15-intel",
    "macos-arm64": "macos-15",
    "windows-x86_64": "windows-2025",
}
ACTION_REVISIONS = {
    "actions/checkout": "3d3c42e5aac5ba805825da76410c181273ba90b1",
    "prefix-dev/setup-pixi": "a09b6247153796b190642a2b53fac4241043cf6f",
    "actions/upload-artifact": "043fb46d1a93c77aae656e7c1c64a875d1fc6a0a",
    "actions/download-artifact": "3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c",
    "actions/attest-build-provenance": (
        "0f67c3f4856b2e3261c31976d6725780e5e4c373"
    ),
    "github/codeql-action": "e4fba868fa4b1b91e1fdab776edc8cfbe6e9fb81",
    "codecov/codecov-action": "fb8b3582c8e4def4969c97caa2f19720cb33a72f",
}
OBSOLETE_ACTION_OBJECTS = {
    "adfda868f108ac4222129de456ea554034a27db7",
    "a99c28d3f0da835de33ff2feb2e15691c7b9641f",
}


def load_yaml(path: Path) -> dict[str, Any]:
    assert path.is_file(), f"missing YAML file: {path.relative_to(ROOT)}"
    document = yaml.safe_load(path.read_text(encoding="utf-8"))
    assert isinstance(document, dict)
    return document


def workflow_trigger(workflow: dict[str, Any]) -> Any:
    # PyYAML implements YAML 1.1 and therefore parses the plain key `on` as True.
    return workflow.get("on", workflow.get(True))


def iter_steps(workflow: dict[str, Any]) -> Iterator[dict[str, Any]]:
    for job in workflow.get("jobs", {}).values():
        for step in job.get("steps", []):
            if isinstance(step, dict):
                yield step


def commands(job: dict[str, Any]) -> str:
    return "\n".join(
        str(step.get("run", ""))
        for step in job.get("steps", [])
        if isinstance(step, dict)
    )


def matrix_targets(job: dict[str, Any]) -> dict[str, str]:
    includes = job["strategy"]["matrix"]["include"]
    return {entry["target"]: entry["runner"] for entry in includes}


def recursively_contains_key(value: Any, key: str) -> bool:
    if isinstance(value, dict):
        return key in value or any(
            recursively_contains_key(child, key) for child in value.values()
        )
    if isinstance(value, list):
        return any(recursively_contains_key(child, key) for child in value)
    return False


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


def assert_pwsh_fail_fast(script: str) -> None:
    error_preference = '$ErrorActionPreference = "Stop"'
    native_preference = "$PSNativeCommandUseErrorActionPreference = $true"
    assert error_preference in script
    assert native_preference in script
    first_command = min(
        (
            position
            for token in ("cmake", "ctest", "gh ", "python", "netft", "vcpkg")
            if (position := script.find(token)) >= 0
        ),
        default=len(script),
    )
    assert script.index(error_preference) < first_command
    assert script.index(native_preference) < first_command
    commands = logical_pwsh_commands(script)
    critical_indexes = [
        index
        for index, command in enumerate(commands)
        if re.search(
            r"^(?:cmake|ctest|gh|python|pwsh|vcpkg)\b"
            r"|^&\s+.*netft(?:\.exe)?\b"
            r"|=\s*git\b",
            command,
        )
    ]
    if critical_indexes:
        assert "function Assert-NativeSuccess" in script
    for index in critical_indexes:
        assert index + 1 < len(commands)
        assert commands[index + 1] == "Assert-NativeSuccess"


def logical_pwsh_commands(script: str) -> list[str]:
    commands: list[str] = []
    continued: list[str] = []
    here_string = False
    for raw_line in script.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if here_string:
            continued.append(line)
            if line == "'@":
                commands.append(" ".join(continued))
                continued = []
                here_string = False
            continue
        if line.endswith("@'"):
            continued.append(line)
            here_string = True
            continue
        if line.endswith("`"):
            continued.append(line[:-1].rstrip())
            continue
        if continued:
            continued.append(line)
            commands.append(" ".join(continued))
            continued = []
            continue
        commands.append(line)
    assert not continued
    assert not here_string
    return commands


@pytest.mark.parametrize("name", ["ci", "codeql", "coverage", "release"])
def test_workflow_defaults_are_read_only_and_avoid_untrusted_privilege(
    name: str,
) -> None:
    workflow = load_yaml(WORKFLOW_DIRECTORY / f"{name}.yml")

    assert workflow["permissions"] == {"contents": "read"}
    trigger = workflow_trigger(workflow)
    assert isinstance(trigger, dict)
    assert "pull_request_target" not in trigger


def test_every_external_action_is_pinned_and_checkout_drops_credentials() -> None:
    observed: set[str] = set()
    for path in sorted(WORKFLOW_DIRECTORY.glob("*.yml")):
        workflow = load_yaml(path)
        for step in iter_steps(workflow):
            action = step.get("uses")
            if action is None or str(action).startswith("./"):
                continue
            match = re.fullmatch(r"([^@]+)@([0-9a-f]{40})", str(action))
            assert match is not None, f"action is not pinned: {action}"
            owner, revision = match.groups()
            root_action = (
                "github/codeql-action"
                if owner.startswith("github/codeql-action/")
                else owner
            )
            assert ACTION_REVISIONS[root_action] == revision
            assert revision not in OBSOLETE_ACTION_OBJECTS
            observed.add(root_action)
            if owner == "actions/checkout":
                assert step.get("with", {}).get("persist-credentials") is False

    assert observed == set(ACTION_REVISIONS)


def test_native_ci_covers_exact_platforms_and_required_native_evidence() -> None:
    workflow = load_yaml(WORKFLOW_DIRECTORY / "ci.yml")
    native = workflow["jobs"]["native"]

    assert matrix_targets(native) == TARGETS
    script = commands(native)
    assert "cmake --build" in script
    assert "ctest --test-dir" in script
    assert "--version" in script
    assert "pixi run check" in script
    assert "test/artifact/install_ps1_test.ps1" in script
    assert "netft.exe" in script
    assert "test/artifact/install_sh_test.py" in script
    assert any(
        "NETFT_EXECUTABLE" in step.get("env", {})
        for step in native["steps"]
        if isinstance(step, dict)
    )


def test_every_powershell_workflow_step_enables_native_fail_fast() -> None:
    powershell_steps = []
    for path in sorted(WORKFLOW_DIRECTORY.glob("*.yml")):
        workflow = load_yaml(path)
        powershell_steps.extend(
            step
            for step in iter_steps(workflow)
            if step.get("shell") == "pwsh" and "run" in step
        )

    assert powershell_steps
    for step in powershell_steps:
        assert_pwsh_fail_fast(str(step["run"]))


def test_powershell_contract_rejects_late_success_overwriting_failure() -> None:
    unsafe = """\
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true
ctest --test-dir build/native
netft.exe --version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
"""

    with pytest.raises(AssertionError):
        assert_pwsh_fail_fast(unsafe)


def test_windows_installer_suite_runs_in_a_fresh_powershell_process() -> None:
    workflow = load_yaml(WORKFLOW_DIRECTORY / "ci.yml")
    native = workflow["jobs"]["native"]
    installer_step = next(
        step
        for step in native["steps"]
        if "NETFT_EXECUTABLE" in step.get("env", {})
    )
    script = str(installer_step["run"])

    assert (
        "pwsh -NoProfile -File test/artifact/install_ps1_test.ps1"
        in script
    )
    assert "& test/artifact/install_ps1_test.ps1" not in script
    assert "Assert-NativeSuccess" in script


def test_linux_complete_check_includes_workflow_contracts() -> None:
    pixi = tomllib.loads((ROOT / "pixi.toml").read_text(encoding="utf-8"))

    assert "test/workflows" in pixi["tasks"]["workflow-check"]
    assert "workflow-check" in pixi["tasks"]["check"]["depends-on"]


def test_codeql_is_scheduled_and_uses_manual_cpp_build() -> None:
    workflow = load_yaml(WORKFLOW_DIRECTORY / "codeql.yml")
    trigger = workflow_trigger(workflow)
    assert trigger["schedule"]
    analyze = workflow["jobs"]["analyze"]
    init = next(
        step
        for step in analyze["steps"]
        if str(step.get("uses", "")).startswith("github/codeql-action/init@")
    )

    assert init["with"]["languages"] == "c-cpp"
    assert init["with"]["build-mode"] == "manual"
    assert "cmake --build" in commands(analyze)
    assert analyze["permissions"] == {
        "contents": "read",
        "security-events": "write",
    }


def test_coverage_uses_gcovr_oidc_and_has_no_percentage_gate() -> None:
    workflow = load_yaml(WORKFLOW_DIRECTORY / "coverage.yml")
    coverage = workflow["jobs"]["coverage"]
    upload = next(
        step
        for step in coverage["steps"]
        if str(step.get("uses", "")).startswith("codecov/codecov-action@")
    )
    config = load_yaml(ROOT / ".codecov.yml")

    assert "gcovr" in commands(coverage)
    assert "coverage.xml" in commands(coverage)
    assert upload["with"]["files"] == "coverage.xml"
    assert upload["with"]["use_oidc"] is True
    assert upload["with"]["fail_ci_if_error"] is True
    assert coverage["permissions"] == {"contents": "read", "id-token": "write"}
    assert not recursively_contains_key(config, "target")
    for category in ("project", "patch"):
        assert config["coverage"]["status"][category]["default"]["informational"] is True


def test_release_validates_authorized_annotated_tag_and_version_sources() -> None:
    workflow = load_yaml(WORKFLOW_DIRECTORY / "release.yml")
    validate = workflow["jobs"]["validate"]
    script = commands(validate)

    assert workflow_trigger(workflow)["push"]["tags"]
    assert "git cat-file -t" in script
    assert "verify-tag --raw" in script
    assert "gpg.format=ssh" in script
    assert "gpg.ssh.allowedSignersFile=.github/release-allowed-signers" in script
    assert "42986190+han-xudong@users.noreply.github.com" in script
    assert "git merge-base --is-ancestor" in script
    assert "refs/remotes/origin/main" in script
    assert "CMakeLists.txt" in script
    assert "CHANGELOG.md" in script

    allowed_signers = (
        ROOT / ".github" / "release-allowed-signers"
    ).read_text(encoding="utf-8")
    assert allowed_signers.split() == [
        "42986190+han-xudong@users.noreply.github.com",
        'namespaces="git"',
        "ssh-ed25519",
        "AAAAC3NzaC1lZDI1NTE5AAAAIBAK7PQxd5cAgT56rIeKrImhyzreNULEoRT3DWmJTIvv",
    ]


def test_release_builds_exact_static_native_inventory() -> None:
    workflow = load_yaml(WORKFLOW_DIRECTORY / "release.yml")
    build = workflow["jobs"]["build"]
    assemble = workflow["jobs"]["assemble"]
    build_script = commands(build)
    assemble_script = commands(assemble)

    assert matrix_targets(build) == TARGETS
    assert build["needs"] == "validate"
    assert "tools/build_static_curl.sh" in build_script
    assert "tools/build_windows_curl.ps1" in build_script
    assert "CURL_USE_STATIC_LIBS=ON" in build_script
    assert "tools/package_release.py" in build_script
    assert "tools/check_release.py" in build_script
    assert "--allow-partial" in build_script
    assert "validate_inventory" in assemble_script
    assert "SHA256SUMS" in assemble_script
    assert assemble["needs"] == "build"


def test_release_dag_attests_drafts_smokes_all_installers_then_publishes() -> None:
    workflow = load_yaml(WORKFLOW_DIRECTORY / "release.yml")
    jobs = workflow["jobs"]

    assert jobs["attest"]["needs"] == "assemble"
    assert jobs["attest"]["permissions"] == {
        "attestations": "write",
        "contents": "read",
        "id-token": "write",
    }
    assert jobs["draft_release"]["needs"] == ["assemble", "attest"]
    assert jobs["draft_release"]["permissions"] == {"contents": "write"}
    assert jobs["draft_release"]["outputs"]["release_id"]
    assert "--draft" in commands(jobs["draft_release"])
    draft_script = commands(jobs["draft_release"])
    assert "asset-ids" in draft_script
    assert "--method DELETE" in draft_script
    assert "validate-remote" in draft_script
    assert "validate-draft" in draft_script
    assert "--expected-release-id" in draft_script
    assert "releases/tags/" in draft_script
    assert "-F prerelease=false" in draft_script
    assert matrix_targets(jobs["smoke"]) == TARGETS
    assert jobs["smoke"]["needs"] == "draft_release"
    smoke_script = commands(jobs["smoke"])
    assert "gh release download" in smoke_script
    assert "--pattern" not in smoke_script
    assert "validate-remote" in smoke_script
    assert "--expected-release-id" in smoke_script
    assert "releases/tags/" in smoke_script
    assert "scripts/install/install.sh" in smoke_script
    assert "scripts/install/install.ps1" in smoke_script
    assert (
        "NETFT_CLI_RELEASE_BASE_URL=http://127.0.0.1:49152/releases"
        in smoke_script
    )
    assert '"http://127.0.0.1:49152/releases"' in smoke_script
    assert jobs["publish"]["needs"] == ["smoke", "draft_release"]
    assert jobs["publish"]["permissions"] == {"contents": "write"}
    assert "validate-remote" in commands(jobs["publish"])
    assert "--expected-release-id" in commands(jobs["publish"])
    assert "--prerelease=false" in commands(jobs["publish"])
    assert "releases/tags/" in commands(jobs["publish"])
    assert (
        'gh release edit "${GITHUB_REF_NAME}" \\\n'
        '  --repo "${GITHUB_REPOSITORY}" \\\n'
        "  --draft=false"
    ) in commands(jobs["publish"])


def test_only_final_release_publication_uses_protected_environment() -> None:
    workflow = load_yaml(WORKFLOW_DIRECTORY / "release.yml")
    jobs = workflow["jobs"]
    protected_jobs: dict[str, str] = {}

    for name, job in jobs.items():
        environment = job.get("environment")
        if isinstance(environment, dict):
            environment = environment.get("name")
        if environment is not None:
            protected_jobs[name] = environment

    assert protected_jobs == {"publish": "netft-release"}
    assert jobs["publish"]["needs"] == ["smoke", "draft_release"]


def test_remote_release_inventory_requires_exact_draft_assets() -> None:
    release_inventory = load_release_inventory_module()
    names = sorted(release_inventory.expected_asset_names("0.1.0"))
    metadata = {
        "id": 41,
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
    changelog = """\
# Changelog

## 0.1.0 - 2026-07-30

### Added

- A release note that starts on one line and
  continues as a normal sentence without retaining the source wrap.
- A second release note.

## 0.0.1 - 2026-07-01

- Older.
"""

    notes = release_notes.extract_release_notes(changelog, "0.1.0")

    assert (
        "- A release note that starts on one line and continues as a normal "
        "sentence without retaining the source wrap."
    ) in notes.splitlines()
    assert "## 0.0.1" not in notes


def test_release_notes_do_not_insert_fixed_width_breaks() -> None:
    release_notes = load_release_notes_module()
    continuation = "words " * 30
    changelog = (
        "## 0.1.0 - 2026-07-30\n\n"
        f"- This is deliberately long and\n  {continuation.strip()}.\n"
    )

    notes = release_notes.extract_release_notes(changelog, "0.1.0")

    bullet_lines = [line for line in notes.splitlines() if line.startswith("- ")]
    assert len(bullet_lines) == 1
    assert len(bullet_lines[0]) > 120
