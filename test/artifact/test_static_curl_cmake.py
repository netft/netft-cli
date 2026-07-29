from __future__ import annotations

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def write_header(prefix: Path) -> None:
    header = prefix / "include" / "curl" / "curl.h"
    header.parent.mkdir(parents=True)
    header.write_text("#pragma once\n", encoding="utf-8")


def configure_fixture(
    tmp_path: Path, prefix: Path, system_name: str = "Linux"
) -> subprocess.CompletedProcess[str]:
    source = tmp_path / "source"
    source.mkdir()
    (source / "FindCURL.cmake").write_text(
        """
if(NOT CURL_LIBRARY)
  set(CURL_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/fallback/libcurl.so")
endif()
add_library(CURL::libcurl UNKNOWN IMPORTED)
set_target_properties(CURL::libcurl PROPERTIES IMPORTED_LOCATION "${CURL_LIBRARY}")
set(CURL_FOUND TRUE)
set(CURL_VERSION_STRING "8.21.0")
""",
        encoding="utf-8",
    )
    (source / "CMakeLists.txt").write_text(
        f"""
cmake_minimum_required(VERSION 3.16)
project(static_curl_fixture LANGUAGES NONE)
list(PREPEND CMAKE_MODULE_PATH "${{CMAKE_CURRENT_SOURCE_DIR}}")
set(CURL_USE_STATIC_LIBS ON)
set(CURL_ROOT "{prefix.as_posix()}")
include("{(ROOT / 'cmake' / 'StaticCurl.cmake').as_posix()}")
netft_prepare_static_curl()
find_package(CURL 7.63.0 REQUIRED MODULE)
netft_lock_static_curl_target()
get_target_property(resolved CURL::libcurl IMPORTED_LOCATION)
file(WRITE "${{CMAKE_BINARY_DIR}}/resolved.txt" "${{resolved}}")
""",
        encoding="utf-8",
    )
    return subprocess.run(
        [
            "cmake",
            "-S",
            str(source),
            "-B",
            str(tmp_path / "build"),
            f"-DCMAKE_SYSTEM_NAME={system_name}",
        ],
        capture_output=True,
        text=True,
    )


def test_dynamic_only_prefix_is_rejected(tmp_path: Path) -> None:
    prefix = tmp_path / "curl"
    write_header(prefix)
    library = prefix / "lib" / "libcurl.so"
    library.parent.mkdir()
    library.write_bytes(b"dynamic")

    assert configure_fixture(tmp_path, prefix).returncode != 0


def test_mixed_unix_prefix_locks_target_to_static_archive(tmp_path: Path) -> None:
    prefix = tmp_path / "curl"
    write_header(prefix)
    library = prefix / "lib" / "libcurl.a"
    library.parent.mkdir()
    library.write_bytes(b"!<arch>\n")
    (prefix / "lib" / "libcurl.so").write_bytes(b"dynamic")

    completed = configure_fixture(tmp_path, prefix)

    assert completed.returncode == 0
    assert (tmp_path / "build" / "resolved.txt").read_text() == str(library)


def test_windows_import_style_library_is_rejected(tmp_path: Path) -> None:
    prefix = tmp_path / "curl"
    write_header(prefix)
    library = prefix / "lib" / "libcurl.lib"
    library.parent.mkdir()
    library.write_bytes(b"import")

    assert configure_fixture(tmp_path, prefix, "Windows").returncode != 0


def test_windows_explicit_static_library_is_accepted(tmp_path: Path) -> None:
    prefix = tmp_path / "curl"
    write_header(prefix)
    library = prefix / "lib" / "libcurl_a.lib"
    library.parent.mkdir()
    library.write_bytes(b"!<arch>\n")

    completed = configure_fixture(tmp_path, prefix, "Windows")

    assert completed.returncode == 0
    assert (tmp_path / "build" / "resolved.txt").read_text() == str(library)


def test_windows_mixed_static_and_import_libraries_are_rejected(tmp_path: Path) -> None:
    prefix = tmp_path / "curl"
    write_header(prefix)
    library_dir = prefix / "lib"
    library_dir.mkdir()
    (library_dir / "libcurl_a.lib").write_bytes(b"!<arch>\n")
    (library_dir / "libcurl.lib").write_bytes(b"!<arch>\n")

    assert configure_fixture(tmp_path, prefix, "Windows").returncode != 0


def test_static_filename_with_nonarchive_content_is_rejected(tmp_path: Path) -> None:
    prefix = tmp_path / "curl"
    write_header(prefix)
    library = prefix / "lib" / "libcurl.a"
    library.parent.mkdir()
    library.write_bytes(b"not an archive")

    assert configure_fixture(tmp_path, prefix).returncode != 0
