#pragma once

#include "output/records.hpp"

#include <iosfwd>

namespace netft_cli {

void write_json(std::ostream &stream, const ConfigurationRecord &record);
void write_json(std::ostream &stream, const SampleRecord &record);
void write_json(std::ostream &stream, const BiasRecord &record);

void write_ndjson(std::ostream &stream, const ConfigurationRecord &record);
void write_ndjson(std::ostream &stream, const SampleRecord &record);
void write_ndjson(std::ostream &stream, const BiasRecord &record);

} // namespace netft_cli
