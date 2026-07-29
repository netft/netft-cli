from pathlib import Path


def test_windows_smoke_wraps_every_native_command() -> None:
    script = Path(__file__).with_name("windows_smoke.ps1").read_text(encoding="utf-8")
    lines = [line.strip() for line in script.splitlines()]
    invocations = [
        line for line in lines if line.startswith("Invoke-NativeChecked -FilePath ")
    ]
    bare_commands = [
        line
        for line in lines
        if line.startswith("cmake ") or line.startswith("ctest ")
    ]

    assert len(invocations) == 3
    assert not bare_commands
    assert "$LASTEXITCODE" in script
    assert "exit $ExitCode" in script
