# Changelog

All notable changes to this project are documented in this file.

## Unreleased

### Fixed

- Updated the private netft-cpp core to v0.3.3 and reduced the local adaptation
  to the confirmed bias command's two-session isolation requirements.

## 0.2.0 - 2026-07-30

### Features

- Added bounded sensor health checks with configurable receive-rate, packet-loss, and reconnect acceptance criteria.
- Added reliable CSV and NDJSON recording by duration or sample count, with versioned records and explicit partial-file handling.
- Added generated help and bash, zsh, fish, and PowerShell completion from a shared command schema.
- Added environment-based connection configuration, consistent terminal controls, and stable exit categories for automation.

### Reliability

- Separated latest-sample monitoring from all-sample recording so each command has an explicit buffering policy.
- Added bounded recording queues, orderly shutdown and draining, atomic finalization, and visible integrity failures.
- Added structured machine-output contract tests and integer `schema_version` fields without coupling tests to human-facing prose.

### Automation

- Expanded deterministic fake-sensor integration coverage for diagnostics, recording, interruption, and failure paths.
- Kept coverage reporting informational while retaining formatting, static-analysis, workflow, packaging, and sanitizer gates.

### Platform support

- Preserved self-contained native releases for Linux x86_64 and ARM64, macOS Intel and Apple silicon, and Windows x86_64.

## 0.1.0 - 2026-07-30

### Added

- Added `info`, `monitor`, and confirmed `bias` commands for ATI Net F/T Ethernet sensors.
- Added automatic sensor configuration discovery with raw counts, converted force and torque values, native units, stream health, and packet-quality information.
- Added live terminal, JSON, NDJSON, and CSV output for interactive and machine-readable workflows.
- Added standalone release targets and checksum-verifying installers for Linux x86_64, Linux ARM64, macOS Intel, macOS Apple silicon, and Windows x86_64.
- Added a checksum-controlled private snapshot of `netft-cpp` 0.3.0.
