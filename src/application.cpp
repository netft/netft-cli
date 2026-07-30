#include "application.hpp"

#include "app/error.hpp"
#include "cli/parser.hpp"
#include "commands/bias.hpp"
#include "commands/check.hpp"
#include "commands/info.hpp"
#include "commands/monitor.hpp"
#include "commands/record.hpp"
#include "config/resolver.hpp"
#include "platform/line_reader.hpp"
#include "sensor/netft_backend.hpp"

#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace netft_cli {
namespace {

constexpr int standard_input_descriptor = 0;
constexpr int standard_output_descriptor = 1;

bool is_terminal(int descriptor) noexcept {
#ifdef _WIN32
  return ::_isatty(descriptor) != 0;
#else
  return ::isatty(descriptor) != 0;
#endif
}

template <typename... Functions> struct Overloaded : Functions... {
  using Functions::operator()...;
};
template <typename... Functions> Overloaded(Functions...) -> Overloaded<Functions...>;

void write_general_help(std::ostream &stream) {
  stream << "Usage: netft <command> [options]\n\n"
            "Commands:\n"
            "  info <HOST>     Show sensor configuration\n"
            "  monitor <HOST>  Stream current force and torque samples\n"
            "  bias <HOST>     Apply a software bias after confirmation\n"
            "  help [COMMAND]  Show help\n\n"
            "Global options:\n"
            "  --help          Show help\n"
            "  --version       Show version\n";
}

void write_command_help(std::ostream &stream, const std::string &topic) {
  if (topic == "monitor") {
    stream << "Usage: netft monitor <HOST> [--rate HZ] [--duration DURATION]\n"
              "                     [--format table|ndjson|csv] [connection options]\n";
  } else if (topic == "bias") {
    stream << "Usage: netft bias <HOST> [--yes] [--format text|json] [connection options]\n";
  } else {
    stream << "Usage: netft info <HOST> [--format text|json] [connection options]\n";
  }
  stream << "\nConnection options:\n"
            "  --http-port PORT\n"
            "  --rdt-port PORT\n"
            "  --timeout DURATION\n"
            "  --output PATH\n";
}

} // namespace

class NativeEnvironment::Implementation {
public:
  Implementation()
      : output{std::cin,
               std::cout,
               std::cerr,
               is_terminal(standard_input_descriptor),
               is_terminal(standard_output_descriptor),
               {}},
        interrupt_handler{interrupt},
        line_reader{make_interruptible_line_reader(standard_input_descriptor)},
        confirmation{output, interrupt, *line_reader} {}

  OutputContext output;
  NetftBackend backend;
  InterruptFlag interrupt;
  InterruptHandler interrupt_handler;
  SystemClock clock;
  SystemWallClock wall_clock;
  NativeFilesystem filesystem;
  std::unique_ptr<InterruptibleLineReader> line_reader;
  TerminalConfirmation confirmation;
};

NativeEnvironment::NativeEnvironment() : implementation_(std::make_unique<Implementation>()) {}

NativeEnvironment::~NativeEnvironment() = default;

SensorBackend &NativeEnvironment::backend() { return implementation_->backend; }

OutputContext &NativeEnvironment::output() { return implementation_->output; }

Confirmation &NativeEnvironment::confirmation() { return implementation_->confirmation; }

InterruptFlag &NativeEnvironment::interrupt() { return implementation_->interrupt; }

Clock &NativeEnvironment::clock() { return implementation_->clock; }

WallClock &NativeEnvironment::wall_clock() { return implementation_->wall_clock; }

Filesystem &NativeEnvironment::filesystem() { return implementation_->filesystem; }

EnvironmentMap NativeEnvironment::environment() const { return read_process_environment(); }

int NativeEnvironment::show_help(const ShowHelp &help) {
  if (help.topic == "general") {
    write_general_help(implementation_->output.standard_output);
  } else {
    write_command_help(implementation_->output.standard_output, help.topic);
  }
  implementation_->output.standard_output.flush();
  if (!implementation_->output.standard_output) {
    throw AppError{ExitCode::Io, "failed to write help output"};
  }
  return 0;
}

int NativeEnvironment::show_version() {
  implementation_->output.standard_output << "netft " NETFT_CLI_VERSION "\n";
  implementation_->output.standard_output.flush();
  if (!implementation_->output.standard_output) {
    throw AppError{ExitCode::Io, "failed to write version output"};
  }
  return 0;
}

int run_application(const std::vector<std::string_view> &arguments, AppEnvironment &environment) {
  const auto action = resolve_options(parse_arguments(arguments), environment.environment());
  return std::visit(
      Overloaded{[&](const ShowHelp &value) { return environment.show_help(value); },
                 [&](const ShowVersion &) { return environment.show_version(); },
                 [&](const InfoOptions &value) {
                   environment.output().terminal = value.terminal;
                   return run_info(value, environment.backend(), environment.output());
                 },
                 [&](const MonitorOptions &value) {
                   environment.output().terminal = value.terminal;
                   return run_monitor(value, environment.backend(), environment.output(),
                                      environment.interrupt(), environment.clock());
                 },
                 [&](const CheckOptions &value) {
                   environment.output().terminal = value.terminal;
                   return run_check(value, environment.backend(), environment.output(),
                                    environment.interrupt(), environment.clock());
                 },
                 [&](const RecordOptions &value) {
                   environment.output().terminal = value.terminal;
                   return run_record(value, environment.backend(), environment.output(),
                                     environment.interrupt(), environment.clock(),
                                     environment.wall_clock(), environment.filesystem());
                 },
                 [&](const BiasOptions &value) {
                   environment.output().terminal = value.terminal;
                   return run_bias(value, environment.backend(), environment.output(),
                                   environment.confirmation(), environment.interrupt());
                 },
                 [&](const auto &) -> int {
                   throw AppError{ExitCode::Usage, "command is not available yet"};
                 }},
      action);
}

} // namespace netft_cli
