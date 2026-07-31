# Contributing to netft-cli

Bug reports, hardware compatibility reports, documentation corrections, tests, and focused code changes are welcome. Security vulnerabilities must follow [SECURITY.md](SECURITY.md) instead of the public issue tracker.

## Development environment

Install [Pixi](https://pixi.sh/), clone the repository, and create the locked environment:

```bash
git clone https://github.com/netft/netft-cli.git
cd netft-cli
pixi install
```

Run the complete local gate with:

```bash
pixi run check
pixi run sanitizers
```

`pixi run check` verifies the core snapshot, formatting, native build and tests, static analysis, and GitHub workflows. `pixi run sanitizers` rebuilds and runs the suite with AddressSanitizer and UndefinedBehaviorSanitizer.

For a focused native test:

```bash
pixi run configure
pixi run cmake --build build --target netft_diagnostic_criteria_test
pixi run ctest --test-dir build -R netft_diagnostic_criteria --output-on-failure
```

The process-level integration tests use local fake HTTP and UDP sensors:

```bash
pixi run cmake --build build --target netft
NETFT_EXECUTABLE="$PWD/build/netft" pixi run pytest -q test/integration/process_test.py
```

Ordinary automated tests must never require or contact physical hardware.

## Testing expectations

Tests should verify typed behavior, protocols, serializers, exit categories, resource ownership, and other machine-readable contracts. Prefer deterministic fake-sensor or injected-boundary tests for time, interruption, queue pressure, writer failure, and network behavior.

Do not freeze README, changelog, release-note, help, diagnostic, or terminal prose in tests. Help and completion tests should inspect semantic inventory and registration behavior rather than complete output snapshots. Add tests for observable behavior and defects, not for implementation details or coverage percentage alone.

Machine-readable output is a public interface. Preserve the meaning and types of existing fields within a schema version. An intentional incompatible change requires a new schema version, structural parser tests, documentation, and a changelog entry.

## Hardware testing is opt-in

Physical-sensor tests require explicit approval from the person responsible for the sensor and test area. Pass the host only in the environment for the individual command:

```bash
NETFT_SENSOR_HOST=<sensor-host> pixi run hardware-test
```

This read-only check discovers the configuration and monitors a bounded stream. Before running it, verify the sensor identity, calibration, fixture state, network path, and expected units.

Bias testing is a separate state-changing operation and requires both an explicit sensor host and the dedicated bias task:

```bash
NETFT_SENSOR_HOST=<sensor-host> pixi run hardware-bias-test
```

The dedicated task supplies the second `NETFT_ALLOW_BIAS=1` gate to the hardware harness. Select it only after obtaining fresh authorization for that run, unloading or safely fixturing the sensor, stopping hazardous motion, and keeping people clear. Authorization must not be reused between runs.

Never commit a laboratory sensor address, credentials, recordings, or private network details. Public examples use only ATI's documented factory-default address.

## Core synchronization

The private core snapshot comes from [netft-cpp](https://github.com/netft/netft-cpp). The current synchronization command accepts only `netft-cpp` tag `v0.3.1` at commit `859eeda8b077093f9bc49d9c1e5506c334647e7b`; it applies the checksum-pinned `core/ADAPTATIONS.patch` to reproduce the existing adapted snapshot.

To reproduce the current snapshot from a clean checkout at that exact tag:

```bash
python tools/sync_core.py sync --source <path-to-netft-cpp> --tag v0.3.1
python tools/sync_core.py verify
```

Do not edit the generated `core/netft` tree manually. Shared core behavior changes must be accepted in `netft-cpp` first; the adaptation patch is limited to reviewed CLI-specific integration and must not bypass that upstream-first rule.

A core upgrade is a maintainer-controlled change. The maintainer updates the pinned tag and commit in `tools/sync_core.py` and the corresponding tests, rebuilds and audits `core/ADAPTATIONS.patch` against that exact upstream base, and updates its pinned SHA-256. The synchronization command then regenerates `core/netft`, `core/UPSTREAM`, and `core/MANIFEST.sha256`; notices and snapshot tests are updated and the complete snapshot verification is run before the files are committed together.

## Pull requests

Keep a pull request limited to one coherent change. Describe the user-visible outcome, tests run, affected platforms, sensor model and firmware when relevant, and any core or release-asset impact. Remove exact private addresses and sensitive data from commands, logs, and attachments.

Ordinary pull requests do not create tags, draft releases, or release assets. Release automation is run by maintainers from a reviewed commit through the protected, signed-tag workflow.

By contributing, you agree that your contribution is licensed under the [Apache License 2.0](LICENSE).
