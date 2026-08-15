#include "TelemetryReplay.h"

#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/simulator.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using flying::core_sim::AdvanceReport;
using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::AuthoritativeState;
using flying::core_sim::CallerFrameInput;
using flying::core_sim::ControlInputSample;
using flying::core_sim::CoreSimulator;
using flying::core_sim::RigidBodyParameters;
using flying::telemetry_replay::ReplayCompatibilityPolicy;
using flying::telemetry_replay::ReplayEnvironment;
using flying::telemetry_replay::ReplayPlayer;
using flying::telemetry_replay::TelemetryExporter;
using flying::telemetry_replay::TelemetryRecorder;
using flying::telemetry_replay::TelemetryRecording;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void remove_temp_file(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

struct JsonValue {
  enum class Type {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
  };

  Type type{Type::Null};
  bool boolean_value{};
  double number_value{};
  std::string string_value;
  std::vector<JsonValue> array_value;
  std::map<std::string, JsonValue> object_value;

  [[nodiscard]] const JsonValue* find(std::string_view key) const {
    if (type != Type::Object) {
      return nullptr;
    }
    const auto found = object_value.find(std::string{key});
    return found == object_value.end() ? nullptr : &found->second;
  }
};

class JsonParser {
public:
  explicit JsonParser(std::string_view text) : text_{text} {}

  JsonValue parse() {
    JsonValue value = parse_value();
    skip_whitespace();
    require(position_ == text_.size(), "JSON document must not contain trailing tokens");
    return value;
  }

private:
  std::string_view text_;
  std::size_t position_{};

  void skip_whitespace() {
    while (position_ < text_.size()) {
      const char ch = text_[position_];
      if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
        return;
      }
      ++position_;
    }
  }

  [[nodiscard]] char peek() {
    skip_whitespace();
    require(position_ < text_.size(), "JSON value expected");
    return text_[position_];
  }

  void consume(char expected) {
    skip_whitespace();
    require(position_ < text_.size() && text_[position_] == expected,
            "JSON punctuation mismatch");
    ++position_;
  }

  bool consume_literal(std::string_view literal) {
    skip_whitespace();
    if (text_.substr(position_, literal.size()) != literal) {
      return false;
    }
    position_ += literal.size();
    return true;
  }

  JsonValue parse_value() {
    const char ch = peek();
    if (ch == '"') {
      JsonValue value;
      value.type = JsonValue::Type::String;
      value.string_value = parse_string();
      return value;
    }
    if (ch == '{') {
      return parse_object();
    }
    if (ch == '[') {
      return parse_array();
    }
    if (ch == 't') {
      require(consume_literal("true"), "invalid JSON true literal");
      JsonValue value;
      value.type = JsonValue::Type::Boolean;
      value.boolean_value = true;
      return value;
    }
    if (ch == 'f') {
      require(consume_literal("false"), "invalid JSON false literal");
      JsonValue value;
      value.type = JsonValue::Type::Boolean;
      return value;
    }
    if (ch == 'n') {
      require(consume_literal("null"), "invalid JSON null literal");
      return {};
    }
    return parse_number();
  }

  std::string parse_string() {
    consume('"');
    std::string value;
    while (position_ < text_.size()) {
      const char ch = text_[position_++];
      if (ch == '"') {
        return value;
      }
      if (ch != '\\') {
        value.push_back(ch);
        continue;
      }
      require(position_ < text_.size(), "unterminated JSON escape");
      const char escaped = text_[position_++];
      switch (escaped) {
      case '"':
      case '\\':
      case '/':
        value.push_back(escaped);
        break;
      case 'b':
        value.push_back('\b');
        break;
      case 'f':
        value.push_back('\f');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      case 'u':
        require(position_ + 4 <= text_.size(), "short JSON unicode escape");
        value.push_back('?');
        position_ += 4;
        break;
      default:
        throw std::runtime_error("unsupported JSON escape");
      }
    }
    throw std::runtime_error("unterminated JSON string");
  }

  JsonValue parse_number() {
    skip_whitespace();
    const std::size_t start = position_;
    if (position_ < text_.size() && text_[position_] == '-') {
      ++position_;
    }
    while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
      ++position_;
    }
    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
        ++position_;
      }
    }
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) {
        ++position_;
      }
      while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
        ++position_;
      }
    }
    require(position_ > start, "invalid JSON number");
    JsonValue value;
    value.type = JsonValue::Type::Number;
    value.number_value = std::stod(std::string{text_.substr(start, position_ - start)});
    return value;
  }

  JsonValue parse_array() {
    consume('[');
    JsonValue value;
    value.type = JsonValue::Type::Array;
    if (peek() == ']') {
      consume(']');
      return value;
    }
    while (true) {
      value.array_value.push_back(parse_value());
      const char ch = peek();
      if (ch == ']') {
        consume(']');
        return value;
      }
      consume(',');
    }
  }

  JsonValue parse_object() {
    consume('{');
    JsonValue value;
    value.type = JsonValue::Type::Object;
    if (peek() == '}') {
      consume('}');
      return value;
    }
    while (true) {
      const std::string key = parse_string();
      consume(':');
      value.object_value.emplace(key, parse_value());
      const char ch = peek();
      if (ch == '}') {
        consume('}');
        return value;
      }
      consume(',');
    }
  }
};

