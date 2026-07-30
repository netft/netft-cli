#include "application.hpp"

#include "app/error.hpp"

#include <netft/client.hpp>
#include <netft/discovery.hpp>

#include <cstdio>
#include <exception>
#include <ios>
#include <string_view>
#include <vector>

namespace {

std::string_view category(netft_cli::ExitCode code) noexcept {
  using netft_cli::ExitCode;
  switch (code) {
  case ExitCode::Usage:
    return "usage";
  case ExitCode::Discovery:
    return "discovery";
  case ExitCode::Stream:
    return "stream";
  case ExitCode::Sensor:
    return "sensor";
  case ExitCode::Io:
    return "io";
  case ExitCode::Acceptance:
    return "acceptance";
  case ExitCode::Recording:
    return "recording";
  case ExitCode::Interrupted:
    return "interrupted";
  }
  return "io";
}

int report(netft_cli::ExitCode code, std::string_view message) noexcept {
  const auto name = category(code);
  static_cast<void>(std::fputs("error[", stderr));
  static_cast<void>(std::fwrite(name.data(), 1, name.size(), stderr));
  static_cast<void>(std::fputs("]: ", stderr));
  static_cast<void>(std::fwrite(message.data(), 1, message.size(), stderr));
  static_cast<void>(std::fputc('\n', stderr));
  return static_cast<int>(code);
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index) {
      arguments.emplace_back(argv[index]);
    }
    netft_cli::NativeEnvironment environment;
    return netft_cli::run_application(arguments, environment);
  } catch (const netft_cli::AppError &error) {
    return report(error.code(), error.what());
  } catch (const netft::DiscoveryError &error) {
    return report(netft_cli::ExitCode::Discovery, error.what());
  } catch (const netft::NotConnectedError &error) {
    return report(netft_cli::ExitCode::Stream, error.what());
  } catch (const std::ios_base::failure &error) {
    return report(netft_cli::ExitCode::Io, error.what());
  } catch (const std::exception &error) {
    return report(netft_cli::ExitCode::Io, error.what());
  } catch (...) {
    return report(netft_cli::ExitCode::Io, "unexpected application failure");
  }
}
