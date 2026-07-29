# netft-cli Design

## Summary

`netft-cli` is a standalone, cross-platform command-line application for
inspecting, monitoring, and biasing ATI Net F/T sensors. It lives in the
independent `netft/netft-cli` repository and installs the `netft` executable.

The project follows the native-distribution principles used by
[`openai/codex`](https://github.com/openai/codex): platform-specific release
archives, small shell and PowerShell installers, and a self-contained native
executable. It does not reproduce Codex's npm wrapper or monorepo structure.

The first release is `netft-cli 0.1.0`. The CLI has its own version line,
independent of the version of the embedded `netft-cpp` core.

## Goals

- Provide a focused native CLI with `info`, `monitor`, and `bias` commands.
- Offer an interactive real-time monitor and stable machine-readable output.
- Ship native artifacts for mainstream desktop and Linux platforms.
- Build offline from a controlled private snapshot of a released `netft-cpp`
  core.
- Keep protocol and transport behavior aligned with the other NetFT projects
  through explicit, reviewable snapshot updates.
- Make state-changing bias operations deliberate and safe for both people and
  automation.

## Non-goals

The initial release does not provide:

- compatibility with the CLI formerly included in `netft-cpp`;
- lossless high-rate recording or historical batch replay;
- sensor discovery across the local network;
- persistent profiles or configuration files;
- manual calibration overrides;
- npm, Homebrew, Scoop, winget, or other package-manager channels; or
- a public C++ library or development API.

## Command-line interface

The executable exposes three commands:

```text
netft info <HOST>
netft monitor <HOST>
netft bias <HOST>
```

`HOST` is a required positional IP address or hostname. Examples use the
address documented by ATI and never contain a developer's or test sensor's
address. A host value may not contain a URL scheme, credentials, a path, or a
query.

### `info`

`info` reads product and calibration information from the sensor without
starting a sustained RDT stream. It reports the product identity, endpoint,
force and torque units, counts-per-unit values, and other configuration
available through the core discovery API.

The command does not fall back silently to default calibration values. If the
sensor configuration cannot be read, it fails with a configuration or
connection error.

### `monitor`

`monitor` starts an RDT session and runs until interrupted. In an interactive
terminal it displays a fixed-layout table containing:

- connection state and receive rate;
- sequence, loss, and reordering information;
- raw counts for `Fx`, `Fy`, `Fz`, `Tx`, `Ty`, and `Tz`; and
- converted measurements and the units read from the sensor.

The terminal view refreshes at approximately 20 Hz. The core continues to
receive sensor packets independently of rendering. The renderer reads from a
single latest-value slot, so a slow terminal cannot block reception and stale
samples are never replayed.

Machine-readable NDJSON and CSV modes follow the same latest-value semantics.
`--rate <HZ>` controls their sampling rate and defaults to 20 Hz. Each emitted
record includes the sensor sequence and timing information so skipped sensor
records remain observable. The command does not claim to be a lossless
recorder.

`--duration <DURATION>` provides an optional bounded run. No duration means the
command continues until `Ctrl+C` or a terminal fault.

### `bias`

`bias` connects, reads a current sample, and displays the six raw and converted
values before requesting confirmation. It sends the ATI software-bias command
only after confirmation, then waits for a later sensor record and reports the
post-bias values.

Automation must pass `--yes`. A non-interactive invocation without `--yes`
fails without sending the bias command. Refusing an interactive prompt also
leaves the sensor unchanged.

### Common options

The command model supports:

```text
--format <FORMAT>
--output <PATH>
--http-port <PORT>
--rdt-port <PORT>
--timeout <DURATION>
--help
--version
```

`info` and `bias` accept `auto`, `text`, and `json`. `monitor` accepts `auto`,
`table`, `ndjson`, and `csv`. Separate `--json` and `--csv` aliases are not
provided.

With `--format auto`, an interactive terminal receives human-readable output.
Redirected `info` and `bias` output becomes JSON, while redirected `monitor`
output becomes NDJSON. When `--output` is used with `auto`, the same
non-interactive formats are selected; file extensions do not influence the
format.

Normal data is written only to standard output or the selected output file.
Diagnostics are written only to standard error.

## Architecture

The repository uses the following logical layout:

```text
netft-cli/
├── core/
│   ├── netft/
│   ├── UPSTREAM
│   └── MANIFEST.sha256
├── src/
│   ├── main.cpp
│   ├── cli/
│   ├── commands/
│   ├── output/
│   └── platform/
├── test/
├── tools/
├── scripts/install/
└── .github/workflows/
```

### Private core snapshot

`core/netft` contains only the source, headers, build fragments, and license
material required from a released `netft-cpp` core. It excludes the former
upstream CLI, upstream tests, packaging metadata, and developer environments.

`core/UPSTREAM` records the upstream repository, release tag, and full commit
SHA. `core/MANIFEST.sha256` records the controlled file set and content
digests. A synchronization tool provides:

```text
python tools/sync_core.py sync --source /path/to/netft-cpp --tag vX.Y.Z
python tools/sync_core.py verify
```

Synchronization accepts only a clean local source checkout at the requested
release. Normal configuration and compilation never contact the network,
discover an installed `netft-cpp`, or use a Git submodule.

Protocol, transport, discovery, recovery, and sensor-configuration changes are
made and released in `netft-cpp` first. The CLI snapshot is not patched
independently. A snapshot update is a separate reviewable change with full
cross-platform and fake-sensor validation.

### CLI modules

- `cli` parses arguments into typed command and option values. It has no sensor
  or terminal dependencies.
- `commands` owns the workflows for `info`, `monitor`, and `bias`.
- `output` serializes stable machine records and renders human-facing output.
- `platform` isolates signal handling, terminal detection, ANSI/Windows terminal
  setup, and process-specific behavior.
- `main.cpp` maps typed failures to diagnostics and process exit codes.

The build creates a private static core target and an internal CLI library for
testing. Only the `netft` executable is installed. No project headers or CMake
package are exported.

The first version uses a small internal typed argument parser and a lightweight
terminal renderer. It does not add a general CLI or TUI framework dependency.

## Data flow and concurrency

For `monitor`, configuration discovery completes before the RDT stream starts.
The core's receiving path publishes the latest immutable sample snapshot. A
separate presentation loop reads that slot at the selected rate and renders or
serializes it.

There is no presentation backlog. Replacing an unread value is expected
monitoring behavior, while core health counters retain the evidence needed to
distinguish presentation sampling from network loss.

The output path is not called from the core receive callback. An output failure
requests an orderly stop, terminates the RDT session, reports the local I/O
error, and returns a nonzero status.

For `bias`, the workflow is sequential: discover configuration, start the
stream, receive a current sample, obtain authorization, send the bias command,
receive a later sequence, report the result, and stop.

## Output contracts

JSON objects and CSV columns form the stable machine interface. Each sample
record contains:

- sensor endpoint and product identity where available;
- monotonic elapsed time and sensor sequence;
- six raw count values;
- six converted values;
- force and torque units; and
- connection and sequence-health fields relevant to that record.

JSON uses finite numeric values and valid UTF-8. NDJSON contains one complete
JSON object per line. CSV writes one header row followed by data rows in a
documented, stable column order.

Human-readable wording, whitespace, color, and terminal layout are not stable
machine interfaces.

## Failure model

The stable process statuses are:

| Code | Meaning |
| ---: | --- |
| 0 | Successful completion |
| 2 | Usage error or declined bias |
| 3 | Configuration discovery or connection failure |
| 4 | Data-stream interruption or timeout |
| 5 | Sensor-reported fault |
| 6 | Local output or file I/O failure |
| 130 | User interruption |

Ports, rates, durations, and timeouts are range-checked before network access.
Structured output never mixes diagnostics into its data stream.

`Ctrl+C` stops rendering first, requests the core session to stop, flushes any
completed output record, and exits with status 130. A bounded monitor that
reaches its requested duration exits successfully.

## Testing

Tests focus on behavior and structured contracts rather than fixed prose:

- parser tests inspect typed commands, values, and validation categories;
- JSON and NDJSON tests parse documents and inspect fields and types;
- CSV tests inspect column semantics and valid row encoding;
- terminal tests check refresh, dimensions, cleanup, and stream separation
  without full-screen text snapshots;
- fake-sensor integration tests cover discovery, streaming, bias, timeout,
  packet loss, reordering, recovery, and clean shutdown;
- platform tests exercise native signal and terminal behavior on Linux, macOS,
  and Windows;
- installer tests use isolated prefixes and controlled release fixtures without
  modifying the test host's actual user environment; and
- snapshot verification checks provenance, file scope, and checksums.

Linux additionally runs sanitizers and static analysis. Coverage is reported
for diagnosis but is not a merge-blocking percentage threshold.

Hardware tests are explicit opt-in jobs. The sensor address is supplied at
runtime through protected configuration or an environment variable and is
never committed. Bias hardware tests require fresh, explicit authorization and
must not reuse approval from an earlier job.

## Platforms and distribution

Version 0.1.0 publishes native assets for:

| Platform | Architectures |
| --- | --- |
| Linux | x86_64, arm64 |
| macOS | x86_64, arm64 |
| Windows | x86_64 |

The initial support promise is this platform and architecture matrix; it does
not claim compatibility with every historical OS release. Each release
documents the environments used for its native smoke tests.

Unix assets use `.tar.gz`; Windows uses `.zip`. The executable includes the
required NetFT core and curl library components. Users do not install a separate
`netft-cpp` or curl runtime, though an installer may use a system download
facility to retrieve the release archive.

`install.sh` selects the Linux or macOS asset and installs to
`~/.local/bin/netft` by default. `install.ps1` installs to
`%LOCALAPPDATA%\netft\bin\netft.exe` and can add that directory to the user
`PATH`. Both installers accept an explicit version and destination, verify the
downloaded artifact, stage updates atomically, and avoid administrator
privileges.

## Release process

A version tag starts a staged release:

1. Build each native artifact.
2. Run its tests and artifact smoke checks.
3. Produce the release archives and integrity metadata.
4. Exercise both installers against isolated release fixtures.
5. Assemble a draft GitHub Release only after every required asset succeeds.
6. Smoke-install the draft artifacts on every target platform.
7. Publish the release atomically.

Release notes use normal Markdown paragraphs without fixed-column hard
wrapping. The initial release has no npm or external package-manager wrapper.

## Repository separation

The CLI is removed from `netft-cpp` only after `netft-cli 0.1.0` is available:

1. Create and validate the private core snapshot from `netft-cpp 0.3.0`.
2. Implement and publish `netft-cli 0.1.0`.
3. Update `netft-cpp` documentation to direct CLI users to the new repository.
4. Remove `app/`, CLI-specific tests, and `NETFT_BUILD_CLI` from the subsequent
   `netft-cpp` SDK release.

The repositories then have clear roles: `netft-cpp` is the canonical C++ SDK
and shared core source; `netft-cli` is the native terminal product and a
controlled snapshot consumer.

## Licensing

`netft-cli` uses Apache License 2.0, consistent with the other current NetFT
projects. The synchronized core's upstream license and required third-party
notices remain in source and binary distributions.
