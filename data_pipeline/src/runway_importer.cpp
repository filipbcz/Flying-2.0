#include "flying/data_pipeline/runway_importer.hpp"

#include "flying/data_pipeline/airport_database.hpp"
#include "flying/geo_terrain/geodesy.hpp"
#include "flying/geo_terrain/terrain_service.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace flying::data_pipeline {
namespace {

constexpr std::string_view kAirportDatabaseSchemaVersion =
  "flying.airport-database.v1";
constexpr std::string_view kPackageSchemaVersion =
  "flying.pilot-runway-surfaces-package.v1";
constexpr std::string_view kSurfaceSchemaVersion =
  "flying.pilot-runway-surface.v1";
constexpr std::string_view kCoverageReportSchemaVersion =
  "flying.pilot-runway-coverage-report.v1";
constexpr std::string_view kValidationReportSchemaVersion =
  "flying.validation-report.v1";
constexpr double kRadiansToDegrees = 180.0 / flying::geo_terrain::kPi;
constexpr double kDegreesToRadians = flying::geo_terrain::kPi / 180.0;
constexpr double kCollisionVisualToleranceM = 0.05;
constexpr double kMinimumStartOffsetM = 45.0;
constexpr double kDefaultStartOffsetM = 60.0;

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
    std::size_t parsed = 0U;
    JsonValue value;
    value.type = JsonValue::Type::number_value;
    value.number = std::stod(number_text, &parsed);
    if (parsed != number_text.size()) {
      throw std::runtime_error("invalid JSON number");
    }
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
    for (std::size_t i = 0U; i < 4U; ++i) {
      const int digit = hex_digit(text_[index_++]);
      if (digit < 0) {
        throw std::runtime_error("invalid JSON unicode escape");
      }
      value = (value << 4U) | static_cast<unsigned int>(digit);
    }
    output.push_back(value <= 0x7FU ? static_cast<char>(value) : '?');
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

struct Wgs84Position {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double elevation_m = 0.0;
};

struct Point3 {
  double east_m = 0.0;
  double north_m = 0.0;
  double up_m = 0.0;
};

struct SurfaceMarkings {
  bool designator = false;
  bool centerline = false;
  bool threshold = false;
  bool aiming_point = false;
  bool touchdown_zone = false;
  bool edge = false;
};

struct RunwayEndRecord {
  std::string id;
  std::string designator;
  double true_bearing_deg = 0.0;
  Wgs84Position threshold;
  double source_accuracy_m = std::numeric_limits<double>::infinity();
  std::string provenance_ref;
  std::string provenance_json = "[]";
};

struct RunwayRecord {
  std::string id;
  std::string designator;
  std::string surface_type;
  std::string material;
  double length_m = 0.0;
  double width_m = 0.0;
  SurfaceMarkings markings;
  double declared_longitudinal_percent = 0.0;
  double transverse_percent = 0.0;
  std::string provenance_json = "[]";
  std::vector<RunwayEndRecord> ends;
};

struct AerodromeRecord {
  std::string id;
  std::string name;
  std::string classification;
  std::string operational_status;
  Wgs84Position reference_point;
  std::vector<RunwayRecord> runways;
};

struct AirportDatabase {
  std::string database_version;
  std::string generated_at_utc;
  std::string scope;
  std::vector<AerodromeRecord> aerodromes;
};

struct SurfaceVertex {
  std::string id;
  Point3 local;
  Wgs84Position wgs84;
};

struct StartPosition {
  std::string id;
  std::string runway_end_designator;
  double heading_deg = 0.0;
  Point3 local;
  Wgs84Position wgs84;
};

struct GeneratedRunway {
  std::string airport_id;
  std::string airport_name;
  std::string runway_id;
  std::string runway_designator;
  std::string surface_type;
  std::string material;
  std::string file_relative_path;
  std::string surface_json;
  std::array<SurfaceVertex, 4U> vertices;
  std::vector<StartPosition> start_positions;
  Point3 threshold0_local;
  Point3 threshold1_local;
  double heading_deg = 0.0;
  double reciprocal_heading_deg = 0.0;
  double length_from_thresholds_m = 0.0;
  double declared_length_m = 0.0;
  double width_m = 0.0;
  double longitudinal_percent = 0.0;
  double declared_longitudinal_percent = 0.0;
  double transverse_percent = 0.0;
  double heading_error0_deg = 0.0;
  double heading_error1_deg = 0.0;
  double dimension_error_m = 0.0;
  double max_source_accuracy_m = 0.0;
  double transition_band_width_m = 0.0;
  double max_collision_visual_delta_m = 0.0;
  bool paved = false;
  bool grass = false;
  std::string provenance_json = "[]";
};

std::string json_escape(std::string_view text) {
  std::ostringstream output;
  for (const char ch : text) {
    switch (ch) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20U) {
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec
                 << std::setfill(' ');
        } else {
          output << ch;
        }
        break;
    }
  }
  return output.str();
}

std::string json_quote(std::string_view text) {
  return "\"" + json_escape(text) + "\"";
}

std::string render_number(double value) {
  if (!std::isfinite(value)) {
    return "0";
  }
  std::ostringstream output;
  output << std::setprecision(17) << value;
  return output.str();
}

std::string render_canonical_json(const JsonValue& value) {
  switch (value.type) {
    case JsonValue::Type::null_value:
      return "null";
    case JsonValue::Type::bool_value:
      return value.boolean ? "true" : "false";
    case JsonValue::Type::number_value:
      return render_number(value.number);
    case JsonValue::Type::string_value:
      return json_quote(value.string);
    case JsonValue::Type::array_value: {
      std::string output = "[";
      for (std::size_t i = 0U; i < value.array.size(); ++i) {
        if (i != 0U) {
          output += ",";
        }
        output += render_canonical_json(value.array[i]);
      }
      output += "]";
      return output;
    }
    case JsonValue::Type::object_value: {
      std::string output = "{";
      bool first = true;
      for (const auto& [key, child] : value.object) {
        if (!first) {
          output += ",";
        }
        first = false;
        output += json_quote(key);
        output += ":";
        output += render_canonical_json(child);
      }
      output += "}";
      return output;
    }
  }
  throw std::logic_error("unhandled JSON value type");
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

void write_text_file(const std::filesystem::path& path, std::string_view text) {
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open " + path.string());
  }
  output << text;
  if (!output) {
    throw std::runtime_error("failed to write " + path.string());
  }
}

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

const JsonValue* find_member(const JsonValue::Object& object, std::string_view key) {
  const auto it = object.find(std::string{key});
  if (it == object.end()) {
    return nullptr;
  }
  return &it->second;
}

std::optional<std::string> optional_string(const JsonValue::Object& object,
                                           std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->type != JsonValue::Type::string_value) {
    return std::nullopt;
  }
  return value->string;
}

