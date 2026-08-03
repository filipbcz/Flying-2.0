#include "flying/data_pipeline/airport_database.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace flying::data_pipeline {
namespace {

constexpr std::string_view kAirportDatabaseSchemaVersion =
  "flying.airport-database.v1";
constexpr std::string_view kValidationReportSchemaVersion =
  "flying.validation-report.v1";

struct JsonValue {
  enum class Type {
    null_value,
    bool_value,
    number_value,
    string_value,
    array_value,
    object_value,
  };

  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;

  Type type = Type::null_value;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  Array array;
  Object object;
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  JsonValue parse() {
    skip_whitespace();
    JsonValue value = parse_value();
    skip_whitespace();
    if (index_ != text_.size()) {
      throw std::runtime_error("unexpected trailing JSON content");
    }
    return value;
  }

 private:
  JsonValue parse_value() {
    skip_whitespace();
    if (index_ >= text_.size()) {
      throw std::runtime_error("unexpected end of JSON");
    }

    const char token = text_[index_];
    if (token == '{') {
      return parse_object();
    }
    if (token == '[') {
      return parse_array();
    }
    if (token == '"') {
      JsonValue value;
      value.type = JsonValue::Type::string_value;
      value.string = parse_string();
      return value;
    }
    if (token == 't') {
      consume_literal("true");
      JsonValue value;
      value.type = JsonValue::Type::bool_value;
      value.boolean = true;
      return value;
    }
    if (token == 'f') {
      consume_literal("false");
      JsonValue value;
      value.type = JsonValue::Type::bool_value;
      value.boolean = false;
      return value;
    }
    if (token == 'n') {
      consume_literal("null");
      return {};
    }
    if (token == '-' || std::isdigit(static_cast<unsigned char>(token)) != 0) {
      return parse_number();
    }
    throw std::runtime_error("unexpected JSON token");
  }

  JsonValue parse_object() {
    expect('{');
    JsonValue value;
    value.type = JsonValue::Type::object_value;
    skip_whitespace();
    if (try_consume('}')) {
      return value;
    }

    while (true) {
      skip_whitespace();
      if (index_ >= text_.size() || text_[index_] != '"') {
        throw std::runtime_error("expected JSON object key");
      }
      std::string key = parse_string();
      skip_whitespace();
      expect(':');
      value.object.emplace(std::move(key), parse_value());
      skip_whitespace();
      if (try_consume('}')) {
        return value;
      }
      expect(',');
    }
  }

  JsonValue parse_array() {
    expect('[');
    JsonValue value;
    value.type = JsonValue::Type::array_value;
    skip_whitespace();
    if (try_consume(']')) {
      return value;
    }

    while (true) {
      value.array.push_back(parse_value());
      skip_whitespace();
      if (try_consume(']')) {
        return value;
      }
      expect(',');
    }
  }

  JsonValue parse_number() {
    const std::size_t begin = index_;
    if (try_consume('-') && index_ >= text_.size()) {
      throw std::runtime_error("incomplete JSON number");
    }
    if (try_consume('0')) {
      if (index_ < text_.size() &&
          std::isdigit(static_cast<unsigned char>(text_[index_])) != 0) {
        throw std::runtime_error("JSON number has a leading zero");
      }
    } else {
      require_digit();
      while (index_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[index_])) != 0) {
        ++index_;
      }
    }
    if (try_consume('.')) {
      require_digit();
      while (index_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[index_])) != 0) {
        ++index_;
      }
    }
    if (index_ < text_.size() && (text_[index_] == 'e' || text_[index_] == 'E')) {
      ++index_;
      if (index_ < text_.size() && (text_[index_] == '+' || text_[index_] == '-')) {
        ++index_;
      }
      require_digit();
      while (index_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[index_])) != 0) {
        ++index_;
      }
    }

    const std::string number_text{text_.substr(begin, index_ - begin)};
    JsonValue value;
    value.type = JsonValue::Type::number_value;
    value.number = std::stod(number_text);
    return value;
  }

  std::string parse_string() {
    expect('"');
    std::string output;
    while (index_ < text_.size()) {
      const char ch = text_[index_++];
      if (ch == '"') {
        return output;
      }
      if (static_cast<unsigned char>(ch) < 0x20U) {
        throw std::runtime_error("JSON strings cannot contain control characters");
      }
      if (ch != '\\') {
        output.push_back(ch);
        continue;
      }

      if (index_ >= text_.size()) {
        throw std::runtime_error("incomplete JSON escape");
      }
      const char escape = text_[index_++];
      switch (escape) {
        case '"':
        case '\\':
        case '/':
          output.push_back(escape);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        case 'u':
          append_unicode_escape(output);
          break;
        default:
          throw std::runtime_error("invalid JSON escape");
      }
    }
    throw std::runtime_error("unterminated JSON string");
  }

  void append_unicode_escape(std::string& output) {
    if (index_ + 4U > text_.size()) {
      throw std::runtime_error("incomplete JSON unicode escape");
    }
    unsigned int value = 0U;
    for (std::size_t i = 0; i < 4U; ++i) {
      const char ch = text_[index_++];
      const int digit = hex_digit(ch);
      if (digit < 0) {
        throw std::runtime_error("invalid JSON unicode escape");
      }
      value = (value << 4U) | static_cast<unsigned int>(digit);
    }
    if (value <= 0x7FU) {
      output.push_back(static_cast<char>(value));
    } else {
      output.push_back('?');
    }
  }

  static int hex_digit(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
      return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
      return 10 + (ch - 'A');
    }
    return -1;
  }

  void skip_whitespace() noexcept {
    while (index_ < text_.size()) {
      const unsigned char ch = static_cast<unsigned char>(text_[index_]);
      if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
        return;
      }
      ++index_;
    }
  }

  void consume_literal(std::string_view literal) {
    if (text_.substr(index_, literal.size()) != literal) {
      throw std::runtime_error("invalid JSON literal");
    }
    index_ += literal.size();
  }

  void expect(char expected) {
    skip_whitespace();
    if (index_ >= text_.size() || text_[index_] != expected) {
      throw std::runtime_error("unexpected JSON token");
    }
    ++index_;
  }

  bool try_consume(char expected) noexcept {
    if (index_ < text_.size() && text_[index_] == expected) {
      ++index_;
      return true;
    }
    return false;
  }

  void require_digit() {
    if (index_ >= text_.size() ||
        std::isdigit(static_cast<unsigned char>(text_[index_])) == 0) {
      throw std::runtime_error("expected digit in JSON number");
    }
    ++index_;
  }

  std::string_view text_;
  std::size_t index_ = 0U;
};

