# netft-cli Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and release a standalone `netft` command-line application for inspecting, monitoring, and biasing ATI Net F/T sensors on Linux, macOS, and Windows.

**Architecture:** The executable uses a private, checksum-controlled snapshot of `netft-cpp 0.3.0`. Typed command parsing, sensor workflows, output serialization, and platform behavior remain separate internal modules; the monitor transfers only the latest immutable sample from the core callback to a rate-limited renderer.

**Tech Stack:** C++17, CMake 3.16+, libcurl 8.21.0, GoogleTest, Python 3.10+ maintenance tools, Pixi for Linux development, GitHub Actions.

## Global Constraints

- The repository, source, documentation, diagnostics, and release notes are English-only; user communication remains Chinese.
- The project and executable are named `netft-cli` and `netft`, respectively.
- The first release version is exactly `0.1.0`.
- Use an offline private snapshot of `https://github.com/netft/netft-cpp` tag `v0.3.0`, commit `46ee05639f818a17c1cfe604d0d77b1feb8f9b2b`.
- Normal builds must not fetch, discover, or require an installed `netft-cpp`.
- Do not edit snapshot protocol, transport, discovery, recovery, or sensor-configuration code locally.
- Require CMake 3.16 and C++17.
- Use Apache License 2.0 and preserve the synchronized core and curl notices.
- Do not include any local sensor address in source, tests, documentation, logs, or repository configuration.
- Examples may use only the ATI-documented `192.168.1.1` address.
- Tests may assert structured fields, types, exit categories, and state transitions, but must not freeze complete help text, diagnostic prose, release prose, or terminal-screen copy.
- Do not introduce a CLI framework, TUI framework, npm wrapper, Docker image, Homebrew formula, Scoop manifest, or winget manifest.
- `monitor` is a latest-value observer, not a lossless recorder: default render/output rate is exactly `20 Hz`, with no backlog or batch replay.
- `bias` must require interactive confirmation or the explicit `--yes` option.
- Release binaries must not require a separately installed `netft-cpp` or libcurl runtime.
- CI may report coverage but must not enforce a percentage threshold.
- Keep `.superpowers/`, worktrees, build output, local sensor settings, and hardware addresses out of the eventual public Initial commit.

## Planned file map

- `CMakeLists.txt`: project, private targets, install rules, warnings, and test entry point.
- `cmake/NetftCore.cmake`: build the selected snapshot sources as `netft_core`.
- `cmake/Warnings.cmake`, `cmake/Sanitizers.cmake`: project warning and sanitizer interfaces.
- `core/netft/`: exact upstream `LICENSE`, `include/`, and `src/` snapshot.
- `core/UPSTREAM`, `core/MANIFEST.sha256`: snapshot identity and controlled hashes.
- `src/app/error.hpp`: stable exit categories and typed application errors.
- `src/cli/options.hpp`, `src/cli/options.cpp`: typed command model and parser.
- `src/sensor/backend.hpp`, `src/sensor/netft_backend.*`: testable sensor boundary and core adapter.
- `src/output/records.*`: configuration, sample, and bias record construction.
- `src/output/json.*`, `src/output/csv.*`: stable machine serializers.
- `src/output/terminal.*`: human text and fixed-layout monitor rendering.
- `src/platform/interrupt.*`, `src/platform/terminal.*`: signal and TTY portability.
- `src/monitor/latest_sample.*`: single-slot callback handoff.
- `src/commands/info.*`, `src/commands/monitor.*`, `src/commands/bias.*`: command workflows.
- `src/application.*`, `src/main.cpp`: dependency assembly and process entry point.
- `test/unit/`: focused parser, record, serializer, terminal, and latest-value tests.
- `test/support/`: fake backends, clocks, records, output contexts, and structured parsers shared by unit tests.
- `test/integration/`: fake ATI sensor and end-to-end command tests.
- `test/artifact/`: packaged executable and installer checks.
- `tools/sync_core.py`: clean-release snapshot synchronization and verification.
- `tools/build_static_curl.sh`, `tools/build_windows_curl.ps1`: pinned HTTP-only static libcurl.
- `tools/package_release.py`, `tools/check_release.py`: deterministic archive assembly and inventory validation.
- `scripts/install/install.sh`, `scripts/install/install.ps1`: platform-aware user installers.
- `.github/workflows/ci.yml`, `codeql.yml`, `coverage.yml`, `release.yml`: quality and staged release automation.
- `README.md`, `CONTRIBUTING.md`, `CHANGELOG.md`, `SECURITY.md`: reader and contributor documentation.

---

### Task 1: Repository foundation and controlled core snapshot

**Files:**
- Create: `.gitignore`
- Create: `.clang-format`
- Create: `.clang-tidy`
- Create: `LICENSE`
- Create: `LICENSES/curl.txt`
- Create: `CMakeLists.txt`
- Create: `cmake/NetftCore.cmake`
- Create: `cmake/Warnings.cmake`
- Create: `cmake/Sanitizers.cmake`
- Create: `pixi.toml`
- Create: `tools/sync_core.py`
- Create: `test/tools/test_sync_core.py`
- Create: `core/UPSTREAM`
- Create: `core/MANIFEST.sha256`
- Create: `core/netft/LICENSE`
- Create: `core/netft/include/**`
- Create: `core/netft/src/**`

**Interfaces:**
- Produces: CMake target `netft_core`; commands `python tools/sync_core.py sync --source <PATH> --tag <TAG>` and `python tools/sync_core.py verify`.
- Consumes: clean local `netft-cpp` checkout at the exact signed/tagged `v0.3.0` commit.

- [ ] **Step 1: Write snapshot-tool tests**

Create fixture repositories in temporary directories and test exact path selection, dirty-tree rejection, mismatched-HEAD rejection, metadata generation, and checksum failure:

```python
def test_sync_records_exact_release_and_selected_paths(tmp_path: Path) -> None:
    source = make_tagged_fixture(tmp_path / "source", tag="v0.3.0")
    destination = tmp_path / "core"
    sync_core.sync(source, destination, "v0.3.0")
    metadata = sync_core.read_upstream(destination / "UPSTREAM")
    assert metadata["tag"] == "v0.3.0"
    assert metadata["paths"] == "LICENSE,include,src"
    assert not (destination / "netft" / "app").exists()
    sync_core.verify(destination)


def test_verify_rejects_changed_snapshot_file(tmp_path: Path) -> None:
    destination = synchronized_fixture(tmp_path)
    header = destination / "netft" / "include" / "netft" / "client.hpp"
    header.write_text(header.read_text() + "\n", encoding="utf-8")
    with pytest.raises(SystemExit, match="checksum mismatch"):
        sync_core.verify(destination)
```

- [ ] **Step 2: Run the snapshot tests and verify they fail**

Run:

```bash
python -m pytest test/tools/test_sync_core.py -q
```