[[nodiscard]] const JsonValue& require_member(const JsonValue& value, std::string_view key) {
  const JsonValue* member = value.find(key);
  if (!member) {
    throw std::runtime_error("missing JSON member: " + std::string{key});
  }
  return *member;
}

[[nodiscard]] std::string require_string(const JsonValue& value, std::string_view context) {
  require(value.type == JsonValue::Type::String,
          ("expected JSON string: " + std::string{context}).c_str());
  return value.string_value;
}

void validate_schema_type(const JsonValue& instance,
                          std::string_view type,
                          std::string_view path) {
  const bool valid =
      (type == "object" && instance.type == JsonValue::Type::Object) ||
      (type == "array" && instance.type == JsonValue::Type::Array) ||
      (type == "string" && instance.type == JsonValue::Type::String) ||
      (type == "number" && instance.type == JsonValue::Type::Number) ||
      (type == "integer" && instance.type == JsonValue::Type::Number &&
           std::floor(instance.number_value) == instance.number_value) ||
      (type == "boolean" && instance.type == JsonValue::Type::Boolean);
  if (!valid) {
    throw std::runtime_error("JSON schema type mismatch at " + std::string{path});
  }
}

void validate_against_schema(const JsonValue& instance,
                             const JsonValue& schema,
                             std::string path = "$") {
  if (const JsonValue* type = schema.find("type")) {
    validate_schema_type(instance, require_string(*type, path + ".type"), path);
  }
  if (const JsonValue* const_value = schema.find("const")) {
    require(instance.type == const_value->type, "JSON schema const type mismatch");
    if (const_value->type == JsonValue::Type::String) {
      require(instance.string_value == const_value->string_value,
              ("JSON schema const mismatch at " + path).c_str());
    }
  }
  if (const JsonValue* min_items = schema.find("minItems")) {
    require(instance.type == JsonValue::Type::Array, "minItems applies to arrays");
    require(instance.array_value.size() >= static_cast<std::size_t>(min_items->number_value),
            ("JSON schema minItems failed at " + path).c_str());
  }
  if (const JsonValue* required = schema.find("required")) {
    require(instance.type == JsonValue::Type::Object, "required applies to objects");
    for (const JsonValue& required_key : required->array_value) {
      const std::string key = require_string(required_key, path + ".required");
      require(instance.find(key) != nullptr,
              ("JSON schema required member missing at " + path + "." + key).c_str());
    }
  }
  if (const JsonValue* properties = schema.find("properties")) {
    require(properties->type == JsonValue::Type::Object, "schema properties must be an object");
    for (const auto& [key, property_schema] : properties->object_value) {
      if (const JsonValue* child = instance.find(key)) {
        validate_against_schema(*child, property_schema, path + "." + key);
      }
    }
  }
  if (const JsonValue* items = schema.find("items")) {
    require(instance.type == JsonValue::Type::Array, "items applies to arrays");
    for (std::size_t index = 0; index < instance.array_value.size(); ++index) {
      validate_against_schema(instance.array_value[index],
                              *items,
                              path + "[" + std::to_string(index) + "]");
    }
  }
}