void add_issue(ValidationReport& report,
               std::string severity,
               std::string code,
               std::string message,
               std::string source_id = {}) {
  report.issues.push_back({
    std::move(severity),
    std::move(code),
    std::move(message),
    std::move(source_id),
  });
}

bool has_errors(const ValidationReport& report) {
  return std::any_of(report.issues.begin(), report.issues.end(), [](const ValidationIssue& issue) {
    return issue.severity == "error";
  });
}

void finalize_report(ValidationReport& report) {
  report.schema_version = std::string{kValidationReportSchemaVersion};
  report.passed = !has_errors(report);
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    throw std::runtime_error("failed to read " + path.string());
  }
  return buffer.str();
}

const JsonValue* find_member(const JsonValue::Object& object, std::string_view key) {
  const auto it = object.find(std::string{key});
  if (it == object.end()) {
    return nullptr;
  }
  return &it->second;
}

std::string indexed_subject(std::string_view subject, std::size_t index) {
  std::ostringstream output;
  output << subject << "[" << index << "]";
  return output.str();
}

bool is_allowed(std::string_view value, std::initializer_list<std::string_view> allowed) {
  return std::any_of(allowed.begin(), allowed.end(), [value](std::string_view candidate) {
    return value == candidate;
  });
}

std::optional<std::string> optional_string(const JsonValue::Object& object,
                                           std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->type != JsonValue::Type::string_value) {
    return std::nullopt;
  }
  return value->string;
}

std::string require_non_empty_string(const JsonValue::Object& object,
                                     std::string_view key,
                                     ValidationReport& report,
                                     std::string_view code_prefix,
                                     std::string_view subject,
                                     std::string_view source_id) {
  const JsonValue* value = find_member(object, key);
  const std::string code = std::string{code_prefix} + "." + std::string{key};
  if (value == nullptr) {
    add_issue(report,
              "error",
              code + ".missing",
              std::string{subject} + " is missing required field '" + std::string{key} + "'.",
              std::string{source_id});
    return {};
  }
  if (value->type != JsonValue::Type::string_value) {
    add_issue(report,
              "error",
              code + ".invalid_type",
              std::string{subject} + " field '" + std::string{key} + "' must be a string.",
              std::string{source_id});
    return {};
  }
  if (value->string.empty()) {
    add_issue(report,
              "error",
              code + ".empty",
              std::string{subject} + " field '" + std::string{key} + "' must not be empty.",
              std::string{source_id});
    return {};
  }
  return value->string;
}

bool require_enum_string(const JsonValue::Object& object,
                         std::string_view key,
                         std::initializer_list<std::string_view> allowed,
                         ValidationReport& report,
                         std::string_view code_prefix,
                         std::string_view subject,
                         std::string_view source_id) {
  const std::string value =
    require_non_empty_string(object, key, report, code_prefix, subject, source_id);
  if (value.empty()) {
    return false;
  }
  if (!is_allowed(value, allowed)) {
    add_issue(report,
              "error",
              std::string{code_prefix} + "." + std::string{key} + ".invalid",
              std::string{subject} + " field '" + std::string{key} +
                "' has an unsupported value.",
              std::string{source_id});
    return false;
  }
  return true;
}