Expected: collection fails because `tools.sync_core` does not exist.

- [ ] **Step 3: Implement the synchronization contract**

Use only `LICENSE`, `include`, and `src`; write `UPSTREAM` outside the copied tree and hash every file below `core/netft`:

```python
SELECTED = ("LICENSE", "include", "src")


def sync(source: Path, destination: Path, tag: str) -> None:
    commit = git(source, "rev-parse", f"{tag}^{{commit}}")
    if git(source, "rev-parse", "HEAD") != commit:
        raise SystemExit("source HEAD does not match the requested tag")
    if git(source, "status", "--porcelain"):
        raise SystemExit("source repository is dirty")
    replace_selected_tree(source, destination / "netft", SELECTED)
    write_upstream(destination / "UPSTREAM", tag, commit, SELECTED)
    write_manifest(destination / "netft", destination / "MANIFEST.sha256")
    verify(destination)
```

Copy the Apache license from `netft-cpp`; copy curl's upstream license text from the already audited pyNetFT `LICENSES/curl.txt`.

- [ ] **Step 4: Synchronize the real core and verify its identity**

Run:

```bash
python tools/sync_core.py sync \
  --source <path-to-netft-cpp> \
  --tag v0.3.0
python tools/sync_core.py verify
```

Expected: `core/UPSTREAM` contains commit `46ee05639f818a17c1cfe604d0d77b1feb8f9b2b`; verification exits 0.

- [ ] **Step 5: Add the private core CMake target and Linux developer environment**

`cmake/NetftCore.cmake` must compile the exact upstream implementation list and select one UDP transport:

```cmake
add_library(netft_core STATIC
  "${NETFT_CORE_ROOT}/src/types.cpp"
  "${NETFT_CORE_ROOT}/src/status.cpp"
  "${NETFT_CORE_ROOT}/src/discovery.cpp"
  "${NETFT_CORE_ROOT}/src/client.cpp"
  "${NETFT_CORE_ROOT}/src/detail/client_impl.cpp"
  "${NETFT_CORE_ROOT}/src/detail/fault_latch.cpp"
  "${NETFT_CORE_ROOT}/src/detail/protocol.cpp"
  "${NETFT_CORE_ROOT}/src/detail/sequence.cpp"
  "${NETFT_CORE_ROOT}/src/detail/xml_config.cpp"
)
if(WIN32)
  target_sources(netft_core PRIVATE "${NETFT_CORE_ROOT}/src/detail/udp_transport_windows.cpp")
  target_link_libraries(netft_core PRIVATE ws2_32)
else()
  target_sources(netft_core PRIVATE "${NETFT_CORE_ROOT}/src/detail/udp_transport_posix.cpp")
endif()
target_compile_features(netft_core PUBLIC cxx_std_17)
target_include_directories(netft_core PUBLIC "${NETFT_CORE_ROOT}/include"
                                      PRIVATE "${NETFT_CORE_ROOT}/src")
target_link_libraries(netft_core PUBLIC Threads::Threads CURL::libcurl)
```

Configure the top-level project as `project(netft_cli VERSION 0.1.0 LANGUAGES CXX)`. Pixi provides CMake, Ninja, GCC/Clang, GTest, libcurl, Python, pytest, clang-format, clang-tidy, actionlint, gcovr, and tasks named `snapshot-check`, `configure`, `build`, `test`, `format-check`, `tidy`, and `check`.

- [ ] **Step 6: Build the empty application target and run foundation checks**

Add a temporary `src/main.cpp` returning 0, then run:

```bash
pixi run snapshot-check
pixi run cmake -S . -B build/foundation -G Ninja -DBUILD_TESTING=OFF
pixi run cmake --build build/foundation
./build/foundation/netft --help
```

Expected: snapshot verification and build pass; the temporary executable exits 0.

- [ ] **Step 7: Commit the foundation**

```bash
git add . ':!.superpowers'
git commit -m "build: initialize netft CLI"
```

### Task 2: Typed command model and parser

**Files:**
- Create: `src/app/error.hpp`
- Create: `src/cli/options.hpp`
- Create: `src/cli/options.cpp`
- Create: `test/unit/options_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `netft_cli::Action parse_arguments(const std::vector<std::string_view>&)`.
- Produces: `Action = std::variant<ShowHelp, ShowVersion, InfoOptions, MonitorOptions, BiasOptions>`.
- Produces: `AppError(ExitCode, std::string)` with `ExitCode::{Usage, Discovery, Stream, Sensor, Io, Interrupted}`.

- [ ] **Step 1: Write parser tests against typed values**

```cpp
TEST(Options, ParsesPositionalMonitorHostAndRate) {
  const auto action = parse_arguments({"monitor", "192.168.1.1", "--rate", "25"});
  const auto &monitor = std::get<MonitorOptions>(action);
  EXPECT_EQ(monitor.connection.host, "192.168.1.1");
  EXPECT_DOUBLE_EQ(monitor.rate_hz, 25.0);
  EXPECT_EQ(monitor.format, OutputFormat::Automatic);
}

TEST(Options, RejectsUrlInsteadOfHost) {
  expect_usage_error({"info", "http://192.168.1.1/config.xml"});
}

TEST(Options, RequiresYesForNoninteractiveBiasAtRuntimeNotParseTime) {
  const auto action = parse_arguments({"bias", "192.168.1.1"});
  EXPECT_FALSE(std::get<BiasOptions>(action).assume_yes);
}
```

Also cover all allowed formats per command, port range `1..65535`, positive finite rate and timeout, `--duration`, duplicate host, unknown options, help topics, and version action. Do not compare complete help or error sentences.

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```bash
pixi run cmake -S . -B build/parser -G Ninja -DBUILD_TESTING=ON
pixi run cmake --build build/parser
pixi run ctest --test-dir build/parser -R netft_options --output-on-failure
```

Expected: compilation fails because `options.hpp` is absent.

- [ ] **Step 3: Implement the typed model**

Use these public internal types:

```cpp
enum class OutputFormat { Automatic, Text, Json, Table, Ndjson, Csv };

struct ConnectionOptions {
  std::string host;
  int http_port{80};
  int rdt_port{49152};
  std::chrono::duration<double> timeout{1.0};
};

struct InfoOptions { ConnectionOptions connection; OutputFormat format; std::optional<std::filesystem::path> output; };
struct MonitorOptions {
  ConnectionOptions connection;
  OutputFormat format{OutputFormat::Automatic};
  std::optional<std::filesystem::path> output;
  double rate_hz{20.0};
  std::optional<std::chrono::duration<double>> duration;
};
struct BiasOptions {
  ConnectionOptions connection;
  OutputFormat format{OutputFormat::Automatic};
  std::optional<std::filesystem::path> output;
  bool assume_yes{false};
};
```

Reject schemes by forbidding `"://"`, `/`, `?`, `#`, and `@` in the positional host. Keep help text generation separate from parsing so tests inspect `ShowHelp::topic`, not prose.

