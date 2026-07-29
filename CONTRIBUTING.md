# Contributing to netft-cli

Bug reports, hardware compatibility reports, documentation corrections, tests, and focused code changes are welcome. Security vulnerabilities must follow [SECURITY.md](SECURITY.md) instead of the public issue tracker.

## Development environment

Install [Pixi](https://pixi.sh/), clone the repository, and create the locked environment:

```bash
git clone https://github.com/netft/netft-cli.git
cd netft-cli
pixi install
```

Use the repository tasks rather than relying on host tool versions:

```bash
pixi run check
pixi run sanitizers
```

`pixi run check` verifies the core snapshot, formatting, native build and tests, static analysis, and GitHub workflows. The ordinary integration suite uses local fake HTTP and UDP sensors; it must never require or contact physical hardware. Run `pixi run sanitizers` for native memory and undefined-behavior checks.

Tests should verify typed behavior, protocols, serializers, exit categories, and other machine-readable contracts. Do not freeze README, changelog, release-note, help, diagnostic, or terminal prose in tests.

## Hardware testing is opt-in

Physical-sensor tests require explicit approval from the person responsible for the sensor and test area. Provide the intended host only for the individual command:

```bash
NETFT_SENSOR_HOST=<sensor-host> pixi run hardware-test
```

This read-only check discovers the configuration and monitors a bounded stream. Before running it, verify the sensor identity, calibration, fixture state, network path, and expected units.

Bias testing is a separate state-changing operation and requires both an explicit sensor host and the dedicated bias task:

```bash
NETFT_SENSOR_HOST=<sensor-host> pixi run hardware-bias-test
```

The dedicated task supplies the second `NETFT_ALLOW_BIAS=1` gate to the hardware harness. Select it only after obtaining fresh authorization for that run, unloading or safely fixturing the sensor, stopping hazardous motion, and keeping people clear. Authorization must not be reused between runs.

Never commit a laboratory sensor address, credentials, or private network details. Public examples use only ATI's documented factory-default address.

## Core synchronization

The private core snapshot comes from [netft-cpp](https://github.com/netft/netft-cpp). Do not edit protocol, transport, discovery, recovery, or sensor-configuration files below `core/netft` in this repository.

Make core behavior changes upstream in `netft-cpp` first. After an upstream release is reviewed, synchronize from a clean checkout at the exact release tag:

```bash
python tools/sync_core.py sync --source <path-to-netft-cpp> --tag <release-tag>
python tools/sync_core.py verify
```

Commit the updated `core/UPSTREAM`, manifest, selected snapshot files, and notices together. A pull request must identify the upstream tag and commit and explain any CLI adaptation outside the snapshot.

## Pull requests

Keep a pull request limited to one coherent change. Describe the user-visible outcome, tests run, affected platforms, sensor model and firmware when relevant, and any core or release-asset impact. Remove exact private addresses and sensitive data from commands, logs, and attachments.

Ordinary pull requests do not create tags, draft releases, or release assets. Release automation is run by maintainers from a reviewed commit through the protected, signed-tag workflow.

By contributing, you agree that your contribution is licensed under the [Apache License 2.0](LICENSE).