bool require_number(const JsonValue::Object& object,
                    std::string_view key,
                    ValidationReport& report,
                    std::string_view code_prefix,
                    std::string_view subject,
                    std::string_view source_id) {
  const JsonValue* value = find_member(object, key);
  const std::string code = std::string{code_prefix} + "." + std::string{key};
  if (value == nullptr) {
    add_issue(report,
              "error",
              code + ".missing",
              std::string{subject} + " is missing required field '" + std::string{key} + "'.",
              std::string{source_id});
    return false;
  }
  if (value->type != JsonValue::Type::number_value) {
    add_issue(report,
              "error",
              code + ".invalid_type",
              std::string{subject} + " field '" + std::string{key} + "' must be a number.",
              std::string{source_id});
    return false;
  }
  return true;
}

bool require_bool(const JsonValue::Object& object,
                  std::string_view key,
                  ValidationReport& report,
                  std::string_view code_prefix,
                  std::string_view subject,
                  std::string_view source_id) {
  const JsonValue* value = find_member(object, key);
  const std::string code = std::string{code_prefix} + "." + std::string{key};
  if (value == nullptr) {
    add_issue(report,
              "error",
              code + ".missing",
              std::string{subject} + " is missing required field '" + std::string{key} + "'.",
              std::string{source_id});
    return false;
  }
  if (value->type != JsonValue::Type::bool_value) {
    add_issue(report,
              "error",
              code + ".invalid_type",
              std::string{subject} + " field '" + std::string{key} + "' must be a boolean.",
              std::string{source_id});
    return false;
  }
  return true;
}

const JsonValue::Object* require_object(const JsonValue::Object& object,
                                        std::string_view key,
                                        ValidationReport& report,
                                        std::string_view code_prefix,
                                        std::string_view subject,
                                        std::string_view source_id) {
  const JsonValue* value = find_member(object, key);
  const std::string code = std::string{code_prefix} + "." + std::string{key};
  if (value == nullptr) {
    add_issue(report,
              "error",
              code + ".missing",
              std::string{subject} + " is missing required object '" + std::string{key} + "'.",
              std::string{source_id});
    return nullptr;
  }
  if (value->type != JsonValue::Type::object_value) {
    add_issue(report,
              "error",
              code + ".invalid_type",
              std::string{subject} + " field '" + std::string{key} + "' must be an object.",
              std::string{source_id});
    return nullptr;
  }
  return &value->object;
}

const JsonValue::Array* require_array(const JsonValue::Object& object,
                                      std::string_view key,
                                      ValidationReport& report,
                                      std::string_view code_prefix,
                                      std::string_view subject,
                                      std::string_view source_id,
                                      std::size_t min_items = 0U) {
  const JsonValue* value = find_member(object, key);
  const std::string code = std::string{code_prefix} + "." + std::string{key};
  if (value == nullptr) {
    add_issue(report,
              "error",
              code + ".missing",
              std::string{subject} + " is missing required array '" + std::string{key} + "'.",
              std::string{source_id});
    return nullptr;
  }
  if (value->type != JsonValue::Type::array_value) {
    add_issue(report,
              "error",
              code + ".invalid_type",
              std::string{subject} + " field '" + std::string{key} + "' must be an array.",
              std::string{source_id});
    return nullptr;
  }
  if (value->array.size() < min_items) {
    add_issue(report,
              "error",
              code + ".too_few_items",
              std::string{subject} + " field '" + std::string{key} + "' must contain at least " +
                std::to_string(min_items) + " item(s).",
              std::string{source_id});
  }
  return &value->array;
}

void validate_date_field(const JsonValue::Object& object,
                         std::string_view key,
                         ValidationReport& report,
                         std::string_view code_prefix,
                         std::string_view subject,
                         std::string_view source_id) {
  const std::string value =
    require_non_empty_string(object, key, report, code_prefix, subject, source_id);
  if (value.size() != 10U || value[4] != '-' || value[7] != '-') {
    add_issue(report,
              "error",
              std::string{code_prefix} + "." + std::string{key} + ".invalid",
              std::string{subject} + " field '" + std::string{key} +
                "' must be formatted as YYYY-MM-DD.",
              std::string{source_id});
  }
}

void validate_wgs84_position(const JsonValue::Object& object,
                             ValidationReport& report,
                             std::string_view code_prefix,
                             std::string_view subject,
                             std::string_view source_id) {
  const bool has_lat =
    require_number(object, "latDeg", report, code_prefix, subject, source_id);
  const bool has_lon =
    require_number(object, "lonDeg", report, code_prefix, subject, source_id);
  (void)require_number(object, "elevationM", report, code_prefix, subject, source_id);

  const JsonValue* lat = find_member(object, "latDeg");
  if (has_lat && (lat->number < -90.0 || lat->number > 90.0)) {
    add_issue(report,
              "error",
              std::string{code_prefix} + ".latDeg.out_of_range",
              std::string{subject} + " latitude must be within WGS84 bounds.",
              std::string{source_id});
  }
  const JsonValue* lon = find_member(object, "lonDeg");
  if (has_lon && (lon->number < -180.0 || lon->number > 180.0)) {
    add_issue(report,
              "error",
              std::string{code_prefix} + ".lonDeg.out_of_range",
              std::string{subject} + " longitude must be within WGS84 bounds.",
              std::string{source_id});
  }
}

