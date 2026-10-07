# netft-cli

[![CI](https://github.com/netft/netft-cli/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/netft/netft-cli/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/netft/netft-cli?display_name=tag&sort=semver)](https://github.com/netft/netft-cli/releases)
[![CodeQL](https://github.com/netft/netft-cli/actions/workflows/codeql.yml/badge.svg?branch=main)](https://github.com/netft/netft-cli/actions/workflows/codeql.yml)
[![Coverage](https://codecov.io/gh/netft/netft-cli/graph/badge.svg?branch=main)](https://codecov.io/gh/netft/netft-cli)
[![License](https://img.shields.io/github/license/netft/netft-cli?label=license)](LICENSE)

`netft-cli` is a cross-platform command-line application for commissioning,
diagnosing, monitoring, recording, and biasing ATI Net F/T Ethernet sensors.
It discovers the active calibration and native units before consuming the RDT
force/torque stream.

## Features

- Inspect sensor identity, calibration, and measurement units.
- Check stream continuity, packet loss, rate, reconnects, and device status.
- Monitor the latest six-axis sample in a live table or structured stream.
- Record every accepted sample to CSV or NDJSON with integrity safeguards.
- Apply software bias through an explicit confirmation step.

## Installation

| Install method | Platform | Support |
| --- | --- | --- |
| [GitHub Releases](https://github.com/netft/netft-cli/releases) or installer | Linux x86-64/ARM64, macOS Intel/Apple silicon, Windows x86-64 | Self-contained executable |
| Source | Linux, macOS, Windows | CMake 3.16+, C++17, Threads, libcurl 7.63+ |

On Linux or macOS:

```bash
curl -fsSL https://raw.githubusercontent.com/netft/netft-cli/main/scripts/install/install.sh | sh
```

On Windows PowerShell:

```powershell
irm https://raw.githubusercontent.com/netft/netft-cli/main/scripts/install/install.ps1 | iex
```

The installers select and verify the latest stable release. Release binaries
include the private `netft-cpp` core and libcurl, so no separate runtime is
required.

## Quick start

The examples use `192.168.1.1`, the ATI factory-default sensor address. Replace
it with the address configured for your sensor.

```bash
# Inspect identity, calibration, and units.
netft info 192.168.1.1

# Run a bounded health check.
netft check 192.168.1.1 --duration 10s

# Monitor the latest sample in a live table.
netft monitor 192.168.1.1

# Record every accepted sample for one minute.
netft record 192.168.1.1 --output measurement.csv --duration 60s
```

Run `netft help <COMMAND>` or `netft <COMMAND> --help` for the options supported
by the installed version.

## Documentation

- [Inspect and validate a sensor](https://netft.dev/docs/tutorials/cli/inspect-and-validate)
- [Monitor live data](https://netft.dev/docs/tutorials/cli/monitor-live-data)
- [Record a capture](https://netft.dev/docs/tutorials/cli/record-a-capture)
- [Command reference](https://netft.dev/docs/references/cli/overview)
- [Data formats](https://netft.dev/docs/references/data-formats/csv)
- [Security and safety](https://netft.dev/docs/references/security-and-safety)

`monitor` presents the latest sample without maintaining a backlog. Use
`record` when every accepted sample must be preserved. Bias changes the
measurement zero; perform it only when the sensor and connected equipment are
in a safe state.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, tests,
hardware-safety requirements, and core synchronization. Report security issues
through [SECURITY.md](SECURITY.md).

## License

This project is licensed under the [Apache License 2.0](LICENSE). Release
archives include the notices for the synchronized `netft-cpp` core and libcurl.

### Recording integrity (unreleased candidate)

`record` never replaces existing destination or partial paths, including dangling links. The first sample and subsequent accepted samples must arrive within `--timeout`. A zero-sample session, terminal fault, queue overflow, writer failure or data timeout returns recording error 8 and retains `.partial`; inspect partial data before using it. Recoverable interruptions can resume within the timeout. `--count` can wait for the selected sample count while data keeps arriving; combine it with `--duration` for a total time limit. Ctrl-C drains nonempty recordings, finalizes them and returns 130. Publication protects file names but does not promise power-loss durability.

## Recording metadata (unreleased candidate)

After successful data finalization, the candidate writes `<output>.metadata.json` using schema version 1 and kind `netft-recording`. CLI and Viewer share these meanings:

| Field | Meaning |
| --- | --- |
| `producer`, `result` | `netft-cli` or `netft-viewer`; `complete` or CLI `interrupted` after a successful nonempty drain. |
| `accepted_samples`, `written_samples` | Samples accepted into the recording queue and rows successfully written. These are not power-loss durability guarantees. |
| `sample_span_seconds` | Nonnegative host receive-time span between first and last written sample; zero for fewer than two samples. |
| `configuration_revisions` | Unique revision identifiers in written rows, scoped to the producer session. They do not contain full calibration snapshots. |
| `force_units`, `torque_units` | Unique native unit symbols observed in written rows. |
| `recorded_rdt_gaps` | Sum of forward sequence gaps between written rows, using unsigned wraparound and ignoring backward jumps. Pauses, rate filtering and reconnects can contribute; this is not the SDK packet-loss counter. |
| `pause_count` | Accepted Viewer pauses; CLI has no pause operation and reports zero. |
| `reconnect_count` | CLI SDK reconnect counter for the run; `null` for Viewer because its recorder does not own connection health. |
| `error` | `null` on successful finalization. Failure details remain in the CLI error or Viewer recorder state. |

Partial or failed captures do not receive a completed summary. Metadata creation and promotion follow the data file's overwrite policy. The two files are finalized separately: a metadata failure reports an error and preserves the completed data file, and may leave a metadata partial for recovery. Keep both files when archiving a successful capture.

The CLI candidate appends `configuration_revision` to CSV without moving existing columns and adds it inside the NDJSON sample object. Viewer already records revisions in each CSV row. Existing published releases retain their documented formats until these changes are released.

## Machine interface (unreleased candidate)

`netft --schema` prints schema-version-1 JSON without opening a sensor connection. Commands refer to option identifiers, typed positionals and exit statuses from the same command definitions used by parsing and help. `version`, `sourceCommit` and `sourceDirty` identify the configured build. Archives with no Git metadata report unknown provenance; documentation generation requires a clean checkout and matching executable. Consumers must reject unsupported schema versions and should tolerate additive fields.