Parse durations with one documented grammar: a positive finite decimal followed
immediately by `ms` or `s`, such as `250ms`, `1s`, or `1.5s`. Reject bare
numbers and every other suffix. Convert the result to
`std::chrono::duration<double>`.

- [ ] **Step 4: Run parser and formatting checks**

```bash
pixi run cmake --build build/parser
pixi run ctest --test-dir build/parser -R netft_options --output-on-failure
pixi run format-check
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/app src/cli test/unit/options_test.cpp
git commit -m "feat: define the netft command interface"
```

### Task 3: Sensor boundary, records, and machine serializers

**Files:**
- Create: `src/sensor/backend.hpp`
- Create: `src/sensor/netft_backend.hpp`
- Create: `src/sensor/netft_backend.cpp`
- Create: `src/output/records.hpp`
- Create: `src/output/records.cpp`
- Create: `src/output/context.hpp`
- Create: `src/output/context.cpp`
- Create: `src/output/json.hpp`
- Create: `src/output/json.cpp`
- Create: `src/output/csv.hpp`
- Create: `src/output/csv.cpp`
- Create: `test/unit/records_test.cpp`
- Create: `test/unit/json_test.cpp`
- Create: `test/unit/csv_test.cpp`
- Create: `test/support/assertions.hpp`
- Create: `test/support/fake_backend.hpp`
- Create: `test/support/fake_backend.cpp`
- Create: `test/support/memory_output.hpp`
- Create: `test/support/options.hpp`
- Create: `test/support/records.hpp`
- Create: `test/support/structured_parsers.hpp`
- Create: `test/support/structured_parsers.cpp`
- Modify: `CMakeLists.txt`
- Modify: `pixi.toml`

**Interfaces:**
- Produces: `SensorBackend::discover`, `SensorBackend::open`, and `SensorSession::{start, stop, bias, health}`.
- Produces: `ConfigurationRecord`, `SampleRecord`, `BiasRecord`, `make_sample_record`.
- Produces: `write_json`, `write_ndjson`, and stateful `CsvWriter::write`.
- Produces: `OutputContext`, `OutputHandle`, `FakeBackend`, `MemoryOutput`, `expect_app_error`, `parse_json`, `parse_ndjson`, and `parse_csv`.
- Consumes: Task 2 `ConnectionOptions` and the private core's `SensorConfiguration`, `Sample`, and `HealthSnapshot`.

- [ ] **Step 1: Write record and serializer tests**

Construct records with locale-independent values and assert parsed structure rather than serialized prose:

```cpp
TEST(Json, EmitsFiniteTypedSampleDocument) {
  std::ostringstream stream;
  write_ndjson(stream, sample_record());
  const auto document = parse_json(stream.str());
  EXPECT_EQ(document.at("rdt_sequence"), 41);
  EXPECT_EQ(document.at("raw").size(), 6);
  EXPECT_EQ(document.at("force").at("unit"), "N");
  EXPECT_EQ(document.at("torque").at("unit"), "N-mm");
}

TEST(Csv, WritesHeaderOnceAndSixRawAndScaledAxes) {
  std::ostringstream stream;
  CsvWriter writer(stream);
  writer.write(sample_record());
  writer.write(next_sample_record());
  const auto table = parse_csv(stream.str());
  EXPECT_EQ(table.header_count(), 1U);
  EXPECT_TRUE(table.has_columns({"raw_fx", "fx", "raw_tz", "tz"}));
  EXPECT_EQ(table.row_count(), 2U);
}
```

Use `nlohmann-json` only in the test environment; do not add it as a production dependency.

- [ ] **Step 2: Run and observe compilation failure**

```bash
pixi run cmake -S . -B build/output -G Ninja -DBUILD_TESTING=ON
pixi run cmake --build build/output
```

Expected: missing record and serializer headers.

- [ ] **Step 3: Implement the sensor abstraction and real adapter**

```cpp
class SensorSession {
public:
  using Callback = std::function<void(const netft::Sample &)>;
  virtual ~SensorSession() = default;
  virtual void start(Callback callback) = 0;
  virtual void stop() noexcept = 0;
  virtual void bias() = 0;
  virtual netft::HealthSnapshot health() const = 0;
};

class SensorBackend {
public:
  virtual ~SensorBackend() = default;
  virtual netft::SensorConfiguration discover(const ConnectionOptions &) = 0;
  virtual std::unique_ptr<SensorSession> open(const ConnectionOptions &) = 0;
};
```

`NetftBackend` maps the common timeout to discovery connect/total timeouts and to `netft::Config::receive_timeout`; it never sets `calibration_override`.

- [ ] **Step 4: Implement output ownership and shared test support**

Use one context for the standard streams and a handle that either references
stdout or owns an opened binary-mode file:

```cpp
struct OutputContext {
  std::istream &input;
  std::ostream &standard_output;
  std::ostream &standard_error;
  bool input_is_terminal{};
  bool output_is_terminal{};
};

class OutputHandle {
public:
  static OutputHandle standard(std::ostream &stream);
  static OutputHandle file(const std::filesystem::path &path);
  std::ostream &stream() noexcept;
  void flush();
};
```

`FakeBackend` implements `SensorBackend`, owns a `FakeSession`, and records
discover, open, start, stop, and bias calls. `MemoryOutput` owns
input/output/error string streams and returns an `OutputContext`.
`test/support/records.hpp` provides `configuration()`, `sample(sequence)`,
`sample_record(sequence)`, and `next_sample_record()`.
`test/support/options.hpp` provides valid `info_options()`,
`monitor_for(duration)`, and `bias_options(assume_yes)` fixtures.
`expect_app_error`
executes a callable and compares only `AppError::exit_code()`.

Add `nlohmann-json` only to the Pixi test environment and link
`nlohmann_json::nlohmann_json` only to test targets. `parse_json` wraps
`nlohmann::json::parse`, and `parse_ndjson` parses each nonempty line into a
`std::vector<nlohmann::json>`. Implement `parse_csv` as a test-only RFC 4180 reader:

```cpp
struct CsvTable {
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> rows;
  std::size_t header_count() const noexcept { return header.empty() ? 0U : 1U; }
  std::size_t row_count() const noexcept { return rows.size(); }
  bool has_columns(std::initializer_list<std::string_view>) const;
};
```

- [ ] **Step 5: Implement stable record construction and serialization**

Define scaled axes in `Fx,Fy,Fz,Tx,Ty,Tz` order. Use
`std::locale::classic()` and `max_digits10`; reject non-finite values with
`AppError{ExitCode::Io, "non-finite sample value"}` before writing malformed
JSON.

