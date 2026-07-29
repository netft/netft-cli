from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "scripts" / "install" / "install.ps1"
NATIVE_TEST = ROOT / "test" / "artifact" / "install_ps1_test.ps1"


def test_powershell_installer_exposes_only_the_public_install_interface() -> None:
    parameter_block = INSTALLER.read_text(encoding="utf-8").split(
        "Set-StrictMode", maxsplit=1
    )[0]

    assert "$Version" in parameter_block
    assert "$BinDir" in parameter_block
    assert "$NoModifyPath" in parameter_block
    assert "ReleaseBase" not in parameter_block


def test_native_powershell_test_uses_production_binary_and_isolated_seams() -> None:
    source = NATIVE_TEST.read_text(encoding="utf-8")

    assert "NETFT_EXECUTABLE" in source
    assert "Add-Type -TypeDefinition" not in source
    assert "ConsoleApplication" not in source
    assert "127.0.0.1" in source
    assert "NETFT_CLI_TEST_USER_PATH_FILE" in source
    assert "EnvironmentVariableTarget]::User" not in source
