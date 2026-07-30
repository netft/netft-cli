#include "commands/record.hpp"

#include "app/error.hpp"
#include "recording/csv_writer.hpp"
#include "recording/ndjson_writer.hpp"
#include "recording/output_file.hpp"
#include "recording/recorder.hpp"

#include <exception>
#include <memory>
#include <ostream>
#include <string>

namespace netft_cli {
namespace {

OutputFormat resolve_format(const RecordOptions &options) {
  if (options.format == OutputFormat::Csv || options.format == OutputFormat::Ndjson) {
    return options.format;
  }
  if (options.format != OutputFormat::Automatic) {
    throw AppError{ExitCode::Usage, "output format is not supported by this command"};
  }
  const auto extension = options.output.extension().string();
  if (extension == ".csv") {
    return OutputFormat::Csv;
  }
  if (extension == ".ndjson") {
    return OutputFormat::Ndjson;
  }
  throw AppError{ExitCode::Usage,
                 "recording format must be selected or implied by .csv or .ndjson"};
}

void discover(const RecordOptions &options, SensorBackend &backend) {
  try {
    static_cast<void>(backend.discover(options.connection));
  } catch (const std::exception &) {
    throw AppError{ExitCode::Discovery, "sensor discovery failed"};
  }
}

void write_progress(OutputContext &output, const TerminalOptions &terminal, std::string_view text) {
  if (terminal.verbosity == Verbosity::Quiet) {
    return;
  }
  output.standard_error << text << '\n';
  output.standard_error.flush();
  if (!output.standard_error) {
    throw AppError{ExitCode::Io, "failed to write recording progress"};
  }
}

} // namespace

int run_record(const RecordOptions &options, SensorBackend &backend, OutputContext &output,
               InterruptFlag &interrupt, Clock &clock, WallClock &wall_clock,
               Filesystem &filesystem, std::size_t queue_capacity) {
  const auto format = resolve_format(options);
  OutputFile file(options.output, filesystem);
  std::unique_ptr<RecordingWriter> writer;
  if (format == OutputFormat::Csv) {
    writer = std::make_unique<CsvRecordingWriter>(file.stream());
  } else {
    writer = std::make_unique<NdjsonRecordingWriter>(file.stream());
  }
  discover(options, backend);
  write_progress(output, options.terminal, "Recording started");

  Recorder recorder(backend, options.connection, *writer, clock, wall_clock, interrupt);
  const RecorderLimits limits{options.duration, options.count, queue_capacity};
  const auto result = recorder.run(limits, [&] { file.finalize(); });

  write_progress(output, options.terminal,
                 "Recording saved: " + std::to_string(result.written_count) + " samples");
  return result.interrupted ? static_cast<int>(ExitCode::Interrupted) : 0;
}

} // namespace netft_cli