```cpp
struct SampleRecord {
  std::string host;
  double elapsed_seconds{};
  std::uint32_t rdt_sequence{}, ft_sequence{}, status{};
  std::array<std::int32_t, 6> raw{};
  std::array<double, 6> scaled{};
  std::string force_unit, torque_unit;
  double receive_rate_hz{};
  std::uint64_t lost_count{}, duplicate_count{}, out_of_order_count{};
  std::string state;
};
```

JSON uses nested `force` and `torque` objects with `raw`, `value`, and `unit`; CSV uses documented flat columns.

- [ ] **Step 6: Run tests**

```bash
pixi run cmake --build build/output
pixi run ctest --test-dir build/output -R 'netft_(records|json|csv)' --output-on-failure
pixi run format-check
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/sensor src/output test/unit
git commit -m "feat: add sensor records and structured output"
```

### Task 4: Cross-platform interruption and terminal rendering

**Files:**
- Create: `src/platform/interrupt.hpp`
- Create: `src/platform/interrupt.cpp`
- Create: `src/platform/terminal.hpp`
- Create: `src/platform/terminal_posix.cpp`
- Create: `src/platform/terminal_windows.cpp`
- Create: `src/output/terminal.hpp`
- Create: `src/output/terminal.cpp`
- Create: `test/unit/interrupt_test.cpp`
- Create: `test/unit/terminal_test.cpp`
- Create: `test/support/fake_terminal.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `InterruptFlag::{requested, request}`, RAII `InterruptHandler`, `TerminalCapabilities`, `TerminalWriter`.
- Produces: `render_configuration_text`, `render_bias_text`, and `TerminalMonitor::render`.
- Produces: test-only `FakeTerminal`, which records cursor, clear, write, and flush operations.
- Consumes: Task 3 records.

- [ ] **Step 1: Write behavioral terminal tests**

```cpp
TEST(TerminalMonitor, ReusesOneFrameAndDoesNotAppendHistory) {
  FakeTerminal terminal({.width = 100, .height = 24, .ansi = true});
  TerminalMonitor monitor(terminal);
  monitor.render(sample_record(1));
  monitor.render(sample_record(2));
  EXPECT_EQ(terminal.clear_frame_count(), 1U);
  EXPECT_EQ(terminal.home_count(), 2U);
}

TEST(InterruptFlag, RequestIsVisibleAcrossThreads) {
  InterruptFlag flag;
  std::thread worker([&] { flag.request(); });
  worker.join();
  EXPECT_TRUE(flag.requested());
}
```

Check fixed column widths with numeric magnitude changes, raw and converted values present, and cleanup on close. Inspect terminal operations, not complete rendered copy.

- [ ] **Step 2: Run the tests and verify missing types**

```bash
pixi run cmake -S . -B build/terminal -G Ninja -DBUILD_TESTING=ON
pixi run cmake --build build/terminal
```

Expected: compilation fails on missing platform and terminal headers.

- [ ] **Step 3: Implement platform isolation**

Use `sigaction` and `isatty` on POSIX. Use `SetConsoleCtrlHandler`, `_isatty`, and guarded `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on Windows. The OS callback may only set an atomic flag:

```cpp
class InterruptFlag {
public:
  void request() noexcept { requested_.store(true, std::memory_order_release); }
  bool requested() const noexcept { return requested_.load(std::memory_order_acquire); }
private:
  std::atomic<bool> requested_{false};
};
```

Restore the previous signal/console handler and terminal mode in RAII destructors.

- [ ] **Step 4: Implement the fixed-layout renderer**

The renderer writes connection health, six raw values, six converted values, units, and sequence counters. It uses fixed numeric field widths and ANSI home/clear operations only when capabilities report support. Non-ANSI terminals receive a periodic compact line, never escape bytes.

- [ ] **Step 5: Run native tests**

```bash
pixi run cmake --build build/terminal
pixi run ctest --test-dir build/terminal -R 'netft_(interrupt|terminal)' --output-on-failure
pixi run format-check
```

Expected: all pass on Linux; Windows-specific source remains selected only on Windows.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/platform src/output/terminal.* test/unit
git commit -m "feat: add portable terminal runtime"
```

### Task 5: `info` command

**Files:**
- Create: `src/commands/info.hpp`
- Create: `src/commands/info.cpp`
- Create: `test/unit/info_command_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `int run_info(const InfoOptions&, SensorBackend&, OutputContext&)`.
- Consumes: Task 2 options, Task 3 backend and serializers, Task 4 terminal detection.

- [ ] **Step 1: Write command tests with a fake backend**

```cpp
TEST(InfoCommand, DiscoversConfigurationWithoutOpeningStream) {
  FakeBackend backend;
  backend.set_configuration(configuration());
  MemoryOutput output(/*stdout_is_tty=*/false);
  EXPECT_EQ(run_info(info_options(), backend, output.context()), 0);
  EXPECT_EQ(backend.discover_calls(), 1U);
  EXPECT_EQ(backend.open_calls(), 0U);
  EXPECT_TRUE(parse_json(output.standard_output_text()).contains("calibration"));
}

TEST(InfoCommand, MapsDiscoveryFailureToExitThree) {
  FakeBackend backend;
  backend.fail_discovery();
  MemoryOutput output(false);
  expect_app_error(ExitCode::Discovery, [&] {
    run_info(info_options(), backend, output.context());
  });
}
```

- [ ] **Step 2: Run and verify failure**

```bash
pixi run cmake -S . -B build/info -G Ninja -DBUILD_TESTING=ON
pixi run cmake --build build/info
```

Expected: missing `run_info`.

- [ ] **Step 3: Implement the workflow**

Resolve `Automatic` to `Text` for a TTY and `Json` otherwise. Reject `Table`, `Ndjson`, and `Csv` for this command in the parser. Open `--output` before network access so a bad local path cannot bias the observed sensor result.

- [ ] **Step 4: Run tests and static analysis**

```bash
pixi run cmake --build build/info
pixi run ctest --test-dir build/info -R netft_info --output-on-failure
pixi run tidy
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/commands/info.* test/unit/info_command_test.cpp
git commit -m "feat: add sensor information command"
```

### Task 6: Latest-value monitor

**Files:**
- Create: `src/monitor/latest_sample.hpp`
- Create: `src/monitor/latest_sample.cpp`
- Create: `src/commands/monitor.hpp`
- Create: `src/commands/monitor.cpp`
- Create: `src/platform/clock.hpp`
- Create: `src/platform/clock.cpp`
- Create: `test/unit/latest_sample_test.cpp`
- Create: `test/unit/monitor_command_test.cpp`
- Create: `test/support/fake_clock.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `LatestSampleSlot::{publish, snapshot, wait_for_first}`.
- Produces: `Clock::{now, sleep_until}`, native `SystemClock`, and test-only `FakeClock`.
- Produces: `int run_monitor(const MonitorOptions&, SensorBackend&, OutputContext&, InterruptFlag&, Clock&)`.
- Consumes: Task 3 sensor sessions and serializers, Task 4 terminal renderer.

- [ ] **Step 1: Write single-slot concurrency tests**

```cpp
TEST(LatestSampleSlot, ReplacesUnreadSampleWithoutQueueing) {
  LatestSampleSlot slot;
  slot.publish(sample(10));
  slot.publish(sample(11));
  const auto latest = slot.snapshot();
  ASSERT_TRUE(latest);
  EXPECT_EQ(latest->rdt_sequence, 11U);
}