void validate_airac(const JsonValue::Object& object,
                    ValidationReport& report,
                    std::string_view code_prefix,
                    std::string_view subject,
                    std::string_view source_id) {
  (void)require_non_empty_string(object, "cycleId", report, code_prefix, subject, source_id);
  validate_date_field(object, "effectiveDate", report, code_prefix, subject, source_id);
  (void)require_non_empty_string(object, "source", report, code_prefix, subject, source_id);
}

bool validate_provenance_array(const JsonValue::Object& object,
                               std::string_view key,
                               ValidationReport& report,
                               std::string_view code_prefix,
                               std::string_view subject,
                               std::string_view source_id) {
  const JsonValue::Array* provenance =
    require_array(object, key, report, code_prefix, subject, source_id, 1U);
  if (provenance == nullptr) {
    return false;
  }
  for (std::size_t i = 0; i < provenance->size(); ++i) {
    const JsonValue& entry = (*provenance)[i];
    const std::string entry_subject = indexed_subject("provenance", i);
    if (entry.type != JsonValue::Type::object_value) {
      add_issue(report,
                "error",
                std::string{code_prefix} + "." + std::string{key} + ".invalid_type",
                "Provenance entries must be objects.",
                std::string{source_id});
      continue;
    }
    const JsonValue::Object& provenance_object = entry.object;
    (void)require_non_empty_string(provenance_object,
                                   "sourceId",
                                   report,
                                   std::string{code_prefix} + "." + std::string{key},
                                   entry_subject,
                                   source_id);
    (void)require_enum_string(provenance_object,
                              "sourceType",
                              {"operator_confirmation",
                               "geodetic_survey",
                               "cuzk_derived_measurement",
                               "project_fixture",
                               "public_register_checklist",
                               "restricted_aip_pending_permission"},
                              report,
                              std::string{code_prefix} + "." + std::string{key},
                              entry_subject,
                              source_id);
    (void)require_non_empty_string(provenance_object,
                                   "publisher",
                                   report,
                                   std::string{code_prefix} + "." + std::string{key},
                                   entry_subject,
                                   source_id);
    (void)require_enum_string(provenance_object,
                              "permissionStatus",
                              {"permitted",
                               "pending_permission",
                               "blocked_missing_permission",
                               "blocked_missing_source_data"},
                              report,
                              std::string{code_prefix} + "." + std::string{key},
                              entry_subject,
                              source_id);
    (void)require_non_empty_string(provenance_object,
                                   "retrievedAtUtc",
                                   report,
                                   std::string{code_prefix} + "." + std::string{key},
                                   entry_subject,
                                   source_id);
  }
  return !provenance->empty();
}

std::optional<std::string> validate_validation_state(const JsonValue::Object& object,
                                                     std::string_view key,
                                                     ValidationReport& report,
                                                     std::string_view code_prefix,
                                                     std::string_view subject,
                                                     std::string_view source_id) {
  const JsonValue::Object* validation =
    require_object(object, key, report, code_prefix, subject, source_id);
  if (validation == nullptr) {
    return std::nullopt;
  }

  const std::optional<std::string> status = optional_string(*validation, "status");
  (void)require_enum_string(*validation,
                            "status",
                            {"draft",
                             "derived",
                             "manually_verified",
                             "production_validated",
                             "blocked_missing_permission",
                             "blocked_missing_source_data"},
                            report,
                            std::string{code_prefix} + "." + std::string{key},
                            subject,
                            source_id);

  const JsonValue::Object* confidence = require_object(*validation,
                                                       "confidence",
                                                       report,
                                                       std::string{code_prefix} + "." +
                                                         std::string{key},
                                                       subject,
                                                       source_id);
  if (confidence != nullptr) {
    for (const std::string_view field :
         {"overall", "geometry", "surface", "declaredDistances"}) {
      if (!require_number(*confidence,
                          field,
                          report,
                          std::string{code_prefix} + "." + std::string{key} + ".confidence",
                          subject,
                          source_id)) {
        continue;
      }
      const JsonValue* value = find_member(*confidence, field);
      if (value->number < 0.0 || value->number > 1.0) {
        add_issue(report,
                  "error",
                  std::string{code_prefix} + "." + std::string{key} + ".confidence." +
                    std::string{field} + ".out_of_range",
                  std::string{subject} + " confidence values must be between 0 and 1.",
                  std::string{source_id});
      }
    }
  }

  const JsonValue::Object* manual = require_object(*validation,
                                                   "manualVerification",
                                                   report,
                                                   std::string{code_prefix} + "." +
                                                     std::string{key},
                                                   subject,
                                                   source_id);
  if (manual != nullptr) {
    (void)require_enum_string(*manual,
                              "status",
                              {"unverified",
                               "reviewer_approved",
                               "rejected",
                               "blocked",
                               "not_required"},
                              report,
                              std::string{code_prefix} + "." + std::string{key} +
                                ".manualVerification",
                              subject,
                              source_id);
  }
  return status;
}