std::vector<std::string> parse_csv_row(std::string_view line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char ch = line[index];
    if (quoted) {
      if (ch == '"' && index + 1 < line.size() && line[index + 1] == '"') {
        field.push_back('"');
        ++index;
      } else if (ch == '"') {
        quoted = false;
      } else {
        field.push_back(ch);
      }
    } else if (ch == '"') {
      quoted = true;
    } else if (ch == ',') {
      fields.push_back(field);
      field.clear();
    } else {
      field.push_back(ch);
    }
  }
  fields.push_back(field);
  require(!quoted, "CSV row must not end inside a quoted field");
  return fields;
}

std::vector<std::string> schema_string_array(const JsonValue& schema,
                                             std::string_view extension_name,
                                             std::string_view key) {
  const JsonValue& extension = require_member(schema, extension_name);
  const JsonValue& values = require_member(extension, key);
  require(values.type == JsonValue::Type::Array, "schema extension value must be an array");
  std::vector<std::string> result;
  for (const JsonValue& value : values.array_value) {
    result.push_back(require_string(value, std::string{key}));
  }
  return result;
}

void require_contains(const std::vector<std::string>& values,
                      const std::string& expected,
                      std::string_view context) {
  for (const std::string& value : values) {
    if (value == expected) {
      return;
    }
  }
  throw std::runtime_error(std::string{context} + " missing expected value: " + expected);
}

AuthoritativeState make_initial_state() {
  AuthoritativeState state{};
  state.ecef_position_m = {3'980'010.0, 1'100'020.0, 4'870'030.0};
  state.ecef_velocity_mps = {34.0, -1.5, -0.25};
  return state;
}

AircraftControlInputSample make_aircraft_controls(std::size_t frame_index) {
  AircraftControlInputSample controls{};
  controls.aileron_norm = frame_index % 2 == 0 ? 0.08 : -0.06;
  controls.elevator_norm = -0.03;
  controls.rudder_norm = 0.01;
  controls.throttle_norm = 0.74;
  controls.flaps_norm = frame_index > 1 ? 0.1 : 0.0;
  controls.mixture_norm = 1.0;
  controls.propeller_norm = 0.96;
  return controls;
}