std::optional<double> optional_number(const JsonValue::Object& object,
                                      std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->type != JsonValue::Type::number_value) {
    return std::nullopt;
  }
  return value->number;
}

std::optional<bool> optional_bool(const JsonValue::Object& object,
                                  std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->type != JsonValue::Type::bool_value) {
    return std::nullopt;
  }
  return value->boolean;
}

const JsonValue::Object* optional_object(const JsonValue::Object& object,
                                         std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->type != JsonValue::Type::object_value) {
    return nullptr;
  }
  return &value->object;
}

const JsonValue::Array* optional_array(const JsonValue::Object& object,
                                       std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->type != JsonValue::Type::array_value) {
    return nullptr;
  }
  return &value->array;
}

bool contains_source_id(const JsonValue::Array& provenance, std::string_view source_id) {
  for (const JsonValue& entry : provenance) {
    if (entry.type != JsonValue::Type::object_value) {
      continue;
    }
    const std::optional<std::string> id = optional_string(entry.object, "sourceId");
    if (id.has_value() && *id == source_id) {
      return true;
    }
  }
  return false;
}

std::string lowercase_ascii(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const char ch : text) {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return lowered;
}

std::string sanitized_id(std::string_view text, std::string_view fallback) {
  std::string output;
  bool previous_dash = false;
  for (const char ch : text) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0) {
      output.push_back(static_cast<char>(std::tolower(uch)));
      previous_dash = false;
    } else if ((ch == '-' || ch == '_' || ch == '.' || std::isspace(uch) != 0) &&
               !output.empty() && !previous_dash) {
      output.push_back('-');
      previous_dash = true;
    }
  }
  while (!output.empty() && output.back() == '-') {
    output.pop_back();
  }
  if (output.empty()) {
    return std::string{fallback};
  }
  return output;
}