void validate_lighting(const JsonValue::Object& object,
                       ValidationReport& report,
                       std::string_view code_prefix,
                       std::string_view subject,
                       std::string_view source_id) {
  for (const std::string_view field : {"edgeLights",
                                       "thresholdLights",
                                       "approachLights",
                                       "papi",
                                       "runwayEndIdentifierLights"}) {
    (void)require_bool(object, field, report, code_prefix, subject, source_id);
  }
  const JsonValue* intensity = find_member(object, "intensity");
  if (intensity != nullptr) {
    (void)require_enum_string(object,
                              "intensity",
                              {"none", "low", "medium", "high", "unknown"},
                              report,
                              code_prefix,
                              subject,
                              source_id);
  }
}

void validate_markings(const JsonValue::Object& object,
                       ValidationReport& report,
                       std::string_view code_prefix,
                       std::string_view subject,
                       std::string_view source_id) {
  for (const std::string_view field :
       {"designator", "centerline", "threshold", "aimingPoint", "touchdownZone", "edge"}) {
    (void)require_bool(object, field, report, code_prefix, subject, source_id);
  }
}

void validate_slope(const JsonValue::Object& object,
                    ValidationReport& report,
                    std::string_view code_prefix,
                    std::string_view subject,
                    std::string_view source_id) {
  (void)require_number(
    object, "longitudinalPercent", report, code_prefix, subject, source_id);
  (void)require_number(object, "transversePercent", report, code_prefix, subject, source_id);
}

bool has_physical_threshold_coordinates(const JsonValue::Object& runway_end) {
  const JsonValue* threshold = find_member(runway_end, "threshold");
  if (threshold == nullptr || threshold->type != JsonValue::Type::object_value) {
    return false;
  }
  const JsonValue* physical = find_member(threshold->object, "physicalThreshold");
  if (physical == nullptr || physical->type != JsonValue::Type::object_value) {
    return false;
  }
  const JsonValue* position = find_member(physical->object, "positionWgs84");
  if (position == nullptr || position->type != JsonValue::Type::object_value) {
    return false;
  }
  for (const std::string_view field : {"latDeg", "lonDeg", "elevationM"}) {
    const JsonValue* coordinate = find_member(position->object, field);
    if (coordinate == nullptr || coordinate->type != JsonValue::Type::number_value) {
      return false;
    }
  }
  return true;
}

void validate_threshold_set(const JsonValue::Object& object,
                            ValidationReport& report,
                            std::string_view code_prefix,
                            std::string_view subject,
                            std::string_view source_id) {
  const JsonValue::Object* physical =
    require_object(object, "physicalThreshold", report, code_prefix, subject, source_id);
  if (physical != nullptr) {
    const JsonValue::Object* position = require_object(
      *physical, "positionWgs84", report, std::string{code_prefix} + ".physicalThreshold",
      subject, source_id);
    if (position != nullptr) {
      validate_wgs84_position(*position,
                              report,
                              std::string{code_prefix} + ".physicalThreshold.positionWgs84",
                              subject,
                              source_id);
    }
    (void)require_number(*physical,
                         "sourceAccuracyM",
                         report,
                         std::string{code_prefix} + ".physicalThreshold",
                         subject,
                         source_id);
    (void)require_non_empty_string(*physical,
                                   "provenanceRef",
                                   report,
                                   std::string{code_prefix} + ".physicalThreshold",
                                   subject,
                                   source_id);
  }
  const JsonValue::Object* displaced =
    require_object(object, "displacedThreshold", report, code_prefix, subject, source_id);
  if (displaced != nullptr) {
    (void)require_bool(
      *displaced, "present", report, std::string{code_prefix} + ".displacedThreshold", subject,
      source_id);
  }
}

void validate_declared_distances(const JsonValue::Object& object,
                                 ValidationReport& report,
                                 std::string_view code_prefix,
                                 std::string_view subject,
                                 std::string_view source_id) {
  for (const std::string_view field : {"tora", "toda", "asda", "lda"}) {
    if (!require_number(object, field, report, code_prefix, subject, source_id)) {
      continue;
    }
    const JsonValue* value = find_member(object, field);
    if (value->number < 0.0) {
      add_issue(report,
                "error",
                std::string{code_prefix} + "." + std::string{field} + ".negative",
                std::string{subject} + " declared distances cannot be negative.",
                std::string{source_id});
    }
  }
  (void)require_non_empty_string(
    object, "provenanceRef", report, code_prefix, subject, source_id);
}

