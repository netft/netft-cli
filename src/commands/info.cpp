#include "commands/info.hpp"

#include "app/error.hpp"
#include "output/json.hpp"
#include "output/records.hpp"
#include "output/terminal.hpp"

#include <exception>
#include <ostream>

namespace netft_cli {
namespace {

OutputFormat resolve_format(const InfoOptions &options, const OutputContext &output) {
  if (options.format != OutputFormat::Automatic) {
    return options.format;
  }
  return !options.output.has_value() && output.output_is_terminal ? OutputFormat::Text
                                                                  : OutputFormat::Json;
}

netft::SensorConfiguration discover(const InfoOptions &options, SensorBackend &backend) {
  try {
    return backend.discover(options.connection);
  } catch (const std::exception &) {
    throw AppError{ExitCode::Discovery, "sensor discovery failed"};
  }
}

} // namespace

int run_info(const InfoOptions &options, SensorBackend &backend, OutputContext &output) {
  OutputHandle destination = options.output.has_value()
                                 ? OutputHandle::file(*options.output)
                                 : OutputHandle::standard(output.standard_output);
  const auto record = make_configuration_record(options.connection, discover(options, backend));

  switch (resolve_format(options, output)) {
  case OutputFormat::Text:
    destination.stream() << render_configuration_text(record);
    break;
  case OutputFormat::Json:
    write_json(destination.stream(), record);
    break;
  case OutputFormat::Automatic:
  case OutputFormat::Table:
  case OutputFormat::Ndjson:
  case OutputFormat::Csv:
    throw AppError{ExitCode::Usage, "output format is not supported by this command"};
  }
  destination.flush();
  return 0;
}

} // namespace netft_cli