TEST(LatestSampleSlot, WaitForFirstStopsOnInterruption) {
  LatestSampleSlot slot;
  InterruptFlag interrupt;
  interrupt.request();
  EXPECT_FALSE(slot.wait_for_first(100ms, interrupt));
}
```

- [ ] **Step 2: Write monitor workflow tests with a fake clock**

Verify 20 Hz default cadence, explicit `--rate`, bounded duration, NDJSON/CSV selection, no replay after delayed rendering, stream fault mapping, output failure mapping, and status 130 on interruption.

```cpp
TEST(MonitorCommand, SamplesLatestValueAtTwentyHertz) {
  FakeBackend backend;
  backend.session().set_samples({sample(1), sample(2), sample(3)});
  FakeClock clock;
  MemoryOutput output(false);
  InterruptFlag interrupt;
  run_monitor(monitor_for(100ms), backend, output.context(), interrupt, clock);
  EXPECT_EQ(parse_ndjson(output.standard_output_text()).size(), 2U);
  EXPECT_EQ(parse_ndjson(output.standard_output_text()).back()["rdt_sequence"], 3U);
}
```

- [ ] **Step 3: Run and verify failure**

```bash
pixi run cmake -S . -B build/monitor -G Ninja -DBUILD_TESTING=ON
pixi run cmake --build build/monitor
```

Expected: missing latest-value and monitor types.

- [ ] **Step 4: Implement the latest-value slot**

Protect one `std::optional<netft::Sample>` with a mutex and condition variable. `publish` overwrites the optional and notifies first-sample waiters. Never invoke output or health queries from the sensor callback.

- [ ] **Step 5: Implement monitor scheduling**

Define `Clock` as an internal interface returning
`std::chrono::steady_clock::time_point` and accepting an absolute
`sleep_until` deadline. `SystemClock` delegates to `steady_clock` and
`std::this_thread::sleep_until`; `FakeClock` advances immediately to the
requested deadline and records it.

Discover configuration, open output, start the session, wait for the first sample, then render on `steady_clock` deadlines spaced by `1 / rate_hz`. At each deadline copy the latest sample and current health, build one `SampleRecord`, and write it. Use deadline advancement rather than sleeping relative durations to avoid accumulating drift.

- [ ] **Step 6: Run monitor tests and sanitizers**

```bash
pixi run cmake --build build/monitor
pixi run ctest --test-dir build/monitor -R 'netft_(latest_sample|monitor)' --output-on-failure
pixi run cmake -S . -B build/monitor-asan -G Ninja \
  -DBUILD_TESTING=ON -DNETFT_SANITIZERS=ON -DCMAKE_CXX_COMPILER=clang++
pixi run cmake --build build/monitor-asan
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  pixi run ctest --test-dir build/monitor-asan -R 'netft_(latest_sample|monitor)' --output-on-failure
```

Expected: all pass without races, leaks, or undefined behavior.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/monitor src/commands/monitor.* test/unit
git commit -m "feat: add real-time monitor command"
```

### Task 7: Confirmed bias workflow

**Files:**
- Create: `src/commands/bias.hpp`
- Create: `src/commands/bias.cpp`
- Create: `src/output/confirmation.hpp`
- Create: `src/output/confirmation.cpp`
- Create: `test/unit/bias_command_test.cpp`
- Create: `test/support/fake_confirmation.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `Confirmation::confirm(const BiasPreview&)`.
- Produces: test-only `FakeConfirmation`, which returns a configured decision and counts calls.
- Produces: `int run_bias(const BiasOptions&, SensorBackend&, OutputContext&, Confirmation&, InterruptFlag&)`.
- Consumes: Task 3 records/backend and Task 6 latest-value slot.

`BiasPreview` contains the discovered `ConfigurationRecord` and the pre-bias
`SampleRecord`. `Confirmation` is:

```cpp
class Confirmation {
public:
  virtual ~Confirmation() = default;
  virtual bool confirm(const BiasPreview &preview) = 0;
};
```

- [ ] **Step 1: Write safety-first bias tests**

```cpp
TEST(BiasCommand, DeclineDoesNotSendBias) {
  FakeBackend backend;
  backend.session().set_samples({sample(10)});
  MemoryOutput output(true);
  InterruptFlag interrupt;
  FakeConfirmation confirmation(false);
  expect_app_error(ExitCode::Usage, [&] {
    run_bias(bias_options(false), backend, output.context(), confirmation, interrupt);
  });
  EXPECT_EQ(backend.session().bias_calls(), 0U);
}

TEST(BiasCommand, YesSkipsPromptAndRequiresLaterSequence) {
  FakeBackend backend;
  backend.session().set_samples({sample(10), sample(11)});
  MemoryOutput output(false);
  InterruptFlag interrupt;
  FakeConfirmation confirmation(false);
  EXPECT_EQ(
      run_bias(bias_options(true), backend, output.context(), confirmation, interrupt),
      0);
  EXPECT_EQ(confirmation.calls(), 0U);
  EXPECT_EQ(backend.session().bias_calls(), 1U);
  const auto document = parse_json(output.standard_output_text());
  EXPECT_EQ(document.at("after").at("rdt_sequence"), 11U);
}
```

Cover non-TTY without `--yes`, interrupted confirmation, no pre-bias sample, no later post-bias sequence, sensor fault, and JSON output.

- [ ] **Step 2: Run and verify failure**

```bash
pixi run cmake -S . -B build/bias -G Ninja -DBUILD_TESTING=ON
pixi run cmake --build build/bias
```

Expected: missing bias workflow.

- [ ] **Step 3: Implement confirmation and workflow**

Interactive confirmation accepts only an explicit affirmative response after showing the current raw and scaled record. If stdin is not a terminal, return `ExitCode::Usage` before `SensorSession::bias`.

After sending bias, ignore records whose RDT sequence equals the preview sequence. Stop and return a stream or sensor category if a later sequence does not arrive before the configured timeout.

- [ ] **Step 4: Run tests**

```bash
pixi run cmake --build build/bias
pixi run ctest --test-dir build/bias -R netft_bias --output-on-failure
pixi run format-check
pixi run tidy
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/commands/bias.* src/output/confirmation.* test/unit/bias_command_test.cpp
git commit -m "feat: add confirmed sensor bias command"
```

### Task 8: Application assembly and fake-sensor end-to-end tests

**Files:**
- Create: `src/application.hpp`
- Create: `src/application.cpp`
- Replace: `src/main.cpp`
- Create: `test/integration/fake_sensor.hpp`
- Create: `test/integration/fake_sensor.cpp`
- Create: `test/integration/cli_integration_test.cpp`
- Create: `test/integration/process_test.py`
- Create: `test/integration/run_fake_sensor.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `int run_application(const std::vector<std::string_view>&, AppEnvironment&)`.
- Produces: final `netft` process with stable statuses `0,2,3,4,5,6,130`.
- Consumes: all command workflows and platform implementations.

