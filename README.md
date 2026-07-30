# netft-cli

[![CI](https://github.com/netft/netft-cli/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/netft/netft-cli/actions/workflows/ci.yml)
[![CodeQL](https://github.com/netft/netft-cli/actions/workflows/codeql.yml/badge.svg?branch=main)](https://github.com/netft/netft-cli/actions/workflows/codeql.yml)
[![Coverage](https://codecov.io/gh/netft/netft-cli/graph/badge.svg?branch=main)](https://codecov.io/gh/netft/netft-cli)
[![Release](https://img.shields.io/github/v/release/netft/netft-cli?display_name=tag&sort=semver&style=flat)](https://github.com/netft/netft-cli/releases)
[![License](https://img.shields.io/github/license/netft/netft-cli?label=license&style=flat)](LICENSE)

`netft-cli` is a standalone command-line application for commissioning, diagnosing, monitoring, recording, and biasing ATI Net F/T Ethernet sensors. It discovers the active calibration and native units from the sensor before consuming the RDT force/torque stream.

## Features

- Inspect sensor identity, calibration scales, and native measurement units.
- Diagnose configuration, stream continuity, packet loss, receive rate, reconnects, and sensor status.
- Monitor the latest six-axis sample in a live table or machine-readable stream.
- Record every accepted sample to CSV or NDJSON with explicit integrity handling.
- Apply software bias through an explicit confirmation step.

## Installation

| Install method | Platform | Support |
| --- | --- | --- |
| [GitHub Releases](https://github.com/netft/netft-cli/releases) or installer | Linux x86_64/ARM64, macOS Intel/Apple silicon, Windows x86_64 | Prebuilt, self-contained executable |
| Source | Linux, macOS, Windows | CMake 3.16+, C++17, Threads, libcurl 7.63+ |

On Linux or macOS:

```bash
curl -fsSL https://raw.githubusercontent.com/netft/netft-cli/main/scripts/install/install.sh | sh
```

On Windows PowerShell:

```powershell
irm https://raw.githubusercontent.com/netft/netft-cli/main/scripts/install/install.ps1 | iex
```

The installers select the latest stable release for the current platform and verify the downloaded archive. Release binaries contain the CLI, its private `netft-cpp` core, and libcurl; no separate runtime installation is required.

To build from source:

```bash
git clone https://github.com/netft/netft-cli.git
cd netft-cli
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --config Release
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the reproducible development environment and test workflow.

## Usage

The examples use `192.168.1.1`, the ATI factory-default sensor address. Replace it with the address configured for your sensor.

### Inspect configuration

```bash
netft info 192.168.1.1
netft info 192.168.1.1 --format json
```

`info` reports the sensor identity, calibration scales, counts, and native force and torque units without starting a measurement stream.

### Check sensor health

```bash
netft check 192.168.1.1
netft check 192.168.1.1 --duration 10s --min-rate 500 --max-loss 0.1
netft check 192.168.1.1 --format json
```

`check` observes a bounded stream for five seconds by default. Configuration discovery, first-sample acquisition, sustained streaming, and sensor status are mandatory checks. Packet loss is a warning unless `--max-loss` is specified; `--min-rate`, `--max-loss`, and `--max-reconnects` turn the corresponding limits into acceptance criteria. A failed criterion returns exit code `7`.

### Monitor live data

```bash
netft monitor 192.168.1.1
netft monitor 192.168.1.1 --format ndjson --rate 20
```

Interactive output uses a live table. Redirected output defaults to NDJSON; CSV is also available. `--rate` controls the presentation rate and defaults to 20 Hz. Monitoring continues until interrupted unless a bounded `--duration`, such as `10s`, is provided.

`monitor` always presents the latest available sample. It intentionally keeps no backlog, so it is suitable for observation but not for lossless capture.

### Record data

```bash
netft record 192.168.1.1 --output measurement.csv --duration 60s
netft record 192.168.1.1 --output measurement.ndjson --count 10000
```

`record` preserves every accepted sample through a bounded producer-consumer queue. The format is inferred from `.csv` or `.ndjson`, or can be selected explicitly with `--format`. A capture must have `--duration`, `--count`, or both.

The command refuses to replace an existing destination. It writes to `PATH.partial`, drains and flushes the queue, then renames the completed file to `PATH`. An interrupted capture is finalized and returns `130`; queue overflow or writer failure returns `8` and retains the partial file for investigation.

### Bias the sensor

```bash
netft bias 192.168.1.1
```

Bias changes the sensor's measurement zero and therefore changes subsequent output. The command shows the current reading and requires confirmation from a terminal. Use `--yes` only for a deliberately pre-authorized noninteractive operation with the sensor and connected equipment in a safe state.

Run `netft help <COMMAND>` or `netft <COMMAND> --help` for the complete command reference.

## Configuration

Command-line values take precedence over environment variables. The built-in connection defaults are HTTP port `80`, RDT port `49152`, and a `1s` timeout.

| Environment variable | Purpose |
| --- | --- |
| `NETFT_HOST` | Sensor host used when the positional host is omitted |
| `NETFT_HTTP_PORT` | Configuration HTTP port |
| `NETFT_RDT_PORT` | RDT UDP port |
| `NETFT_TIMEOUT` | Connection timeout, such as `500ms` or `2s` |
| `NO_COLOR` | Disable color unless `--color` explicitly overrides it |

Use `--verbose` for additional progress, `--quiet` to suppress progress, and `--color auto|always|never` to control color in human-readable output. Diagnostics go to standard error; normal and machine-readable results go to standard output or the selected file.

## Machine-readable output

`info`, `check`, and `bias` support JSON. `monitor` supports NDJSON and CSV, while `record` supports NDJSON and CSV files. Every machine-readable record carries integer `schema_version: 1`; CSV places the same value in its `schema_version` column. Machine output never contains terminal color sequences.

Automatic format selection uses human-readable output on an interactive terminal and a suitable machine format when redirected. For commands other than `record`, `--output PATH` replaces the file and selects the same machine format as redirection when the format is automatic.

## Shell completion

Generate completion from the installed binary so it always matches that version's commands and options:

```bash
netft completion bash
netft completion zsh
netft completion fish
netft completion powershell
```

Load the generated script using the standard completion mechanism for the selected shell. For example, Bash can load it for the current session with:

```bash
source <(netft completion bash)
```

## Exit status

| Code | Meaning |
| ---: | --- |
| `0` | Command completed, including checks that passed with warnings |
| `2` | Invalid command line or configuration |
| `3` | Sensor discovery failed |
| `4` | Stream connection or acquisition failed |
| `5` | Sensor reported a fault |
| `6` | Input/output operation failed |
| `7` | A `check` acceptance criterion failed |
| `8` | Recording integrity could not be guaranteed |
| `130` | Operation was interrupted |

## Security

ATI configuration discovery uses HTTP and RDT streaming uses UDP; neither device protocol provides transport encryption, peer authentication, or message integrity. Operate the sensor and CLI on a trusted, access-controlled network and do not expose sensor ports directly to the public internet. See [SECURITY.md](SECURITY.md) for deployment guidance and private vulnerability reporting.

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, focused tests, hardware-safety requirements, and the core synchronization policy.

## License

This project is licensed under the [Apache License 2.0](LICENSE). Release archives also include the notices for the synchronized `netft-cpp` core and libcurl.
