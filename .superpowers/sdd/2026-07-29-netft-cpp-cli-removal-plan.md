# netft-cpp CLI Removal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the legacy bundled CLI from `netft-cpp` only after `netft-cli 0.1.0` is publicly available and direct users to the standalone repository.

**Architecture:** `netft-cpp` remains the canonical C++ SDK and core source. The executable, command parser, CLI tests, and CLI build option are deleted together; library APIs, package exports, and downstream private snapshots remain unchanged.

**Tech Stack:** C++17, CMake 3.16+, GoogleTest, Pixi, GitHub Actions.

## Global Constraints

- Do not begin until `https://github.com/netft/netft-cli/releases/tag/v0.1.0` is public and all five required native assets and both installers have passed smoke tests.
- Repository content is English-only.
- Do not change public `netft-cpp` library headers or protocol behavior.
- Do not update pyNetFT, ros-netft, or netft-viewer snapshots in this plan.
- Do not delete unrelated user changes or development branches.
- Use a new `netft-cpp 0.4.0` minor release for removal of the pre-1.0 bundled executable.
- Keep CMake 3.16 and C++17 compatibility.
- Tests must verify build/package structure, not README or diagnostic prose.

---

### Task 1: Prove the standalone replacement is available

**Files:**
- No repository modifications.

**Interfaces:**
- Consumes: public `netft-cli v0.1.0` release.
- Produces: recorded read-only evidence authorizing removal work.

- [ ] **Step 1: Inspect the public release**

```bash
gh release view v0.1.0 --repo netft/netft-cli \
  --json isDraft,tagName,assets,url
```

Expected: `isDraft` is false and the assets contain Linux x86_64/arm64, macOS x86_64/arm64, Windows x86_64, and `SHA256SUMS`.

- [ ] **Step 2: Smoke-test the current-platform installer**

Use an isolated destination:

```bash
temporary_bin="$(mktemp -d)"
curl -fsSL \
  https://raw.githubusercontent.com/netft/netft-cli/main/scripts/install/install.sh \
  | sh -s -- --version 0.1.0 --bin-dir "${temporary_bin}"
"${temporary_bin}/netft" --version
```

Expected: output identifies `netft-cli 0.1.0`; no system path is modified.

- [ ] **Step 3: Stop if the replacement contract is incomplete**

Do not modify `netft-cpp` when any required asset, installer smoke test, or public release check fails.

### Task 2: Remove the bundled executable without changing the SDK

**Files:**
- Delete: `app/cli.cpp`
- Delete: `app/cli.hpp`
- Delete: `app/main.cpp`
- Delete: `test/test_cli.cpp`
- Modify: `CMakeLists.txt`
- Modify: `test/CMakeLists.txt`
- Modify: `pixi.toml`
- Modify: `test/install_test.sh`
- Modify: `test/gcc10_compatibility_test.sh`
- Modify: `test/cmake_compatibility_test.sh`

**Interfaces:**
- Removes: `NETFT_BUILD_CLI`, `netft_cli_lib`, `netft_cli`, and installed `bin/netft`.
- Preserves: `netft`, `netft::netft`, installed headers, CMake package files, shared/static consumer behavior, and every core test target.

- [ ] **Step 1: Add an SDK-only install assertion**

Update `test/install_test.sh` to require the library, headers, and CMake config and to fail if `${prefix}/bin/netft` exists:

```bash
test -f "${prefix}/${library_path}"
test -f "${prefix}/include/netft/client.hpp"
test -f "${prefix}/${cmake_path}/netftConfig.cmake"
test ! -e "${prefix}/bin/netft"
```

- [ ] **Step 2: Run the test and verify it fails before removal**

```bash
pixi run install-test
```

Expected: failure because the current package still installs `bin/netft`.

- [ ] **Step 3: Delete CLI targets and sources**

Remove the `NETFT_BUILD_CLI` option, the guarded `netft_cli_lib` and `netft_cli` target definitions, and the guarded executable install rule from `CMakeLists.txt`. Remove CLI-only tests and task filters; retain every library source and target unchanged.

- [ ] **Step 4: Run SDK compatibility tests**

```bash
pixi run check
pixi run shared-test
pixi run static-test
pixi run -e cmake-316 cmake-316-test
pixi run gcc-10-test
```

Expected: all pass and no installed executable exists.

- [ ] **Step 5: Commit**

```bash
git add -A app test CMakeLists.txt pixi.toml
git commit -m "refactor: move CLI to standalone repository"
```

### Task 3: Document the separation and prepare the SDK release

**Files:**
- Modify: `README.md`
- Modify: `CONTRIBUTING.md`
- Modify: `CHANGELOG.md`
- Modify: `CMakeLists.txt`
- Modify: `pixi.toml`
- Modify: `.github/workflows/release.yml`
- Modify: release-version tests that currently expect `0.3.0`.

**Interfaces:**
- Produces: release-ready `netft-cpp 0.4.0` source SDK.
- Consumes: Task 2 SDK-only build.

- [ ] **Step 1: Update reader documentation**

Remove bundled-CLI installation and command examples. In the README's current
command-line-tool section, replace the removed content with one concise
sentence linking `https://github.com/netft/netft-cli` for prebuilt terminal
tooling. Keep the README focused on the C++ library.

- [ ] **Step 2: Update contributor boundaries**

Document that protocol, transport, discovery, recovery, and configuration work remains in `netft-cpp`, while terminal interaction and command behavior belong in `netft-cli`. Do not add a release-process section to the README.

- [ ] **Step 3: Set version `0.4.0` and changelog entry**

Set the CMake and Pixi versions to `0.4.0`. Add a dated CHANGELOG entry explaining that the CLI moved to its own repository and that the C++ API is unchanged. Release notes must use natural Markdown without fixed-width hard wrapping.

- [ ] **Step 4: Run complete verification**

```bash
pixi run check
pixi run shared-test
pixi run static-test
git diff --check
git status --short
```

Expected: all checks pass; only intentional SDK-removal and documentation changes remain.

- [ ] **Step 5: Commit**

```bash
git add README.md CONTRIBUTING.md CHANGELOG.md CMakeLists.txt pixi.toml \
  .github test
git commit -m "chore: prepare netft-cpp 0.4.0"
```

- [ ] **Step 6: Stop before release publication**

Request explicit user approval before merging, pushing a rewritten branch, creating the signed `v0.4.0` tag, or publishing the GitHub Release.