void validate_runway_end(const JsonValue& value,
                         std::size_t index,
                         bool production_validated_runway,
                         ValidationReport& report,
                         std::string_view runway_id) {
  const std::string fallback_subject = indexed_subject("runwayEnd", index);
  if (value.type != JsonValue::Type::object_value) {
    add_issue(report,
              "error",
              "airport.runway_end.invalid_type",
              "Runway ends must be objects.",
              std::string{runway_id});
    return;
  }

  const JsonValue::Object& runway_end = value.object;
  const std::string runway_end_id = require_non_empty_string(runway_end,
                                                            "id",
                                                            report,
                                                            "airport.runway_end",
                                                            fallback_subject,
                                                            runway_id);
  const std::string issue_id = runway_end_id.empty() ? std::string{runway_id} : runway_end_id;
  (void)require_non_empty_string(
    runway_end, "designator", report, "airport.runway_end", fallback_subject, issue_id);
  (void)require_number(
    runway_end, "trueBearingDeg", report, "airport.runway_end", fallback_subject, issue_id);

  const JsonValue::Object* threshold = require_object(
    runway_end, "threshold", report, "airport.runway_end", fallback_subject, issue_id);
  if (threshold != nullptr) {
    validate_threshold_set(
      *threshold, report, "airport.runway_end.threshold", fallback_subject, issue_id);
  }
  const JsonValue::Object* distances = require_object(
    runway_end, "declaredDistancesM", report, "airport.runway_end", fallback_subject, issue_id);
  if (distances != nullptr) {
    validate_declared_distances(*distances,
                                report,
                                "airport.runway_end.declaredDistancesM",
                                fallback_subject,
                                issue_id);
  }
  const JsonValue::Object* lighting = require_object(
    runway_end, "lighting", report, "airport.runway_end", fallback_subject, issue_id);
  if (lighting != nullptr) {
    validate_lighting(
      *lighting, report, "airport.runway_end.lighting", fallback_subject, issue_id);
  }
  const JsonValue::Object* markings = require_object(
    runway_end, "markings", report, "airport.runway_end", fallback_subject, issue_id);
  if (markings != nullptr) {
    validate_markings(
      *markings, report, "airport.runway_end.markings", fallback_subject, issue_id);
  }
  const JsonValue::Object* slope = require_object(
    runway_end, "slope", report, "airport.runway_end", fallback_subject, issue_id);
  if (slope != nullptr) {
    validate_slope(*slope, report, "airport.runway_end.slope", fallback_subject, issue_id);
  }

  const bool has_provenance = validate_provenance_array(
    runway_end, "provenance", report, "airport.runway_end", fallback_subject, issue_id);
  (void)validate_validation_state(
    runway_end, "validation", report, "airport.runway_end", fallback_subject, issue_id);

  if (production_validated_runway && !has_physical_threshold_coordinates(runway_end)) {
    add_issue(report,
              "error",
              "airport.runway_end.production_physical_threshold_coordinates.missing",
              "Production-validated runway ends must include physical threshold latitude, "
              "longitude and elevation.",
              issue_id);
  }
  if (production_validated_runway && !has_provenance) {
    add_issue(report,
              "error",
              "airport.runway_end.production_provenance.missing",
              "Production-validated runway ends must include source provenance.",
              issue_id);
  }
}

void validate_runway_surface(const JsonValue::Object& surface,
                             ValidationReport& report,
                             std::string_view runway_id) {
  (void)require_enum_string(surface,
                            "surfaceType",
                            {"paved", "grass", "unsealed", "water", "unknown"},
                            report,
                            "airport.runway.surface",
                            "Runway surface",
                            runway_id);
  (void)require_enum_string(surface,
                            "material",
                            {"asphalt",
                             "concrete",
                             "grass",
                             "turf",
                             "gravel",
                             "soil",
                             "composite",
                             "unknown"},
                            report,
                            "airport.runway.surface",
                            "Runway surface",
                            runway_id);
  (void)require_enum_string(surface,
                            "condition",
                            {"serviceable", "limited", "closed", "unknown"},
                            report,
                            "airport.runway.surface",
                            "Runway surface",
                            runway_id);
}