- [ ] **Step 1: Port the fake ATI sensor without CLI expectations**

Reuse protocol packet construction and HTTP configuration fixtures from `netft-cpp/test/support`, but place all sensor addresses on loopback with ephemeral ports. The fake exposes observed protocol commands and configurable packet loss, ordering, status, and response timing.

- [ ] **Step 2: Write end-to-end command tests**

```cpp
TEST(CliIntegration, InfoUsesHttpConfigurationAndNeverStartsRdt) {
  FakeSensor sensor;
  const auto status = run_cli({"info", sensor.host(), "--http-port", sensor.http_port(),
                               "--format", "json"});
  EXPECT_EQ(status, 0);
  EXPECT_EQ(sensor.start_realtime_count(), 0U);
}

TEST(CliIntegration, MonitorStopsRdtAfterBoundedRun) {
  FakeSensor sensor;
  const auto status = run_cli({"monitor", sensor.host(), "--http-port", sensor.http_port(),
                               "--rdt-port", sensor.rdt_port(), "--duration", "0.1s",
                               "--format", "ndjson"});
  EXPECT_EQ(status, 0);
  EXPECT_GE(sensor.stop_streaming_count(), 1U);
}
```

Add process tests for `--help`, `--version`, stdout/stderr separation, invalid host exit 2, unreachable host exit 3, and interrupt exit 130. Tests inspect categories and parseable records, not prose.

- [ ] **Step 3: Implement application dispatch and exit mapping**

```cpp
int run_application(const std::vector<std::string_view> &arguments,
                    AppEnvironment &environment) {
  const auto action = parse_arguments(arguments);
  return std::visit(overloaded{
      [&](const ShowHelp &value) { return environment.show_help(value); },
      [&](const ShowVersion &) { return environment.show_version(); },
      [&](const InfoOptions &value) { return run_info(value, environment.backend(), environment.output()); },
      [&](const MonitorOptions &value) { return run_monitor(value, environment.backend(), environment.output(),
                                                            environment.interrupt(), environment.clock()); },
      [&](const BiasOptions &value) { return run_bias(value, environment.backend(), environment.output(),
                                                      environment.confirmation(), environment.interrupt()); }},
      action);
}
```

`main` catches `AppError`, core discovery errors, sensor faults, `std::ios_base::failure`, and unexpected exceptions, writes diagnostics to stderr, and returns the mapped code.

Define `AppEnvironment` as the internal dependency boundary:

```cpp
class AppEnvironment {
public:
  virtual ~AppEnvironment() = default;
  virtual SensorBackend &backend() = 0;
  virtual OutputContext &output() = 0;
  virtual Confirmation &confirmation() = 0;
  virtual InterruptFlag &interrupt() = 0;
  virtual Clock &clock() = 0;
  virtual int show_help(const ShowHelp &) = 0;
  virtual int show_version() = 0;
};
```

`NativeEnvironment` owns `NetftBackend`, standard-stream `OutputContext`,
terminal confirmation, `InterruptHandler`, and `SystemClock`.

- [ ] **Step 4: Run all functional tests**

```bash
pixi run cmake -S . -B build/integration -G Ninja -DBUILD_TESTING=ON
pixi run cmake --build build/integration
pixi run ctest --test-dir build/integration --output-on-failure
pixi run python -m pytest test/integration/process_test.py -q
```

Expected: all pass.

- [ ] **Step 5: Run a local fake-sensor smoke session**

```bash
pixi run python test/integration/run_fake_sensor.py -- \
  ./build/integration/netft monitor 127.0.0.1 \
  --http-port 18080 --rdt-port 49153 --duration 1s --format ndjson
```

Expected: valid NDJSON records, successful bounded exit, and observed RDT stop command.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src test/integration
git commit -m "feat: assemble the native netft application"
```

### Task 9: Native portability and complete local quality gates

**Files:**
- Create: `test/platform/windows_smoke.ps1`
- Create: `test/platform/macos_smoke.sh`
- Create: `test/hardware/hardware_test.py`
- Create: `tools/lsan.supp`
- Modify: `pixi.toml`
- Modify: `.clang-tidy`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `pixi run check`, `pixi run sanitizers`, and explicit `hardware-test` / `hardware-bias-test`.
- Consumes: final application from Task 8.

- [ ] **Step 1: Add platform and hardware tests**

Hardware tests require `NETFT_SENSOR_HOST`; bias additionally requires `NETFT_ALLOW_BIAS=1` set for that invocation. The test parses JSON records and checks numeric types, increasing sequences, sensor-selected units, and `bias` state, not diagnostic wording.

- [ ] **Step 2: Add aggregate quality tasks**

```toml
[tasks]
snapshot-check = "python tools/sync_core.py verify"
format-check = "git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror"
test = { cmd = "ctest --test-dir build --output-on-failure", depends-on = ["build"] }
tidy = "cmake -S . -B build/tidy -G Ninja -DBUILD_TESTING=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && clang-tidy -p build/tidy --config-file=.clang-tidy $(git ls-files 'src/*.cpp')"
check = { depends-on = ["snapshot-check", "format-check", "test", "tidy"] }
hardware-test = "NETFT_ALLOW_BIAS=0 python test/hardware/hardware_test.py"
hardware-bias-test = "NETFT_ALLOW_BIAS=1 python test/hardware/hardware_test.py"
sanitizers = "cmake -S . -B build/sanitizers -G Ninja -DBUILD_TESTING=ON -DNETFT_SANITIZERS=ON -DCMAKE_CXX_COMPILER=clang++ && cmake --build build/sanitizers && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitizers --output-on-failure"
```

- [ ] **Step 3: Run the complete Linux gate**

```bash
pixi run check
pixi run sanitizers
```

Expected: all tests and analysis pass; no coverage percentage gate exists.

- [ ] **Step 4: Run authorized read-only hardware validation**

```bash
NETFT_SENSOR_HOST=<runtime-address> pixi run hardware-test
```

Expected: `info` and bounded `monitor` return valid records using the configuration read from the sensor. Do not run the bias variant without fresh user authorization.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt pixi.toml .clang-tidy test/platform test/hardware tools/lsan.supp
git commit -m "test: validate native CLI behavior"
```

