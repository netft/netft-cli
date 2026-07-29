#include "output/confirmation.hpp"

#include "app/error.hpp"
#include "output/terminal.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <ostream>
#include <string>
#include <utility>

namespace netft_cli {
namespace {

void write_values(std::ostream &stream, const SampleRecord &sample) {
  stream << "Current raw: [";
  for (std::size_t index = 0; index < sample.raw.size(); ++index) {
    if (index != 0) {
      stream << ", ";
    }
    stream << sample.raw[index];
  }
  stream << "]\nCurrent scaled: [";
  for (std::size_t index = 0; index < sample.scaled.size(); ++index) {
    if (index != 0) {
      stream << ", ";
    }
    stream << std::setprecision(6) << sample.scaled[index];
  }
  stream << "]\n";
}

std::string normalized(std::string response) {
  const auto not_space = [](unsigned char character) { return std::isspace(character) == 0; };
  response.erase(response.begin(), std::find_if(response.begin(), response.end(), not_space));
  response.erase(std::find_if(response.rbegin(), response.rend(), not_space).base(),
                 response.end());
  std::transform(response.begin(), response.end(), response.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return response;
}

} // namespace

bool TerminalConfirmation::confirm(const BiasPreview &preview) {
  output_.standard_error << render_configuration_text(preview.configuration);
  write_values(output_.standard_error, preview.sample);
  output_.standard_error << "Apply software bias? [y/N] ";
  output_.standard_error.flush();
  if (!output_.standard_error) {
    throw AppError{ExitCode::Io, "failed to write confirmation prompt"};
  }

  auto result = line_reader_.read_line(interrupt_);
  switch (result.status) {
  case LineReadStatus::Eof:
    return false;
  case LineReadStatus::Interrupted:
    interrupt_.request();
    return false;
  case LineReadStatus::Error:
    throw AppError{ExitCode::Io, "failed to read confirmation input"};
  case LineReadStatus::Line:
    break;
  }
  auto response = normalized(std::move(result.line));
  return response == "y" || response == "yes";
}

} // namespace netft_cli
