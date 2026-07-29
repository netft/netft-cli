# netft-cli

[![CI](https://github.com/netft/netft-cli/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/netft/netft-cli/actions/workflows/ci.yml)
[![CodeQL](https://github.com/netft/netft-cli/actions/workflows/codeql.yml/badge.svg?branch=main)](https://github.com/netft/netft-cli/actions/workflows/codeql.yml)
[![Coverage](https://codecov.io/gh/netft/netft-cli/graph/badge.svg?branch=main)](https://codecov.io/gh/netft/netft-cli)
[![Release](https://img.shields.io/github/v/release/netft/netft-cli?display_name=tag&sort=semver&style=flat)](https://github.com/netft/netft-cli/releases)
[![License](https://img.shields.io/github/license/netft/netft-cli?label=license&style=flat)](LICENSE)

`netft-cli` is a standalone command-line application for inspecting, monitoring, and biasing ATI Net F/T Ethernet sensors from Linux, macOS, and Windows. It discovers the active calibration and units from the sensor before using its RDT force/torque stream.

## Features

- Inspect sensor identity, calibration scales, and native measurement units.
- Monitor raw and converted six-axis data in a live terminal table, NDJSON, or CSV.
- Track stream state, receive rate, sequence progress, and packet-quality counters.
- Apply software bias through an explicit confirmation step.

## Installation

On Linux or macOS:

```bash
curl -fsSL https://raw.githubusercontent.com/netft/netft-cli/main/scripts/install/install.sh | sh
```

On Windows PowerShell:

```powershell
irm https://raw.githubusercontent.com/netft/netft-cli/main/scripts/install/install.ps1 | iex
```

The installers select the latest published stable release for the current platform and verify the downloaded archive. Prebuilt archives for published versions appear on [GitHub Releases](https://github.com/netft/netft-cli/releases).

### Build from source

Building requires CMake 3.16 or newer, a C++17 compiler, Threads, and libcurl 7.63.0 or newer:

```bash
git clone https://github.com/netft/netft-cli.git
cd netft-cli
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --config Release
```

The executable remains in the build tree. See [CONTRIBUTING.md](CONTRIBUTING.md) for the reproducible development environment and test workflow.

## Usage

### Inspect a sensor

```bash
netft info 192.168.1.1
```

`info` reads the sensor configuration without starting a measurement stream. Use `--format json` for machine-readable output.

### Monitor live data

```bash
netft monitor 192.168.1.1
netft monitor 192.168.1.1 --format ndjson --rate 20
```

Interactive output uses a live table. Redirected output defaults to NDJSON; `--format csv` is also available. `--rate` controls the output rate and defaults to 20 Hz. A monitor runs until interrupted unless `--duration`, such as `--duration 10s`, is provided.

`monitor` samples the latest available value at the selected output rate. It keeps no sample backlog and is not a lossless recorder of every RDT packet.

### Bias a sensor

```bash
netft bias 192.168.1.1
```

Bias changes the sensor's measurement zero and therefore changes subsequent output. The command shows the current reading and requires confirmation from a terminal. Use `--yes` only for a deliberately pre-authorized noninteractive operation with the sensor and connected equipment in a safe state.

Connection defaults are HTTP port `80`, RDT port `49152`, and a `1s` timeout. The default `--format auto` uses text for `info` and `bias` on a terminal and JSON when redirected; `monitor` uses a live table on a terminal and NDJSON when redirected. `--output PATH` writes to that file instead of standard output and replaces existing contents; with `auto`, it selects the same machine-readable format as redirection. Run `netft help <COMMAND>` for the command reference.

`192.168.1.1` is the ATI factory-default sensor address; replace it with the address configured for your sensor.

## Supported platforms

The project defines five native release targets. Binaries built for these targets contain the CLI and its private `netft-cpp` core and do not require a separate `netft-cpp` or libcurl installation.

| Release target | Platform | Architecture |
| --- | --- | --- |
| `linux-x86_64` | Linux | x86_64 |
| `linux-arm64` | Linux | ARM64 |
| `macos-x86_64` | macOS | Intel x86_64 |
| `macos-arm64` | macOS | Apple silicon |
| `windows-x86_64` | Windows | x86_64 |

## Security

ATI configuration discovery uses HTTP and RDT streaming uses UDP; neither device protocol provides transport encryption, peer authentication, or message integrity. Operate the sensor and CLI on a trusted, access-controlled network and do not expose sensor ports directly to the public internet. See [SECURITY.md](SECURITY.md) for deployment guidance and private vulnerability reporting.

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, tests, hardware-safety requirements, and the core synchronization policy.

## License

This project is licensed under the [Apache License 2.0](LICENSE). Release archives also include the notices for the synchronized `netft-cpp` core and libcurl.