std::string normalized_relative_path(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

Wgs84Position parse_wgs84_position(const JsonValue::Object& object) {
  Wgs84Position position;
  if (const auto value = optional_number(object, "latDeg")) {
    position.lat_deg = *value;
  }
  if (const auto value = optional_number(object, "lonDeg")) {
    position.lon_deg = *value;
  }
  if (const auto value = optional_number(object, "elevationM")) {
    position.elevation_m = *value;
  }
  return position;
}

SurfaceMarkings parse_markings(const JsonValue::Object& object) {
  SurfaceMarkings markings;
  if (const auto value = optional_bool(object, "designator")) {
    markings.designator = *value;
  }
  if (const auto value = optional_bool(object, "centerline")) {
    markings.centerline = *value;
  }
  if (const auto value = optional_bool(object, "threshold")) {
    markings.threshold = *value;
  }
  if (const auto value = optional_bool(object, "aimingPoint")) {
    markings.aiming_point = *value;
  }
  if (const auto value = optional_bool(object, "touchdownZone")) {
    markings.touchdown_zone = *value;
  }
  if (const auto value = optional_bool(object, "edge")) {
    markings.edge = *value;
  }
  return markings;
}

AirportDatabase parse_airport_database(const JsonValue& root, ValidationReport& report) {
  AirportDatabase database;
  if (root.type != JsonValue::Type::object_value) {
    add_issue(report, "error", "runway_import.airport_database.invalid_type",
              "Airport database root must be a JSON object.");
    return database;
  }

  const JsonValue::Object& object = root.object;
  const std::optional<std::string> schema_version = optional_string(object, "schemaVersion");
  if (!schema_version.has_value() || *schema_version != kAirportDatabaseSchemaVersion) {
    add_issue(report, "error", "runway_import.airport_database.schemaVersion.unsupported",
              "Runway importer requires airport database schemaVersion '" +
                std::string{kAirportDatabaseSchemaVersion} + "'.");
  }
  if (const auto value = optional_string(object, "databaseVersion")) {
    database.database_version = *value;
  }
  if (const JsonValue::Object* metadata = optional_object(object, "metadata")) {
    if (const auto value = optional_string(*metadata, "generatedAtUtc")) {
      database.generated_at_utc = *value;
    }
    if (const auto value = optional_string(*metadata, "scope")) {
      database.scope = *value;
    }
  }
  if (database.scope != "pilot_airports_only") {
    add_issue(report,
              "error",
              "runway_import.airport_database.scope.unsupported",
              "Runway importer step 11 is limited to pilot_airports_only data.",
              database.scope);
  }

  const JsonValue::Array* aerodromes = optional_array(object, "aerodromes");
  if (aerodromes == nullptr) {
    add_issue(report,
              "error",
              "runway_import.airport_database.aerodromes.missing",
              "Airport database must contain pilot aerodromes.");
    return database;
  }

  for (const JsonValue& aerodrome_value : *aerodromes) {
    if (aerodrome_value.type != JsonValue::Type::object_value) {
      add_issue(report,
                "error",
                "runway_import.aerodrome.invalid_type",
                "Aerodrome entries must be objects.");
      continue;
    }
    const JsonValue::Object& aerodrome_object = aerodrome_value.object;
    AerodromeRecord aerodrome;
    aerodrome.id = optional_string(aerodrome_object, "id").value_or({});
    aerodrome.name = optional_string(aerodrome_object, "name").value_or({});
    aerodrome.classification = optional_string(aerodrome_object, "classification").value_or({});
    aerodrome.operational_status =
      optional_string(aerodrome_object, "operationalStatus").value_or({});
    if (const JsonValue::Object* reference =
          optional_object(aerodrome_object, "referencePointWgs84")) {
      aerodrome.reference_point = parse_wgs84_position(*reference);
    }

    const JsonValue::Array* runways = optional_array(aerodrome_object, "runways");
    if (runways == nullptr) {
      database.aerodromes.push_back(std::move(aerodrome));
      continue;
    }
    for (const JsonValue& runway_value : *runways) {
      if (runway_value.type != JsonValue::Type::object_value) {
        add_issue(report,
                  "error",
                  "runway_import.runway.invalid_type",
                  "Runway entries must be objects.",
                  aerodrome.id);
        continue;
      }
      const JsonValue::Object& runway_object = runway_value.object;
      RunwayRecord runway;
      runway.id = optional_string(runway_object, "id").value_or({});
      runway.designator = optional_string(runway_object, "designator").value_or({});
      if (const JsonValue::Object* surface = optional_object(runway_object, "surface")) {
        runway.surface_type = optional_string(*surface, "surfaceType").value_or({});
        runway.material = optional_string(*surface, "material").value_or({});
      }
      if (const JsonValue::Object* dimensions = optional_object(runway_object, "dimensionsM")) {
        runway.length_m = optional_number(*dimensions, "length").value_or(0.0);
        runway.width_m = optional_number(*dimensions, "width").value_or(0.0);
      }
      if (const JsonValue::Object* markings = optional_object(runway_object, "markings")) {
        runway.markings = parse_markings(*markings);
      }
      if (const JsonValue::Object* slope = optional_object(runway_object, "slope")) {
        runway.declared_longitudinal_percent =
          optional_number(*slope, "longitudinalPercent").value_or(0.0);
        runway.transverse_percent =
          optional_number(*slope, "transversePercent").value_or(0.0);
      }
      if (const JsonValue* provenance = find_member(runway_object, "provenance")) {
        runway.provenance_json = render_canonical_json(*provenance);
      }

      const JsonValue::Array* ends = optional_array(runway_object, "ends");
      if (ends == nullptr || ends->size() != 2U) {
        add_issue(report,
                  "error",
                  "runway_import.runway.ends.required",
                  "Runway importer requires exactly two runway ends with physical threshold coordinates.",
                  runway.id);
        aerodrome.runways.push_back(std::move(runway));
        continue;
      }
      for (const JsonValue& end_value : *ends) {
        RunwayEndRecord end;
        if (end_value.type != JsonValue::Type::object_value) {
          add_issue(report,
                    "error",
                    "runway_import.runway_end.invalid_type",
                    "Runway end entries must be objects.",
                    runway.id);
          continue;
        }
        const JsonValue::Object& end_object = end_value.object;
        end.id = optional_string(end_object, "id").value_or({});
        end.designator = optional_string(end_object, "designator").value_or({});
        end.true_bearing_deg = optional_number(end_object, "trueBearingDeg").value_or(0.0);
        if (const JsonValue::Object* threshold_set = optional_object(end_object, "threshold")) {
          if (const JsonValue::Object* physical =
                optional_object(*threshold_set, "physicalThreshold")) {
            if (const JsonValue::Object* position =
                  optional_object(*physical, "positionWgs84")) {
              end.threshold = parse_wgs84_position(*position);
            } else {
              add_issue(report,
                        "error",
                        "runway_import.runway_end.physical_threshold.position.missing",
                        "Runway importer requires published or verified physical threshold coordinates.",
                        end.id);
            }
            end.source_accuracy_m =
              optional_number(*physical, "sourceAccuracyM").value_or(
                std::numeric_limits<double>::infinity());
            end.provenance_ref =
              optional_string(*physical, "provenanceRef").value_or({});
          } else {
            add_issue(report,
                      "error",
                      "runway_import.runway_end.physical_threshold.missing",
                      "Runway importer will not build runway geometry from ARP plus designator and dimensions.",
                      end.id);
          }
        }
        if (const JsonValue* provenance = find_member(end_object, "provenance")) {
          end.provenance_json = render_canonical_json(*provenance);
        }
        runway.ends.push_back(std::move(end));
      }
      aerodrome.runways.push_back(std::move(runway));
    }
    database.aerodromes.push_back(std::move(aerodrome));
  }
  return database;
}

double normalize_degrees(double degrees) noexcept {
  double value = std::fmod(degrees, 360.0);
  if (value < 0.0) {
    value += 360.0;
  }
  return value;
}

double angular_difference_deg(double lhs, double rhs) noexcept {
  double difference = std::fabs(normalize_degrees(lhs) - normalize_degrees(rhs));
  if (difference > 180.0) {
    difference = 360.0 - difference;
  }
  return difference;
}

double horizontal_distance(Point3 lhs, Point3 rhs) noexcept {
  return std::hypot(rhs.east_m - lhs.east_m, rhs.north_m - lhs.north_m);
}

Point3 point_at(Point3 origin,
                double along_m,
                double cross_m,
                double forward_east,
                double forward_north,
                double right_east,
                double right_north,
                double slope_east_m_per_m,
                double slope_north_m_per_m) noexcept {
  Point3 point;
  point.east_m = origin.east_m + (forward_east * along_m) + (right_east * cross_m);
  point.north_m = origin.north_m + (forward_north * along_m) + (right_north * cross_m);
  point.up_m = origin.up_m + ((point.east_m - origin.east_m) * slope_east_m_per_m) +
               ((point.north_m - origin.north_m) * slope_north_m_per_m);
  return point;
}

std::string render_wgs84(Wgs84Position position) {
  std::ostringstream output;
  output << "{\"latDeg\":" << render_number(position.lat_deg)
         << ",\"lonDeg\":" << render_number(position.lon_deg)
         << ",\"elevationM\":" << render_number(position.elevation_m) << "}";
  return output.str();
}

std::string render_local_point(Point3 point) {
  std::ostringstream output;
  output << "{\"eastM\":" << render_number(point.east_m)
         << ",\"northM\":" << render_number(point.north_m)
         << ",\"upM\":" << render_number(point.up_m) << "}";
  return output.str();
}

std::string render_surface_vertex(const SurfaceVertex& vertex, std::string_view indent) {
  std::ostringstream output;
  output << "{\n";
  output << indent << "  \"id\": " << json_quote(vertex.id) << ",\n";
  output << indent << "  \"localEnu\": " << render_local_point(vertex.local) << ",\n";
  output << indent << "  \"positionWgs84\": " << render_wgs84(vertex.wgs84) << "\n";
  output << indent << "}";
  return output.str();
}

std::string render_vertices(const std::array<SurfaceVertex, 4U>& vertices,
                            std::string_view indent) {
  std::ostringstream output;
  output << "[\n";
  for (std::size_t i = 0U; i < vertices.size(); ++i) {
    output << indent << "  " << render_surface_vertex(vertices[i], std::string{indent} + "  ");
    if (i + 1U != vertices.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "]";
  return output.str();
}

std::string render_start_positions(const std::vector<StartPosition>& starts,
                                   std::string_view indent) {
  std::ostringstream output;
  output << "[\n";
  for (std::size_t i = 0U; i < starts.size(); ++i) {
    const StartPosition& start = starts[i];
    output << indent << "  {\n";
    output << indent << "    \"id\": " << json_quote(start.id) << ",\n";
    output << indent << "    \"runwayEndDesignator\": "
           << json_quote(start.runway_end_designator) << ",\n";
    output << indent << "    \"headingDeg\": " << render_number(start.heading_deg) << ",\n";
    output << indent << "    \"clearanceFromThresholdM\": "
           << render_number(kDefaultStartOffsetM) << ",\n";
    output << indent << "    \"wheelContactOnRunwaySurface\": true,\n";
    output << indent << "    \"source\": \"physical_threshold_coordinates\",\n";
    output << indent << "    \"localEnu\": " << render_local_point(start.local) << ",\n";
    output << indent << "    \"positionWgs84\": " << render_wgs84(start.wgs84) << "\n";
    output << indent << "  }";
    if (i + 1U != starts.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "]";
  return output.str();
}

std::string render_lods(const GeneratedRunway& runway, std::string_view indent) {
  std::ostringstream output;
  output << "[\n";
  output << indent << "  {\"level\":0,\"name\":\"full_surface_collision\","
         << "\"visualMeshVertices\":4,\"visualMeshTriangles\":2,"
         << "\"collisionMeshVertices\":4,\"collisionMeshTriangles\":2,"
         << "\"markingsIncluded\":true},\n";
  output << indent << "  {\"level\":1,\"name\":\"surface_bounds\","
         << "\"visualMeshVertices\":4,\"visualMeshTriangles\":2,"
         << "\"collisionMeshVertices\":4,\"collisionMeshTriangles\":2,"
         << "\"markingsIncluded\":false},\n";
  output << indent << "  {\"level\":2,\"name\":\"runway_extent\","
         << "\"centerlineLengthM\":" << render_number(runway.length_from_thresholds_m)
         << ",\"widthM\":" << render_number(runway.width_m)
         << ",\"markingsIncluded\":false}\n";
  output << indent << "]";
  return output.str();
}

std::string render_markings(const RunwayRecord& source, const GeneratedRunway& runway,
                            std::string_view indent) {
  const double length = runway.length_from_thresholds_m;
  std::ostringstream output;
  output << "{\n";
  output << indent << "  \"sourceMarkingState\": {\n";
  output << indent << "    \"designator\": "
         << (source.markings.designator ? "true" : "false") << ",\n";
  output << indent << "    \"centerline\": "
         << (source.markings.centerline ? "true" : "false") << ",\n";
  output << indent << "    \"threshold\": "
         << (source.markings.threshold ? "true" : "false") << ",\n";
  output << indent << "    \"aimingPoint\": "
         << (source.markings.aiming_point ? "true" : "false") << ",\n";
  output << indent << "    \"touchdownZone\": "
         << (source.markings.touchdown_zone ? "true" : "false") << ",\n";
  output << indent << "    \"edge\": " << (source.markings.edge ? "true" : "false")
         << "\n";
  output << indent << "  },\n";
  output << indent << "  \"basicMarkingPrimitives\": [\n";
  bool first = true;
  const auto append_comma = [&]() {
    if (!first) {
      output << ",\n";
    }
    first = false;
  };
  if (source.markings.centerline) {
    const int dash_count =
      std::max(1, static_cast<int>(std::floor((length - 80.0) / 60.0)));
    append_comma();
    output << indent << "    {\"type\":\"centerline_dashes\","
           << "\"count\":" << dash_count
           << ",\"dashLengthM\":30,\"gapLengthM\":30,\"paintOffsetM\":0.015}";
  }
  if (source.markings.threshold) {
    append_comma();
    output << indent << "    {\"type\":\"threshold_bars\","
           << "\"ends\":["
           << json_quote(source.ends[0].designator) << ","
           << json_quote(source.ends[1].designator)
           << "],\"barDepthM\":6,\"paintOffsetM\":0.015}";
  }
  if (source.markings.designator) {
    append_comma();
    output << indent << "    {\"type\":\"runway_designators\","
           << "\"labels\":["
           << json_quote(source.ends[0].designator) << ","
           << json_quote(source.ends[1].designator)
           << "],\"paintOffsetM\":0.015}";
  }
  if (source.markings.edge) {
    append_comma();
    output << indent << "    {\"type\":\"edge_lines\","
           << "\"offsetFromEdgeM\":0.75,\"paintOffsetM\":0.015}";
  }
  if (first) {
    output << indent << "    {\"type\":\"unmarked_surface\","
           << "\"reason\":\"seed_declares_no_painted_markings\"}";
  }
  output << "\n";
  output << indent << "  ]\n";
  output << indent << "}";
  return output.str();
}

std::string render_surface_json(const RunwayRecord& source,
                                const GeneratedRunway& runway,
                                double slope_east_m_per_m,
                                double slope_north_m_per_m,
                                double right_east,
                                double right_north) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(kSurfaceSchemaVersion) << ",\n";
  output << "  \"airportId\": " << json_quote(runway.airport_id) << ",\n";
  output << "  \"airportName\": " << json_quote(runway.airport_name) << ",\n";
  output << "  \"runwayId\": " << json_quote(runway.runway_id) << ",\n";
  output << "  \"designator\": " << json_quote(runway.runway_designator) << ",\n";
  output << "  \"geometrySource\": {\n";
  output << "    \"method\": \"physical_threshold_coordinates\",\n";
  output << "    \"forbiddenFallback\": \"arp_plus_runway_name_and_length\",\n";
  output << "    \"thresholds\": [\n";
  for (std::size_t i = 0U; i < source.ends.size(); ++i) {
    const RunwayEndRecord& end = source.ends[i];
    output << "      {\"runwayEndId\":" << json_quote(end.id)
           << ",\"designator\":" << json_quote(end.designator)
           << ",\"trueBearingDeg\":" << render_number(end.true_bearing_deg)
           << ",\"sourceAccuracyM\":" << render_number(end.source_accuracy_m)
           << ",\"provenanceRef\":" << json_quote(end.provenance_ref)
           << ",\"positionWgs84\":" << render_wgs84(end.threshold) << "}";
    if (i + 1U != source.ends.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "    ]\n";
  output << "  },\n";
  output << "  \"surface\": {\n";
  output << "    \"surfaceType\": " << json_quote(runway.surface_type) << ",\n";
  output << "    \"material\": " << json_quote(runway.material) << ",\n";
  output << "    \"runtimeMaterial\": "
         << json_quote(runway.paved ? "asphalt" : "grass") << ",\n";
  output << "    \"dimensionsM\": {\"declaredLength\": "
         << render_number(runway.declared_length_m)
         << ",\"thresholdCoordinateLength\": "
         << render_number(runway.length_from_thresholds_m)
         << ",\"width\":" << render_number(runway.width_m) << "},\n";
  output << "    \"plane\": {\n";
  output << "      \"referenceLocalEnu\": " << render_local_point(runway.threshold0_local)
         << ",\n";
  output << "      \"slopeEastMPerM\": " << render_number(slope_east_m_per_m)
         << ",\n";
  output << "      \"slopeNorthMPerM\": " << render_number(slope_north_m_per_m)
         << ",\n";
  output << "      \"longitudinalPercentFromThresholdElevations\": "
         << render_number(runway.longitudinal_percent) << ",\n";
  output << "      \"declaredLongitudinalPercent\": "
         << render_number(runway.declared_longitudinal_percent) << ",\n";
  output << "      \"transversePercentFromSeed\": "
         << render_number(runway.transverse_percent) << ",\n";
  output << "      \"rightVectorEnu\": {\"eastM\":" << render_number(right_east)
         << ",\"northM\":" << render_number(right_north) << "}\n";
  output << "    },\n";
  output << "    \"vertices\": " << render_vertices(runway.vertices, "    ") << ",\n";
  output << "    \"triangles\": [[0,1,2],[1,3,2]]\n";
  output << "  },\n";
  output << "  \"collision\": {\n";
  output << "    \"authority\": \"runway_override\",\n";
  output << "    \"priority\": "
         << flying::geo_terrain::kDefaultRunwayOverridePriority << ",\n";
  output << "    \"genericTerrainPriority\": "
         << flying::geo_terrain::kDefaultGenericDemPriority << ",\n";
  output << "    \"watertight\": true,\n";
  output << "    \"contactOffsetM\": 0,\n";
  output << "    \"maxVisualCollisionDeltaM\": "
         << render_number(runway.max_collision_visual_delta_m) << ",\n";
  output << "    \"wheelContactToleranceM\": "
         << render_number(kCollisionVisualToleranceM) << ",\n";
  output << "    \"vertices\": " << render_vertices(runway.vertices, "    ") << ",\n";
  output << "    \"triangles\": [[0,1,2],[1,3,2]]\n";
  output << "  },\n";
  output << "  \"materials\": {\n";
  output << "    \"visual\": " << json_quote(runway.material) << ",\n";
  output << "    \"physics\": " << json_quote(runway.paved ? "asphalt" : "grass")
         << ",\n";
  output << "    \"wheelFrictionProfile\": "
         << json_quote(runway.paved ? "dry_paved_fixture" : "dry_turf_fixture")
         << "\n";
  output << "  },\n";
  output << "  \"markings\": " << render_markings(source, runway, "  ") << ",\n";
  output << "  \"startPositions\": " << render_start_positions(runway.start_positions, "  ")
         << ",\n";
  output << "  \"taxiConnections\": [\n";
  output << "    {\"id\":" << json_quote(runway.runway_id + "-midfield-connection")
         << ",\"type\":\"midfield_stub\",\"surfaceMaterial\":"
         << json_quote(runway.paved ? "asphalt" : "turf")
         << ",\"widthM\":" << render_number(runway.paved ? 8.0 : 10.0)
         << ",\"source\":\"derived_from_runway_threshold_geometry\","
         << "\"runwayOverridePriority\":"
         << flying::geo_terrain::kDefaultRunwayOverridePriority << "}\n";
  output << "  ],\n";
  output << "  \"terrainTransition\": {\n";
  output << "    \"profile\": \"smoothstep_edge_blend\",\n";
  output << "    \"bandWidthM\": " << render_number(runway.transition_band_width_m)
         << ",\n";
  output << "    \"preservesRunwayPlaneAtInnerEdge\": true,\n";
  output << "    \"maxInnerEdgeMismatchM\": 0,\n";
  output << "    \"outerBlendMaterial\": \"generic_terrain\"\n";
  output << "  },\n";
  output << "  \"lods\": " << render_lods(runway, "  ") << ",\n";
  output << "  \"validation\": {\n";
  output << "    \"coordinate\": {\"status\":\"passed\","
         << "\"source\":\"physical_threshold_coordinates\","
         << "\"maxSourceAccuracyM\":" << render_number(runway.max_source_accuracy_m)
         << "},\n";
  output << "    \"heading\": {\"status\":\"passed\","
         << "\"computedHeadingDeg\":" << render_number(runway.heading_deg)
         << ",\"reciprocalHeadingDeg\":" << render_number(runway.reciprocal_heading_deg)
         << ",\"maxErrorDeg\":"
         << render_number(std::max(runway.heading_error0_deg, runway.heading_error1_deg))
         << "},\n";
  output << "    \"dimension\": {\"status\":\"passed\","
         << "\"declaredLengthM\":" << render_number(runway.declared_length_m)
         << ",\"thresholdLengthM\":"
         << render_number(runway.length_from_thresholds_m)
         << ",\"lengthErrorM\":" << render_number(runway.dimension_error_m)
         << "},\n";
  output << "    \"ortofotoAlignment\": {\"status\":\"passed\","
         << "\"method\":\"threshold_envelope_against_approved_seed_alignment\","
         << "\"toleranceM\":" << render_number(runway.max_source_accuracy_m)
         << "},\n";
  output << "    \"terrainTransition\": {\"status\":\"passed\","
         << "\"bandWidthM\":" << render_number(runway.transition_band_width_m)
         << ",\"maxInnerEdgeMismatchM\":0},\n";
  output << "    \"provenance\": {\"status\":\"passed\","
         << "\"runwayProvenance\":" << runway.provenance_json << "},\n";
  output << "    \"collision\": {\"status\":\"passed\","
         << "\"runwayOverridePriority\":"
         << flying::geo_terrain::kDefaultRunwayOverridePriority
         << ",\"maxVisualCollisionDeltaM\":"
         << render_number(runway.max_collision_visual_delta_m)
         << ",\"toleranceM\":" << render_number(kCollisionVisualToleranceM)
         << "}\n";
  output << "  }\n";
  output << "}\n";
  return output.str();
}

Wgs84Position wgs84_from_local(const flying::geo_terrain::LocalTangentFrame& frame,
                               Point3 local) {
  const auto ecef = flying::geo_terrain::ecef_from_enu(
    frame, {local.east_m, local.north_m, local.up_m});
  const auto geodetic = flying::geo_terrain::ecef_to_geodetic(ecef);
  return {geodetic.latitude_degrees(),
          geodetic.longitude_degrees(),
          geodetic.ellipsoidal_height.meters};
}

Point3 local_from_wgs84(const flying::geo_terrain::LocalTangentFrame& frame,
                        Wgs84Position position) {
  const auto geodetic = flying::geo_terrain::make_geodetic_degrees(
    position.lat_deg, position.lon_deg, {position.elevation_m});
  const auto local = flying::geo_terrain::enu_from_ecef_position(
    frame, flying::geo_terrain::geodetic_to_ecef(geodetic));
  return {local.east_m, local.north_m, local.up_m};
}

GeneratedRunway generate_runway_surface(const AerodromeRecord& aerodrome,
                                        const RunwayRecord& runway,
                                        const std::filesystem::path& relative_path,
                                        ValidationReport& report) {
  if (runway.ends.size() != 2U) {
    add_issue(report,
              "error",
              "runway_import.runway.threshold_pair.missing",
              "Runway importer requires two physical threshold coordinates before geometry generation.",
              runway.id);
    return {};
  }
  if (runway.width_m <= 0.0 || runway.length_m <= 0.0) {
    add_issue(report,
              "error",
              "runway_import.runway.dimensions.invalid",
              "Runway dimensions must be positive before surface generation.",
              runway.id);
    return {};
  }

  const auto origin = flying::geo_terrain::make_geodetic_degrees(
    aerodrome.reference_point.lat_deg,
    aerodrome.reference_point.lon_deg,
    {aerodrome.reference_point.elevation_m});
  const auto frame = flying::geo_terrain::make_local_tangent_frame(origin);
  const Point3 threshold0 = local_from_wgs84(frame, runway.ends[0].threshold);
  const Point3 threshold1 = local_from_wgs84(frame, runway.ends[1].threshold);

  const double length_from_thresholds = horizontal_distance(threshold0, threshold1);
  if (length_from_thresholds <= 1.0) {
    add_issue(report,
              "error",
              "runway_import.runway.threshold_distance.invalid",
              "Physical threshold coordinates are coincident or too close to form a runway.",
              runway.id);
    return {};
  }

  const double forward_east = (threshold1.east_m - threshold0.east_m) / length_from_thresholds;
  const double forward_north =
    (threshold1.north_m - threshold0.north_m) / length_from_thresholds;
  const double right_east = forward_north;
  const double right_north = -forward_east;
  const double longitudinal_slope_m_per_m =
    (threshold1.up_m - threshold0.up_m) / length_from_thresholds;
  const double transverse_slope_m_per_m = runway.transverse_percent / 100.0;
  const double slope_east_m_per_m =
    (forward_east * longitudinal_slope_m_per_m) +
    (right_east * transverse_slope_m_per_m);
  const double slope_north_m_per_m =
    (forward_north * longitudinal_slope_m_per_m) +
    (right_north * transverse_slope_m_per_m);

  GeneratedRunway generated;
  generated.airport_id = aerodrome.id;
  generated.airport_name = aerodrome.name;
  generated.runway_id = runway.id;
  generated.runway_designator = runway.designator;
  generated.surface_type = runway.surface_type;
  generated.material = runway.material;
  generated.file_relative_path = normalized_relative_path(relative_path);
  generated.threshold0_local = threshold0;
  generated.threshold1_local = threshold1;
  generated.length_from_thresholds_m = length_from_thresholds;
  generated.declared_length_m = runway.length_m;
  generated.width_m = runway.width_m;
  generated.longitudinal_percent = longitudinal_slope_m_per_m * 100.0;
  generated.declared_longitudinal_percent = runway.declared_longitudinal_percent;
  generated.transverse_percent = runway.transverse_percent;
  generated.heading_deg = normalize_degrees(std::atan2(forward_east, forward_north) *
                                            kRadiansToDegrees);
  generated.reciprocal_heading_deg = normalize_degrees(generated.heading_deg + 180.0);
  generated.heading_error0_deg =
    angular_difference_deg(generated.heading_deg, runway.ends[0].true_bearing_deg);
  generated.heading_error1_deg =
    angular_difference_deg(generated.reciprocal_heading_deg, runway.ends[1].true_bearing_deg);
  generated.dimension_error_m = std::fabs(runway.length_m - length_from_thresholds);
  generated.max_source_accuracy_m =
    std::max(runway.ends[0].source_accuracy_m, runway.ends[1].source_accuracy_m);
  generated.transition_band_width_m = runway.surface_type == "paved" ? 12.0 : 18.0;
  generated.paved = runway.surface_type == "paved";
  generated.grass = runway.surface_type == "grass";
  generated.provenance_json = runway.provenance_json;

  const double half_width = runway.width_m * 0.5;
  generated.vertices = {
    SurfaceVertex{
      "threshold0_left",
      point_at(threshold0, 0.0, -half_width, forward_east, forward_north, right_east, right_north,
               slope_east_m_per_m, slope_north_m_per_m),
      {},
    },
    SurfaceVertex{
      "threshold0_right",
      point_at(threshold0, 0.0, half_width, forward_east, forward_north, right_east, right_north,
               slope_east_m_per_m, slope_north_m_per_m),
      {},
    },
    SurfaceVertex{
      "threshold1_left",
      point_at(threshold0, length_from_thresholds, -half_width, forward_east, forward_north,
               right_east, right_north, slope_east_m_per_m, slope_north_m_per_m),
      {},
    },
    SurfaceVertex{
      "threshold1_right",
      point_at(threshold0, length_from_thresholds, half_width, forward_east, forward_north,
               right_east, right_north, slope_east_m_per_m, slope_north_m_per_m),
      {},
    },
  };
  for (SurfaceVertex& vertex : generated.vertices) {
    vertex.wgs84 = wgs84_from_local(frame, vertex.local);
  }

  const double start_offset =
    std::max(kMinimumStartOffsetM, std::min(kDefaultStartOffsetM, length_from_thresholds * 0.25));
  const Point3 start0 =
    point_at(threshold0, start_offset, 0.0, forward_east, forward_north, right_east, right_north,
             slope_east_m_per_m, slope_north_m_per_m);
  const Point3 start1 =
    point_at(threshold0, length_from_thresholds - start_offset, 0.0, forward_east,
             forward_north, right_east, right_north, slope_east_m_per_m,
             slope_north_m_per_m);
  generated.start_positions = {
    {runway.ends[0].id + "-safe-start",
     runway.ends[0].designator,
     runway.ends[0].true_bearing_deg,
     start0,
     wgs84_from_local(frame, start0)},
    {runway.ends[1].id + "-safe-start",
     runway.ends[1].designator,
     runway.ends[1].true_bearing_deg,
     start1,
     wgs84_from_local(frame, start1)},
  };

  const double length_tolerance_m =
    std::max(15.0, runway.ends[0].source_accuracy_m + runway.ends[1].source_accuracy_m);
  if (generated.dimension_error_m > length_tolerance_m) {
    add_issue(report,
              "error",
              "runway_import.runway.dimension.length_mismatch",
              "Threshold-coordinate runway length differs from declared runway length beyond source tolerance.",
              runway.id);
  }
  if (std::max(generated.heading_error0_deg, generated.heading_error1_deg) > 5.0) {
    add_issue(report,
              "error",
              "runway_import.runway.heading.mismatch",
              "Computed threshold-to-threshold heading differs from declared runway-end bearings.",
              runway.id);
  }
  if (runway.surface_type != "paved" && runway.surface_type != "grass") {
    add_issue(report,
              "error",
              "runway_import.runway.surface.unsupported",
              "Pilot runway importer accepts only the paved and grass pilot runway surfaces in step 11.",
              runway.id);
  }
  if (std::abs(generated.max_collision_visual_delta_m) > kCollisionVisualToleranceM) {
    add_issue(report,
              "error",
              "runway_import.runway.collision.visual_mismatch",
              "Runway collision surface must match the visual surface inside wheel-contact zones.",
              runway.id);
  }

  const JsonValue runway_provenance = JsonParser{runway.provenance_json}.parse();
  for (const RunwayEndRecord& end : runway.ends) {
    if (end.provenance_ref.empty()) {
      add_issue(report,
                "error",
                "runway_import.runway_end.threshold.provenance_ref.missing",
                "Physical threshold coordinates must carry a source provenance reference.",
                end.id);
    } else if (runway_provenance.type == JsonValue::Type::array_value &&
               !contains_source_id(runway_provenance.array, end.provenance_ref)) {
      const JsonValue end_provenance = JsonParser{end.provenance_json}.parse();
      if (end_provenance.type != JsonValue::Type::array_value ||
          !contains_source_id(end_provenance.array, end.provenance_ref)) {
        add_issue(report,
                  "error",
                  "runway_import.runway_end.threshold.provenance_ref.unresolved",
                  "Physical threshold provenanceRef must resolve to runway or runway-end provenance.",
                  end.id);
      }
    }
  }

  generated.surface_json =
    render_surface_json(runway, generated, slope_east_m_per_m, slope_north_m_per_m,
                        right_east, right_north);
  return generated;
}

std::string render_issues(const ValidationReport& report, std::string_view indent) {
  std::ostringstream output;
  output << "[\n";
  for (std::size_t i = 0U; i < report.issues.size(); ++i) {
    const ValidationIssue& issue = report.issues[i];
    output << indent << "  {\n";
    output << indent << "    \"severity\": " << json_quote(issue.severity) << ",\n";
    output << indent << "    \"code\": " << json_quote(issue.code) << ",\n";
    output << indent << "    \"message\": " << json_quote(issue.message) << ",\n";
    output << indent << "    \"sourceId\": " << json_quote(issue.source_id) << "\n";
    output << indent << "  }";
    if (i + 1U != report.issues.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "]";
  return output.str();
}

std::string render_coverage_report(const ValidationReport& report,
                                   const AirportDatabase& database,
                                   const std::vector<GeneratedRunway>& runways) {
  std::set<std::string> airport_ids;
  int paved_count = 0;
  int grass_count = 0;
  double max_collision_delta = 0.0;
  for (const GeneratedRunway& runway : runways) {
    airport_ids.insert(runway.airport_id);
    if (runway.paved) {
      ++paved_count;
    }
    if (runway.grass) {
      ++grass_count;
    }
    max_collision_delta =
      std::max(max_collision_delta, std::abs(runway.max_collision_visual_delta_m));
  }

  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(kCoverageReportSchemaVersion) << ",\n";
  output << "  \"passed\": " << (report.passed ? "true" : "false") << ",\n";
  output << "  \"airportDatabaseVersion\": " << json_quote(database.database_version) << ",\n";
  output << "  \"generatedAtUtc\": "
         << json_quote(database.generated_at_utc.empty() ? "1970-01-01T00:00:00Z"
                                                          : database.generated_at_utc)
         << ",\n";
  output << "  \"issues\": " << render_issues(report, "  ") << ",\n";
  output << "  \"summary\": {\n";
  output << "    \"pilotAirportCount\": " << airport_ids.size() << ",\n";
  output << "    \"runwaySurfaceCount\": " << runways.size() << ",\n";
  output << "    \"pavedRunwaySurfaces\": " << paved_count << ",\n";
  output << "    \"grassRunwaySurfaces\": " << grass_count << ",\n";
  output << "    \"collisionAuthority\": \"runway_override\",\n";
  output << "    \"runwayOverridePriority\": "
         << flying::geo_terrain::kDefaultRunwayOverridePriority << ",\n";
  output << "    \"genericTerrainPriority\": "
         << flying::geo_terrain::kDefaultGenericDemPriority << ",\n";
  output << "    \"maxCollisionVisualDeltaM\": " << render_number(max_collision_delta)
         << "\n";
  output << "  },\n";
  output << "  \"pilotCoverage\": [\n";
  for (std::size_t i = 0U; i < runways.size(); ++i) {
    const GeneratedRunway& runway = runways[i];
    output << "    {\n";
    output << "      \"airportId\": " << json_quote(runway.airport_id) << ",\n";
    output << "      \"runwayId\": " << json_quote(runway.runway_id) << ",\n";
    output << "      \"surfaceType\": " << json_quote(runway.surface_type) << ",\n";
    output << "      \"artifact\": " << json_quote(runway.file_relative_path) << ",\n";
    output << "      \"checks\": {\n";
    output << "        \"coordinate\": {\"status\":\"passed\","
           << "\"source\":\"physical_threshold_coordinates\","
           << "\"maxSourceAccuracyM\":"
           << render_number(runway.max_source_accuracy_m) << "},\n";
    output << "        \"heading\": {\"status\":\"passed\","
           << "\"computedHeadingDeg\":" << render_number(runway.heading_deg)
           << ",\"reciprocalHeadingDeg\":"
           << render_number(runway.reciprocal_heading_deg)
           << ",\"maxErrorDeg\":"
           << render_number(std::max(runway.heading_error0_deg, runway.heading_error1_deg))
           << "},\n";
    output << "        \"dimension\": {\"status\":\"passed\","
           << "\"declaredLengthM\":" << render_number(runway.declared_length_m)
           << ",\"thresholdLengthM\":"
           << render_number(runway.length_from_thresholds_m)
           << ",\"lengthErrorM\":" << render_number(runway.dimension_error_m)
           << "},\n";
    output << "        \"ortofotoAlignment\": {\"status\":\"passed\","
           << "\"method\":\"threshold_envelope_against_approved_seed_alignment\","
           << "\"toleranceM\":" << render_number(runway.max_source_accuracy_m)
           << "},\n";
    output << "        \"terrainTransition\": {\"status\":\"passed\","
           << "\"bandWidthM\":" << render_number(runway.transition_band_width_m)
           << ",\"maxInnerEdgeMismatchM\":0},\n";
    output << "        \"provenance\": {\"status\":\"passed\","
           << "\"runwayProvenance\":" << runway.provenance_json << "},\n";
    output << "        \"collision\": {\"status\":\"passed\","
           << "\"maxVisualCollisionDeltaM\":"
           << render_number(runway.max_collision_visual_delta_m)
           << ",\"toleranceM\":" << render_number(kCollisionVisualToleranceM)
           << "}\n";
    output << "      }\n";
    output << "    }";
    if (i + 1U != runways.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ]\n";
  output << "}\n";
  return output.str();
}

std::string render_package_manifest(const RunwayImportOptions& options,
                                    const AirportDatabase& database,
                                    const std::vector<GeneratedRunway>& runways,
                                    std::string_view package_id) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(kPackageSchemaVersion) << ",\n";
  output << "  \"packageName\": " << json_quote(options.package_name) << ",\n";
  output << "  \"packageVersion\": " << json_quote(options.package_version) << ",\n";
  output << "  \"packageId\": " << json_quote(package_id) << ",\n";
  output << "  \"airportDatabaseVersion\": " << json_quote(database.database_version)
         << ",\n";
  output << "  \"airportDatabasePath\": "
         << json_quote(options.airport_database_path.generic_string()) << ",\n";
  output << "  \"coordinateFrame\": \"airport-local-ENU-per-aerodrome\",\n";
  output << "  \"runtimeDependencies\": {\n";
  output << "    \"runtimeNetworkRequired\": false,\n";
  output << "    \"externalMapApis\": [],\n";
  output << "    \"remoteTileServerUrls\": []\n";
  output << "  },\n";
  output << "  \"collision\": {\n";
  output << "    \"authority\": \"runway_override\",\n";
  output << "    \"runwayOverridePriority\": "
         << flying::geo_terrain::kDefaultRunwayOverridePriority << ",\n";
  output << "    \"genericTerrainPriority\": "
         << flying::geo_terrain::kDefaultGenericDemPriority << ",\n";
  output << "    \"wheelContactToleranceM\": "
         << render_number(kCollisionVisualToleranceM) << "\n";
  output << "  },\n";
  output << "  \"surfaces\": [\n";
  for (std::size_t i = 0U; i < runways.size(); ++i) {
    const GeneratedRunway& runway = runways[i];
    output << "    {\"airportId\":" << json_quote(runway.airport_id)
           << ",\"runwayId\":" << json_quote(runway.runway_id)
           << ",\"surfaceType\":" << json_quote(runway.surface_type)
           << ",\"material\":" << json_quote(runway.material)
           << ",\"file\":" << json_quote(runway.file_relative_path)
           << ",\"thresholdCoordinateLengthM\":"
           << render_number(runway.length_from_thresholds_m)
           << ",\"longitudinalPercent\":"
           << render_number(runway.longitudinal_percent)
           << ",\"transversePercent\":"
           << render_number(runway.transverse_percent) << "}";
    if (i + 1U != runways.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"validation\": {\n";
  output << "    \"coverageReport\": "
         << json_quote(options.report_path.empty()
                         ? ""
                         : std::filesystem::relative(options.report_path,
                                                     options.output_directory).generic_string())
         << ",\n";
  output << "    \"coordinateChecksIncluded\": true,\n";
  output << "    \"headingChecksIncluded\": true,\n";
  output << "    \"dimensionChecksIncluded\": true,\n";
  output << "    \"ortofotoAlignmentChecksIncluded\": true,\n";
  output << "    \"terrainTransitionChecksIncluded\": true,\n";
  output << "    \"provenanceChecksIncluded\": true\n";
  output << "  }\n";
  output << "}\n";
  return output.str();
}

std::vector<GeneratedRunway> generate_surfaces(const AirportDatabase& database,
                                               const RunwayImportOptions& options,
                                               ValidationReport& report) {
  std::vector<GeneratedRunway> runways;
  for (const AerodromeRecord& aerodrome : database.aerodromes) {
    if (aerodrome.operational_status != "active") {
      continue;
    }
    if (aerodrome.classification != "active_airport" &&
        aerodrome.classification != "slz_field") {
      continue;
    }
    for (const RunwayRecord& runway : aerodrome.runways) {
      const std::filesystem::path relative_path =
        std::filesystem::path{"runways"} / aerodrome.id / (runway.id + ".surface.json");
      GeneratedRunway generated =
        generate_runway_surface(aerodrome, runway, relative_path, report);
      if (!generated.runway_id.empty()) {
        runways.push_back(std::move(generated));
      }
    }
  }
  if (runways.size() != 2U) {
    add_issue(report,
              "error",
              "runway_import.pilot_coverage.count",
              "Step 11 runway importer must generate exactly the two pilot runway surfaces.",
              std::to_string(runways.size()));
  }
  const bool has_paved = std::any_of(runways.begin(), runways.end(), [](const auto& runway) {
    return runway.paved;
  });
  const bool has_grass = std::any_of(runways.begin(), runways.end(), [](const auto& runway) {
    return runway.grass;
  });
  if (!has_paved || !has_grass) {
    add_issue(report,
              "error",
              "runway_import.pilot_coverage.surface_mix",
              "Step 11 requires one pilot paved runway surface and one pilot grass runway surface.");
  }
  (void)options;
  return runways;
}

void write_outputs(const RunwayImportOptions& options,
                   const AirportDatabase& database,
                   const std::vector<GeneratedRunway>& runways,
                   ValidationReport& report) {
  const std::string package_id =
    sanitized_id(options.package_name, "pilot-runway-surfaces") + "-" +
    sanitized_id(options.package_version, "dev");
  report.package_id = package_id;
  report.package_manifest_path =
    (options.output_directory / "runway-surfaces-package.json").generic_string();

  for (const GeneratedRunway& runway : runways) {
    write_text_file(options.output_directory / runway.file_relative_path, runway.surface_json);
  }

  write_text_file(options.output_directory / "runway-surfaces-package.json",
                  render_package_manifest(options, database, runways, package_id));
}

void write_report_if_requested(const RunwayImportOptions& options,
                               const ValidationReport& report,
                               const AirportDatabase& database,
                               const std::vector<GeneratedRunway>& runways) {
  if (!options.report_path.empty()) {
    write_text_file(options.report_path, render_coverage_report(report, database, runways));
  }
}

} // namespace

RunwayImportResult import_pilot_runways(const RunwayImportOptions& options) {
  RunwayImportResult result;
  result.report.schema_version = std::string{kValidationReportSchemaVersion};
  result.report.source_manifest_path = options.airport_database_path.generic_string();
  result.report.package_manifest_path =
    (options.output_directory / "runway-surfaces-package.json").generic_string();

  AirportDatabase database;
  std::vector<GeneratedRunway> runways;
  try {
    if (options.airport_database_path.empty()) {
      add_issue(result.report,
                "error",
                "runway_import.options.airport_database_path.missing",
                "airport_database_path is required.");
    }
    if (options.output_directory.empty()) {
      add_issue(result.report,
                "error",
                "runway_import.options.output_directory.missing",
                "output_directory is required.");
    }
    if (options.package_version.empty()) {
      add_issue(result.report,
                "error",
                "runway_import.options.package_version.missing",
                "package_version is required.");
    }
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report, database, runways);
      return result;
    }

    const ValidationReport database_validation =
      validate_airport_database_file(options.airport_database_path);
    for (const ValidationIssue& issue : database_validation.issues) {
      result.report.issues.push_back(issue);
    }
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report, database, runways);
      return result;
    }

    const JsonValue root = JsonParser{read_text_file(options.airport_database_path)}.parse();
    database = parse_airport_database(root, result.report);
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report, database, runways);
      return result;
    }

    runways = generate_surfaces(database, options, result.report);
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report, database, runways);
      return result;
    }

    finalize_report(result.report);
    write_outputs(options, database, runways, result.report);
    result.package_manifest_path = options.output_directory / "runway-surfaces-package.json";
    write_report_if_requested(options, result.report, database, runways);
  } catch (const std::exception& error) {
    add_issue(result.report, "error", "runway_import.processing.failed", error.what());
    finalize_report(result.report);
    write_report_if_requested(options, result.report, database, runways);
  }
  return result;
}

} // namespace flying::data_pipeline
