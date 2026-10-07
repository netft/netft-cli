#pragma once

#include "diagnostics/result.hpp"
#include "output/records.hpp"

#include <iosfwd>

namespace netft_cli {
struct CommandSchema;
struct RecorderResult;
void write_recording_metadata(std::ostream &stream, const RecorderResult &result);
void write_command_schema(std::ostream &stream, const CommandSchema &schema,
                          std::string_view version, std::string_view source_commit,
                          bool source_dirty);

void write_json(std::ostream &stream, const ConfigurationRecord &record);
void write_json(std::ostream &stream, const SampleRecord &record);
void write_json(std::ostream &stream, const BiasRecord &record);
void write_json(std::ostream &stream, const DiagnosticResult &result);

void write_ndjson(std::ostream &stream, const ConfigurationRecord &record);
void write_ndjson(std::ostream &stream, const SampleRecord &record);
void write_ndjson(std::ostream &stream, const BiasRecord &record);

} // namespace netft_cli
