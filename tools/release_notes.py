#!/usr/bin/env python3
"""Extract one release section from the changelog without hard wrapping."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def extract_release_notes(changelog: str, version: str) -> str:
    """Return the dated changelog section for *version* as release notes."""
    section = re.search(
        rf"(?ms)^## {re.escape(version)} - \d{{4}}-\d{{2}}-\d{{2}}\n"
        rf"(?P<body>.*?)(?=^## |\Z)",
        changelog,
    )
    if section is None:
        raise ValueError(f"CHANGELOG.md has no dated release section for {version}")

    rendered: list[str] = []
    current_bullet: str | None = None
    for raw_line in section.group("body").splitlines():
        if raw_line.startswith("- "):
            if current_bullet is not None:
                rendered.append(current_bullet)
            current_bullet = raw_line.rstrip()
            continue
        if (
            current_bullet is not None
            and raw_line[:1].isspace()
            and raw_line.strip()
        ):
            current_bullet += " " + raw_line.strip()
            continue
        if current_bullet is not None:
            rendered.append(current_bullet)
            current_bullet = None
        rendered.append(raw_line.rstrip())
    if current_bullet is not None:
        rendered.append(current_bullet)

    notes = "\n".join(rendered).strip()
    if not notes:
        raise ValueError(f"CHANGELOG.md release section for {version} is empty")
    return notes + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--changelog", type=Path, default=Path("CHANGELOG.md"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        notes = extract_release_notes(
            arguments.changelog.read_text(encoding="utf-8"),
            arguments.version,
        )
    except (OSError, ValueError) as error:
        raise SystemExit(f"release notes validation failed: {error}") from error
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(notes, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