### Task 10: Self-contained dependency and release packaging

**Files:**
- Create: `tools/build_static_curl.sh`
- Create: `tools/build_windows_curl.ps1`
- Create: `tools/package_release.py`
- Create: `tools/check_release.py`
- Create: `test/artifact/test_package_release.py`
- Create: `test/artifact/test_release_inventory.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `netft-cli-0.1.0-{linux-x86_64,linux-arm64,macos-x86_64,macos-arm64}.tar.gz` and `netft-cli-0.1.0-windows-x86_64.zip`.
- Produces: `SHA256SUMS` and artifact inventory validation.
- Consumes: release-mode `netft` binary and license files.

- [ ] **Step 1: Write deterministic package tests**

```python
def test_package_contains_only_runtime_payload(tmp_path: Path) -> None:
    archive = package_release(fake_binary(tmp_path), "0.1.0", "linux-x86_64")
    assert archive.members() == [
        "netft-cli-0.1.0/LICENSE",
        "netft-cli-0.1.0/LICENSES/curl.txt",
        "netft-cli-0.1.0/LICENSES/netft-cpp.txt",
        "netft-cli-0.1.0/netft",
    ]


def test_inventory_requires_all_five_platforms(tmp_path: Path) -> None:
    with pytest.raises(ReleaseError, match="missing platform"):
        validate_inventory(tmp_path, version="0.1.0")
```

- [ ] **Step 2: Run and verify failure**

```bash
pixi run python -m pytest test/artifact/test_package_release.py \
  test/artifact/test_release_inventory.py -q
```

Expected: missing packaging modules.

- [ ] **Step 3: Adapt the audited static curl build**

Pin exactly:

```text
curl version: 8.21.0
source SHA-256: aa1b66a70eace83dc624508745646c08ae561de512ab403adffb93ac87fc72e6
protocols: HTTP only
shared library: disabled
```

Adapt pyNetFT's audited scripts, renaming environment variables to `NETFT_CLI_CURL_PREFIX`, `NETFT_CLI_CURL_ARCHIVE_CACHE`, and `NETFT_CLI_BUILD_JOBS`. Preserve checksum verification and Windows Visual Studio 17/18 generator detection.

- [ ] **Step 4: Implement deterministic archives and dependency checks**

`package_release.py` normalizes member ordering and timestamps from `SOURCE_DATE_EPOCH`. `check_release.py` requires the five exact assets, validates archive roots, runs `netft --version`, and rejects a dynamic `libcurl` dependency using `ldd`, `otool -L`, or PE import inspection.

- [ ] **Step 5: Build and inspect the current Linux artifact**

```bash
NETFT_CLI_CURL_PREFIX="$PWD/build/curl" bash tools/build_static_curl.sh
cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCURL_USE_STATIC_LIBS=ON \
  -DCURL_ROOT="$PWD/build/curl"
cmake --build build/release
python tools/package_release.py \
  --binary build/release/netft \
  --version 0.1.0 \
  --target linux-x86_64 \
  --output dist
python tools/check_release.py dist --version 0.1.0 --allow-partial
```

Expected: archive passes layout and dynamic-dependency checks.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tools test/artifact
git commit -m "build: package self-contained native releases"
```

### Task 11: Codex-style user installers

**Files:**
- Create: `scripts/install/install.sh`
- Create: `scripts/install/install.ps1`
- Create: `test/artifact/install_sh_test.py`
- Create: `test/artifact/install_ps1_test.ps1`

**Interfaces:**
- Produces: `install.sh [--version VERSION] [--bin-dir PATH]`.
- Produces: `install.ps1 [-Version VERSION] [-BinDir PATH] [-NoModifyPath]`.
- Consumes: Task 10 release assets and `SHA256SUMS`.

- [ ] **Step 1: Write installer tests against a local release fixture**

Test platform mapping, explicit versions, checksum rejection, archive rejection, atomic replacement, failed-update preservation, custom destination, and PATH behavior. The tests use temporary directories and a local HTTP fixture; they never touch the real home directory or user PATH.

```python
def test_install_sh_preserves_previous_binary_on_checksum_failure(release_server, tmp_path):
    old = install_fake_binary(tmp_path / "bin", version="0.0.9")
    release_server.corrupt("netft-cli-0.1.0-linux-x86_64.tar.gz")
    result = run_installer(release_server, bin_dir=old.parent)
    assert result.returncode != 0
    assert old.read_bytes() == fake_binary_bytes("0.0.9")
```

- [ ] **Step 2: Run and verify failure**

```bash
pixi run python -m pytest test/artifact/install_sh_test.py -q
pwsh -NoProfile -File test/artifact/install_ps1_test.ps1
```

Expected: installer files are absent.

- [ ] **Step 3: Implement POSIX installer**

Detect `uname -s` and `uname -m`, map to the five-target naming contract, resolve the latest non-prerelease when no version is supplied, download with curl or wget, verify the exact checksum entry, extract in a temporary directory under the destination filesystem, then atomically rename `netft` into place. Print a PATH instruction when `~/.local/bin` is absent from PATH; do not edit shell startup files.

- [ ] **Step 4: Implement PowerShell installer**

Use `Invoke-WebRequest`, .NET SHA-256, `Expand-Archive`, and `Move-Item`. Default to `%LOCALAPPDATA%\netft\bin`; add that exact directory to the user PATH unless `-NoModifyPath` is supplied. Normalize and deduplicate PATH entries without touching the machine-level PATH.

- [ ] **Step 5: Run isolated installer tests**

```bash
pixi run python -m pytest test/artifact/install_sh_test.py -q
pwsh -NoProfile -File test/artifact/install_ps1_test.ps1
```

Expected: all pass and the real environment remains unchanged.

- [ ] **Step 6: Commit**

```bash
git add scripts/install test/artifact
git commit -m "feat: add native CLI installers"
```

### Task 12: CI, security scanning, and staged release workflow

**Files:**
- Create: `.github/dependabot.yml`
- Create: `.github/release-allowed-signers`
- Create: `.github/workflows/ci.yml`
- Create: `.github/workflows/codeql.yml`
- Create: `.github/workflows/coverage.yml`
- Create: `.github/workflows/release.yml`
- Create: `test/workflows/test_release_workflow.py`
- Create: `.codecov.yml`

**Interfaces:**
- Produces: required native CI on Linux x86_64/arm64, macOS x86_64/arm64, and Windows x86_64.
- Produces: signed-tag, draft-first, all-assets-before-publish release pipeline.
- Consumes: Tasks 9-11 quality, package, and installer commands.

- [ ] **Step 1: Write workflow contract tests**

Parse YAML structurally and require five release targets, pinned action SHAs, `persist-credentials: false`, read-only default permissions, a complete-asset assembly gate, installer smoke jobs, and draft publication before public release. Do not assert job display names or prose.