void validate_runway(const JsonValue& value,
                     std::size_t index,
                     ValidationReport& report,
                     std::string_view aerodrome_id) {
  const std::string fallback_subject = indexed_subject("runway", index);
  if (value.type != JsonValue::Type::object_value) {
    add_issue(report,
              "error",
              "airport.runway.invalid_type",
              "Runways must be objects.",
              std::string{aerodrome_id});
    return;
  }

  const JsonValue::Object& runway = value.object;
  const std::string runway_id = require_non_empty_string(
    runway, "id", report, "airport.runway", fallback_subject, aerodrome_id);
  const std::string issue_id = runway_id.empty() ? std::string{aerodrome_id} : runway_id;
  (void)require_non_empty_string(
    runway, "designator", report, "airport.runway", fallback_subject, issue_id);

  const JsonValue::Object* surface =
    require_object(runway, "surface", report, "airport.runway", fallback_subject, issue_id);
  if (surface != nullptr) {
    validate_runway_surface(*surface, report, issue_id);
  }

  const JsonValue::Object* dimensions =
    require_object(runway, "dimensionsM", report, "airport.runway", fallback_subject, issue_id);
  if (dimensions != nullptr) {
    for (const std::string_view field : {"length", "width"}) {
      if (!require_number(
            *dimensions, field, report, "airport.runway.dimensionsM", "Runway dimensions",
            issue_id)) {
        continue;
      }
      const JsonValue* value_number = find_member(*dimensions, field);
      if (value_number->number <= 0.0) {
        add_issue(report,
                  "error",
                  "airport.runway.dimensionsM.non_positive",
                  "Runway dimensions must be positive.",
                  issue_id);
      }
    }
  }

  const JsonValue::Object* lighting =
    require_object(runway, "lighting", report, "airport.runway", fallback_subject, issue_id);
  if (lighting != nullptr) {
    validate_lighting(*lighting, report, "airport.runway.lighting", fallback_subject, issue_id);
  }
  const JsonValue::Object* markings =
    require_object(runway, "markings", report, "airport.runway", fallback_subject, issue_id);
  if (markings != nullptr) {
    validate_markings(*markings, report, "airport.runway.markings", fallback_subject, issue_id);
  }
  const JsonValue::Object* slope =
    require_object(runway, "slope", report, "airport.runway", fallback_subject, issue_id);
  if (slope != nullptr) {
    validate_slope(*slope, report, "airport.runway.slope", fallback_subject, issue_id);
  }

  validate_date_field(
    runway, "airacEffectiveDate", report, "airport.runway", fallback_subject, issue_id);
  const bool has_provenance =
    validate_provenance_array(runway, "provenance", report, "airport.runway", fallback_subject,
                              issue_id);
  const std::optional<std::string> validation_status = validate_validation_state(
    runway, "validation", report, "airport.runway", fallback_subject, issue_id);
  const bool production_validated =
    validation_status.has_value() && *validation_status == "production_validated";

  const JsonValue::Array* ends =
    require_array(runway, "ends", report, "airport.runway", fallback_subject, issue_id, 2U);
  if (ends != nullptr) {
    if (ends->size() != 2U) {
      add_issue(report,
                "error",
                "airport.runway.ends.count",
                "Runways must declare exactly two runway ends.",
                issue_id);
    }
    for (std::size_t i = 0; i < ends->size(); ++i) {
      validate_runway_end((*ends)[i], i, production_validated, report, issue_id);
    }
  }

  if (production_validated && !has_provenance) {
    add_issue(report,
              "error",
              "airport.runway.production_provenance.missing",
              "Production-validated runways must include source provenance.",
              issue_id);
  }
}

void validate_master_list_record(const JsonValue& value,
                                 std::size_t index,
                                 ValidationReport& report) {
  const std::string subject = indexed_subject("masterList", index);
  if (value.type != JsonValue::Type::object_value) {
    add_issue(report,
              "error",
              "airport.master_list.invalid_type",
              "Airport master list records must be objects.",
              subject);
    return;
  }

  const JsonValue::Object& record = value.object;
  const std::string aerodrome_id = require_non_empty_string(
    record, "aerodromeId", report, "airport.master_list", subject, subject);
  const std::string issue_id = aerodrome_id.empty() ? subject : aerodrome_id;
  (void)require_non_empty_string(
    record, "name", report, "airport.master_list", subject, issue_id);
  (void)require_enum_string(record,
                            "classification",
                            {"active_airport", "slz_field", "closed_field"},
                            report,
                            "airport.master_list",
                            subject,
                            issue_id);
  (void)require_enum_string(record,
                            "operationalStatus",
                            {"active", "inactive", "closed", "unknown"},
                            report,
                            "airport.master_list",
                            subject,
                            issue_id);
  (void)require_enum_string(record,
                            "recordStatus",
                            {"validated",
                             "derived",
                             "blocked_missing_permission",
                             "blocked_missing_source_data",
                             "draft"},
                            report,
                            "airport.master_list",
                            subject,
                            issue_id);
  (void)require_enum_string(record,
                            "sourceDataStatus",
                            {"permitted",
                             "pending_permission",
                             "blocked_missing_permission",
                             "blocked_missing_source_data"},
                            report,
                            "airport.master_list",
                            subject,
                            issue_id);
  validate_date_field(
    record, "airacEffectiveDate", report, "airport.master_list", subject, issue_id);
  (void)validate_provenance_array(
    record, "provenance", report, "airport.master_list", subject, issue_id);
  (void)validate_validation_state(
    record, "validation", report, "airport.master_list", subject, issue_id);
}

