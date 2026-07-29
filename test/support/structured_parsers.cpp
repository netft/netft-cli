#include "support/structured_parsers.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace netft_cli::test {
namespace {

std::vector<std::vector<std::string>> parse_csv_rows(std::string_view text) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool quoted = false;

  for (std::size_t index = 0; index < text.size(); ++index) {
    const char character = text[index];
    if (quoted) {
      if (character == '"' && index + 1 < text.size() && text[index + 1] == '"') {
        field.push_back('"');
        ++index;
      } else if (character == '"') {
        quoted = false;
      } else {
        field.push_back(character);
      }
      continue;
    }

    if (character == '"' && field.empty()) {
      quoted = true;
    } else if (character == ',') {
      row.push_back(std::move(field));
      field.clear();
    } else if (character == '\n' || character == '\r') {
      row.push_back(std::move(field));
      field.clear();
      rows.push_back(std::move(row));
      row.clear();
      if (character == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
        ++index;
      }
    } else {
      field.push_back(character);
    }
  }

  if (quoted) {
    throw std::runtime_error("unterminated quoted CSV field");
  }
  if (!field.empty() || !row.empty()) {
    row.push_back(std::move(field));
    rows.push_back(std::move(row));
  }
  return rows;
}

} // namespace

Json parse_json(std::string_view text) { return Json::parse(text); }

std::vector<Json> parse_ndjson(std::string_view text) {
  std::vector<Json> documents;
  std::istringstream stream{std::string{text}};
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      documents.push_back(Json::parse(line));
    }
  }
  return documents;
}

bool CsvTable::has_columns(std::initializer_list<std::string_view> names) const {
  return std::all_of(names.begin(), names.end(), [this](std::string_view name) {
    return std::find(header.begin(), header.end(), name) != header.end();
  });
}

CsvTable parse_csv(std::string_view text) {
  auto parsed_rows = parse_csv_rows(text);
  CsvTable table;
  if (!parsed_rows.empty()) {
    table.header = std::move(parsed_rows.front());
    parsed_rows.erase(parsed_rows.begin());
  }
  table.rows = std::move(parsed_rows);
  return table;
}

} // namespace netft_cli::test
