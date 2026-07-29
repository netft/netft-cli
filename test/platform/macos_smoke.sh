#!/usr/bin/env sh
set -eu

repository_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${NETFT_PLATFORM_BUILD_DIR:-"${repository_root}/build/platform-macos"}

cmake -S "${repository_root}" -B "${build_dir}" -G Ninja -DBUILD_TESTING=ON
cmake --build "${build_dir}"
ctest --test-dir "${build_dir}" --output-on-failure
