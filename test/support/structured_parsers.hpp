#pragma once

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace netft_cli::test {

using Json = nlohmann::json;

Json parse_json(std::string_view text);
std::vector<Json> parse_ndjson(std::string_view text);

struct CsvTable {
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> rows;

  std::size_t header_count() const noexcept { return header.empty() ? 0U : 1U; }
  std::size_t row_count() const noexcept { return rows.size(); }
  bool has_columns(std::initializer_list<std::string_view> names) const;
};

CsvTable parse_csv(std::string_view text);

} // namespace netft_cli::test