TelemetryRecording make_recording() {
  const RigidBodyParameters parameters{
    975.0,
    {875.0, 1'060.0, 1'420.0},
  };

  CoreSimulator simulator{parameters};
  simulator.reset(make_initial_state());

  auto metadata = flying::telemetry_replay::make_default_telemetry_metadata();
  metadata.session_id = "replay-hash-fixture";
  metadata.started_unix_ms = 1'700'000'500'000;
  metadata.scenario_location_id = "LKPR-RWY-06";
  metadata.scenario_start_mode = "cold-and-dark";
  metadata.data_packages.push_back({"czech-terrain-fixture", "2026.08.rc1"});
  metadata.data_packages.push_back({"airport-master-list-fixture", "2026.08.rc1"});

  AircraftControlInputSample initial_controls{};
  initial_controls.throttle_norm = 0.74;
  initial_controls.mixture_norm = 1.0;
  initial_controls.propeller_norm = 0.96;

  TelemetryRecorder recorder{metadata, simulator.state(), {}, initial_controls, parameters};
  const std::array<CallerFrameInput, 5> inputs{{
    {1.0 / 240.0, ControlInputSample{{1'000.0, 5.0, 70.0}, {0.0, 12.0, -2.0}}},
    {1.0 / 120.0, ControlInputSample{{1'080.0, 3.0, 72.0}, {1.0, 10.0, -3.0}}},
    {1.0 / 80.0, ControlInputSample{{1'150.0, -4.0, 68.0}, {-1.0, 9.0, -4.0}}},
    {1.0 / 240.0, ControlInputSample{{1'120.0, 0.0, 66.0}, {0.0, 8.0, -2.0}}},
    {0.005, ControlInputSample{{900.0, 0.0, 30.0}, {0.0, 4.0, 0.0}}},
  }};

  for (std::size_t index = 0; index < inputs.size(); ++index) {
    const AdvanceReport report =
        simulator.advance(inputs[index].elapsed_time_s, inputs[index].controls);
    const AircraftControlInputSample controls = make_aircraft_controls(index);
    recorder.record_advance(inputs[index].elapsed_time_s,
                            inputs[index].controls,
                            controls,
                            flying::telemetry_replay::make_engine_state_sample(controls, true),
                            report,
                            simulator.state(),
                            1'700'000'500'100 + static_cast<std::int64_t>(index));
  }

  return recorder.recording();
}

void replay_hash_is_stable_for_same_versions_and_inputs() {
  const TelemetryRecording recording = make_recording();
  require(flying::telemetry_replay::validate_telemetry_recording(recording).empty(),
          "recording must include replayable metadata, inputs, states, forces, and moments");

  ReplayEnvironment environment = flying::telemetry_replay::make_current_replay_environment();
  environment.rigid_body_parameters = recording.rigid_body_parameters;
  environment.data_packages = recording.metadata.data_packages;

  CoreSimulator replay_simulator{recording.rigid_body_parameters};
  const ReplayPlayer player;
  const auto replay =
      player.play(recording, replay_simulator, environment, ReplayCompatibilityPolicy::RefuseOnMismatch);

  require(replay.played, "compatible replay must play");
  require(replay.deterministic, "replay must reproduce every recorded state hash");
  require(replay.replayed_frames == recording.frames.size(),
          "replay must consume every recorded frame");
  require(replay.final_state_hash == recording.frames.back().state_hash,
          "replay final hash must match the recorded final hash");
}

void exported_files_match_declared_schema_contract() {
  const std::filesystem::path csv_path =
      std::filesystem::temp_directory_path() / "flying-replay-hash-schema.csv";
  const std::filesystem::path json_path =
      std::filesystem::temp_directory_path() / "flying-replay-hash-schema.json";
  remove_temp_file(csv_path);
  remove_temp_file(json_path);

  const TelemetryRecording recording = make_recording();
  const TelemetryExporter exporter;
  const auto csv = exporter.export_csv(csv_path, recording);
  const auto json = exporter.export_json(json_path, recording);
  require(csv.exported, "CSV export must succeed for a valid recording");
  require(json.exported, "JSON export must succeed for a valid recording");

  const JsonValue schema = JsonParser{read_text(
      std::filesystem::path{FLYING_REPO_SOURCE_DIR} / "schemas" /
      "telemetry.schema.json")}.parse();
  const JsonValue json_export = JsonParser{read_text(json_path)}.parse();
  validate_against_schema(json_export, schema);

  const JsonValue& metadata = require_member(json_export, "metadata");
  require(require_string(require_member(metadata, "simulation_configuration_id"),
                         "simulation_configuration_id") ==
              recording.metadata.simulation_configuration_id,
          "JSON export must include the recorded simulation configuration id");
  require(require_string(require_member(metadata, "input_profile_id"), "input_profile_id") ==
              recording.metadata.input_profile_id,
          "JSON export must include the recorded input profile id");
  const JsonValue& json_packages = require_member(metadata, "data_packages");
  require(json_packages.type == JsonValue::Type::Array,
          "JSON export data_packages must be an array");
  require(json_packages.array_value.size() == recording.metadata.data_packages.size(),
          "JSON export data package count must match the recording");
  const JsonValue& flight_path = require_member(json_export, "flight_path");
  const JsonValue& graph_series = require_member(json_export, "graph_series");
  const JsonValue& frames = require_member(json_export, "frames");
  require(flight_path.type == JsonValue::Type::Array &&
              flight_path.array_value.size() == recording.frames.size(),
          "JSON flight_path count must match recorded frames");
  require(frames.type == JsonValue::Type::Array &&
              frames.array_value.size() == recording.frames.size(),
          "JSON frame count must match recorded frames");
  require(graph_series.type == JsonValue::Type::Array && graph_series.array_value.size() >= 4,
          "JSON export must include expected graph series");
  for (const JsonValue& series : graph_series.array_value) {
    const JsonValue& points = require_member(series, "points");
    require(points.type == JsonValue::Type::Array &&
                points.array_value.size() == recording.frames.size(),
            "each JSON graph series must have one point per recorded frame");
  }
  require(require_member(require_member(frames.array_value.front(), "inputs"), "core")
              .type == JsonValue::Type::Object,
          "JSON frame must include parsed core inputs");
  require(require_member(require_member(frames.array_value.front(), "inputs"),
                         "aircraft_controls")
              .type == JsonValue::Type::Object,
          "JSON frame must include parsed aircraft controls");
  require(require_member(require_member(frames.array_value.front(), "state"),
                         "accumulated_force_body_n")
              .type == JsonValue::Type::Object,
          "JSON frame state must include authoritative forces");
  require(require_member(require_member(frames.array_value.front(), "state"),
                         "accumulated_moment_body_nm")
              .type == JsonValue::Type::Object,
          "JSON frame state must include authoritative moments");

  const std::string csv_text = read_text(csv_path);
  std::istringstream csv_lines{csv_text};
  std::string line;
  std::map<std::string, std::vector<std::vector<std::string>>> metadata_rows;
  std::vector<std::string> columns;
  std::size_t data_row_count = 0;
  while (std::getline(csv_lines, line)) {
    if (line.empty()) {
      continue;
    }
    if (line.rfind("# ", 0) == 0) {
      std::vector<std::string> row = parse_csv_row(std::string_view{line}.substr(2));
      require(!row.empty(), "CSV metadata row must contain a key");
      metadata_rows[row.front()].push_back(row);
      continue;
    }
    if (columns.empty()) {
      columns = parse_csv_row(line);
      continue;
    }
    const std::vector<std::string> row = parse_csv_row(line);
    require(row.size() == columns.size(), "CSV data row must match header width");
    ++data_row_count;
  }

  const JsonValue& csv_schema = require_member(schema, "x-csvExport");
  require(require_string(require_member(csv_schema, "schema_version"), "CSV schema version") ==
              "flying.telemetry-export.csv.v1",
          "schema must declare the emitted CSV schema version");
  for (const std::string& metadata_key :
       schema_string_array(schema, "x-csvExport", "required_metadata")) {
    require(metadata_rows.find(metadata_key) != metadata_rows.end(),
            ("CSV export missing schema-required metadata: " + metadata_key).c_str());
  }
  for (const std::string& column :
       schema_string_array(schema, "x-csvExport", "required_columns")) {
    require_contains(columns, column, "CSV header");
  }
  require(data_row_count == recording.frames.size(),
          "CSV data row count must match recorded frames");
  require(metadata_rows["schema_version"].front().at(1) == "flying.telemetry-export.csv.v1",
          "CSV export must declare the CSV export schema");
  require(metadata_rows["simulation_configuration_id"].front().at(1) ==
              recording.metadata.simulation_configuration_id,
          "CSV export must include the recorded simulation configuration id");
  require(metadata_rows["input_profile_id"].front().at(1) ==
              recording.metadata.input_profile_id,
          "CSV export must include the recorded input profile id");
  require(metadata_rows["data_package"].size() == recording.metadata.data_packages.size(),
          "CSV export must include every recorded data package version");

  remove_temp_file(csv_path);
  remove_temp_file(json_path);
}

} // namespace

int main() {
  replay_hash_is_stable_for_same_versions_and_inputs();
  exported_files_match_declared_schema_contract();
  return 0;
}