- [ ] **Step 2: Add native CI**

Use the repository's already audited action revisions:

```text
actions/checkout: 3d3c42e5aac5ba805825da76410c181273ba90b1
prefix-dev/setup-pixi: a09b6247153796b190642a2b53fac4241043cf6f
actions/upload-artifact: 043fb46d1a93c77aae656e7c1c64a875d1fc6a0a
actions/download-artifact: 3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c
actions/attest-build-provenance: 0f67c3f4856b2e3261c31976d6725780e5e4c373
github/codeql-action: adfda868f108ac4222129de456ea554034a27db7
codecov/codecov-action: a99c28d3f0da835de33ff2feb2e15691c7b9641f
```

CI runners are `ubuntu-24.04`, `ubuntu-24.04-arm`, `macos-15-intel`, `macos-15`, and `windows-2025`. Linux runs `pixi run check`; every native runner builds and runs CTest plus an executable smoke test.

- [ ] **Step 3: Add CodeQL and coverage**

CodeQL uses manual C/C++ build mode and weekly scheduling. Coverage uploads gcovr XML through Codecov OIDC with `fail_ci_if_error: true`; `.codecov.yml` may report project/patch changes but defines no target percentage and no blocking status.

- [ ] **Step 4: Implement staged release**

Validate that `v0.1.0` is an authorized signed tag on `main` and matches CMake and CHANGELOG. Build five static-curl artifacts in parallel, assemble and validate the exact inventory, attest all archives, create/update a draft release, smoke-install draft assets on all five targets, and only then run:

```bash
gh release edit "${GITHUB_REF_NAME}" \
  --repo "${GITHUB_REPOSITORY}" \
  --draft=false
```

The release notes extractor must join wrapped Markdown bullet continuations into normal paragraphs and must not insert fixed-width line breaks.

- [ ] **Step 5: Validate workflows locally**

```bash
pixi run actionlint
pixi run python -m pytest test/workflows/test_release_workflow.py -q
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add .github .codecov.yml test/workflows
git commit -m "ci: validate and publish native releases"
```

### Task 13: Reader documentation and repository metadata

**Files:**
- Create: `README.md`
- Create: `CONTRIBUTING.md`
- Create: `CHANGELOG.md`
- Create: `SECURITY.md`
- Create: `.github/ISSUE_TEMPLATE/bug_report.yml`
- Create: `.github/ISSUE_TEMPLATE/feature_request.yml`
- Create: `.github/ISSUE_TEMPLATE/hardware_compatibility.yml`
- Create: `.github/PULL_REQUEST_TEMPLATE.md`
- Create: `.github/CODEOWNERS`

**Interfaces:**
- Produces: public reader and contributor contract for `netft-cli 0.1.0`.
- Consumes: final command syntax, installers, support matrix, snapshot workflow, and security model.

- [ ] **Step 1: Write the README for real users**

Use concise sections in this order: badges, one-paragraph description, Features, Installation, Usage, Supported platforms, Security, Contributing, License. Installation leads with:

```bash
curl -fsSL https://raw.githubusercontent.com/netft/netft-cli/main/scripts/install/install.sh | sh
```

and:

```powershell
irm https://raw.githubusercontent.com/netft/netft-cli/main/scripts/install/install.ps1 | iex
```

Then link GitHub Releases and provide a short source-build section. Pixi belongs only in CONTRIBUTING. Do not add Docker instructions, checksum tutorials, development status prose, release-process prose, architecture documents, or planning documents.

- [ ] **Step 2: Document the three commands and safety boundary**

Examples use `192.168.1.1`:

```bash
netft info 192.168.1.1
netft monitor 192.168.1.1
netft monitor 192.168.1.1 --format ndjson --rate 20
netft bias 192.168.1.1
```

Explain that monitor output is sampled latest-value data, not lossless recording, and that bias changes subsequent sensor output.

- [ ] **Step 3: Add contributor and governance files**

CONTRIBUTING documents `pixi run check`, fake-sensor tests, opt-in hardware tests, core synchronization, and the upstream-first rule. SECURITY combines software and network-security guidance and directs private reports through GitHub Security Advisories. Templates ask for structured environment and sensor details without requesting private network addresses in public issues.

- [ ] **Step 4: Review repository language and local-address hygiene**

```bash
rg -n '192\\.168\\.31\\.100|TODO|TBD|work in progress|temporary|intermediate' \
  --glob '!.git/**' --glob '!.superpowers/**' .
git grep -nP '[\x{4e00}-\x{9fff}]' -- ':!.superpowers/**'
```

Expected: no matches outside intentional test fixture tokens; repository files contain no Chinese.

- [ ] **Step 5: Commit**

```bash
git add README.md CONTRIBUTING.md CHANGELOG.md SECURITY.md .github
git commit -m "docs: document the netft CLI"
```

### Task 14: Final local verification and first-publication gate

**Files:**
- Modify: files found by final verification only when required to satisfy an existing contract.
- Do not add: public planning, progress, review-diff, or task-report artifacts.

**Interfaces:**
- Produces: locally release-ready `netft-cli 0.1.0`.
- Consumes: every prior task.

- [ ] **Step 1: Verify repository state and controlled content**

```bash
git status --short
python tools/sync_core.py verify
rg -n '192\\.168\\.31\\.100' \
  --glob '!.git/**' --glob '!.superpowers/**' . && exit 1 || true
git diff --check
```

Expected: clean worktree, valid snapshot, no local sensor address, no whitespace errors.

- [ ] **Step 2: Run the complete local gate**

```bash
pixi run check
pixi run sanitizers
pixi run actionlint
pixi run python -m pytest test/tools test/artifact test/workflows -q
```

Expected: all pass.

- [ ] **Step 3: Perform read-only real-sensor validation**

After obtaining the sensor address only at runtime:

```bash
NETFT_SENSOR_HOST=<runtime-address> pixi run hardware-test
```

Expected: configuration is read from the device, sequences increase, and raw/scaled values are finite. Do not run `hardware-bias-test` without a fresh explicit authorization.

- [ ] **Step 4: Verify native CI before release**

Create the `netft/netft-cli` repository and push a review branch only after explicit user authorization. Open a PR, require all five native CI targets plus CodeQL and coverage upload to complete, and resolve any failures without weakening tests or release inventory.

- [ ] **Step 5: Prepare but do not publish `v0.1.0`**

Confirm branch protection, the `netft-release` allowed signer, GitHub Release environment protection, and repository security settings. Stop for explicit user approval before rewriting local history into a public Initial commit, force-updating any remote branch, creating the signed tag, or publishing the release.

- [ ] **Step 6: Record verification evidence**

Append no progress file to the public repository. Report the exact commit, core snapshot tag/SHA, commands run, CI URLs, real-hardware scope, and any untested platform limitation directly in the implementation handoff.