void validate_aerodrome(const JsonValue& value, std::size_t index, ValidationReport& report) {
  const std::string subject = indexed_subject("aerodrome", index);
  if (value.type != JsonValue::Type::object_value) {
    add_issue(report,
              "error",
              "airport.aerodrome.invalid_type",
              "Aerodrome records must be objects.",
              subject);
    return;
  }

  const JsonValue::Object& aerodrome = value.object;
  const std::string aerodrome_id =
    require_non_empty_string(aerodrome, "id", report, "airport.aerodrome", subject, subject);
  const std::string issue_id = aerodrome_id.empty() ? subject : aerodrome_id;
  (void)require_non_empty_string(
    aerodrome, "name", report, "airport.aerodrome", subject, issue_id);
  (void)require_enum_string(aerodrome,
                            "classification",
                            {"active_airport", "slz_field", "closed_field"},
                            report,
                            "airport.aerodrome",
                            subject,
                            issue_id);
  (void)require_enum_string(aerodrome,
                            "operationalStatus",
                            {"active", "inactive", "closed", "unknown"},
                            report,
                            "airport.aerodrome",
                            subject,
                            issue_id);
  const JsonValue::Object* reference_point = require_object(aerodrome,
                                                           "referencePointWgs84",
                                                           report,
                                                           "airport.aerodrome",
                                                           subject,
                                                           issue_id);
  if (reference_point != nullptr) {
    validate_wgs84_position(
      *reference_point, report, "airport.aerodrome.referencePointWgs84", subject, issue_id);
  }
  validate_date_field(
    aerodrome, "airacEffectiveDate", report, "airport.aerodrome", subject, issue_id);
  (void)validate_provenance_array(
    aerodrome, "provenance", report, "airport.aerodrome", subject, issue_id);
  (void)validate_validation_state(
    aerodrome, "validation", report, "airport.aerodrome", subject, issue_id);

  const JsonValue::Array* runways =
    require_array(aerodrome, "runways", report, "airport.aerodrome", subject, issue_id);
  if (runways != nullptr) {
    for (std::size_t i = 0; i < runways->size(); ++i) {
      validate_runway((*runways)[i], i, report, issue_id);
    }
  }
}

void validate_root_object(const JsonValue::Object& root, ValidationReport& report) {
  const std::string schema_version = require_non_empty_string(
    root, "schemaVersion", report, "airport", "Airport database", {});
  if (!schema_version.empty() && schema_version != kAirportDatabaseSchemaVersion) {
    add_issue(report,
              "error",
              "airport.schemaVersion.unsupported",
              "Airport database schemaVersion must be flying.airport-database.v1.");
  }

  (void)require_non_empty_string(
    root, "databaseVersion", report, "airport", "Airport database", {});
  const JsonValue::Object* airac =
    require_object(root, "airac", report, "airport", "Airport database", {});
  if (airac != nullptr) {
    validate_airac(*airac, report, "airport.airac", "Airport AIRAC metadata", {});
  }
  const JsonValue::Object* metadata =
    require_object(root, "metadata", report, "airport", "Airport database", {});
  if (metadata != nullptr) {
    (void)require_enum_string(*metadata,
                              "scope",
                              {"pilot_airports_only", "national_airport_master_list"},
                              report,
                              "airport.metadata",
                              "Airport metadata",
                              {});
    (void)require_enum_string(*metadata,
                              "restrictedSourcePolicy",
                              {"no_aip_vfr_content_without_permission",
                               "restricted_sources_permitted_by_archived_license"},
                              report,
                              "airport.metadata",
                              "Airport metadata",
                              {});
    (void)require_non_empty_string(
      *metadata, "generatedAtUtc", report, "airport.metadata", "Airport metadata", {});
  }

  const JsonValue::Array* master_list =
    require_array(root, "masterList", report, "airport", "Airport database", {}, 1U);
  if (master_list != nullptr) {
    for (std::size_t i = 0; i < master_list->size(); ++i) {
      validate_master_list_record((*master_list)[i], i, report);
    }
  }

  const JsonValue::Array* aerodromes =
    require_array(root, "aerodromes", report, "airport", "Airport database", {});
  if (aerodromes != nullptr) {
    for (std::size_t i = 0; i < aerodromes->size(); ++i) {
      validate_aerodrome((*aerodromes)[i], i, report);
    }
  }
}

} // namespace

ValidationReport validate_airport_database_text(std::string_view airport_database_json) {
  ValidationReport report;

  try {
    JsonValue parsed = JsonParser{airport_database_json}.parse();
    if (parsed.type != JsonValue::Type::object_value) {
      add_issue(report,
                "error",
                "airport.root.invalid_type",
                "Airport database root must be a JSON object.");
    } else {
      validate_root_object(parsed.object, report);
    }
  } catch (const std::exception& error) {
    add_issue(report,
              "error",
              "airport.json.parse_failed",
              std::string{"Airport database JSON parse failed: "} + error.what());
  }

  finalize_report(report);
  return report;
}

ValidationReport validate_airport_database_file(const std::filesystem::path& path) {
  ValidationReport report;
  report.source_manifest_path = path.string();
  try {
    report = validate_airport_database_text(read_text_file(path));
    report.source_manifest_path = path.string();
  } catch (const std::exception& error) {
    add_issue(report,
              "error",
              "airport.file.read_failed",
              std::string{"Airport database file could not be read: "} + error.what());
    finalize_report(report);
  }
  return report;
}

} // namespace flying::data_pipeline
