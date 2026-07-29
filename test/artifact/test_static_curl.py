from __future__ import annotations

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_unix_builder_revalidates_a_cached_archive(tmp_path: Path) -> None:
    archive = tmp_path / "curl.tar.xz"
    archive.write_bytes(b"not the pinned curl source")
    environment = os.environ.copy()
    environment["NETFT_CLI_CURL_PREFIX"] = str(tmp_path / "prefix")
    environment["NETFT_CLI_CURL_ARCHIVE_CACHE"] = str(archive)

    completed = subprocess.run(
        ["bash", str(ROOT / "tools" / "build_static_curl.sh")],
        env=environment,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 1
    assert not (tmp_path / "prefix").exists()
