#include "flying/data_pipeline/dmr5g_terrain.hpp"

#include "flying/geo_terrain/heights.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
#include <tuple>
#include <utility>

namespace flying::data_pipeline {
namespace {

constexpr std::string_view kTerrainConfigSchemaVersion =
  "flying.dmr5g-pilot-terrain-config.v1";
constexpr std::string_view kCzechTerrainConfigSchemaVersion =
  "flying.dmr5g-czech-republic-terrain-config.v1";
constexpr std::string_view kTerrainPackageSchemaVersion = "flying.terrain-package.v1";
constexpr std::string_view kValidationReportSchemaVersion = "flying.validation-report.v1";
constexpr double kPilotRegionWidthM = 50'000.0;
constexpr double kPilotRegionHeightM = 50'000.0;
constexpr double kCoordinateToleranceM = 1.0e-6;

struct Bounds {
  double min_east_m = 0.0;
  double max_east_m = 0.0;
  double min_north_m = 0.0;
  double max_north_m = 0.0;
};

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
        default:
          throw std::runtime_error("unsupported JSON escape");
      }
    }
    throw std::runtime_error("unterminated JSON string");
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

struct PilotRegion {
  std::string id;
  double min_east_m = 0.0;
  double min_north_m = 0.0;
  double width_m = 0.0;
  double height_m = 0.0;

  [[nodiscard]] double max_east_m() const noexcept { return min_east_m + width_m; }
  [[nodiscard]] double max_north_m() const noexcept { return min_north_m + height_m; }
};

struct AxisAlignedTransform {
  double east_from_source_x_scale = 1.0;
  double east_offset_m = 0.0;
  double north_from_source_y_scale = 1.0;
  double north_offset_m = 0.0;
  double height_scale = 1.0;
  double orthometric_offset_m = 0.0;
  double geoid_undulation_m = 0.0;

  [[nodiscard]] double east_m(double source_x_m) const noexcept {
    return east_from_source_x_scale * source_x_m + east_offset_m;
  }

  [[nodiscard]] double north_m(double source_y_m) const noexcept {
    return north_from_source_y_scale * source_y_m + north_offset_m;
  }

  [[nodiscard]] double orthometric_height_m(double source_height_m) const noexcept {
    return height_scale * source_height_m + orthometric_offset_m;
  }

  [[nodiscard]] double ellipsoidal_height_m(double orthometric_height_m) const noexcept {
    return geo_terrain::ellipsoidal_from_orthometric(
             geo_terrain::OrthometricHeight{orthometric_height_m},
             geo_terrain::GeoidUndulation{geoid_undulation_m})
      .meters;
  }
};

struct SourceTileConfig {
  std::string id;
  std::string source_manifest_id;
  std::filesystem::path path;
  std::string format = "xyz";
  double declared_vertical_error_m = 0.0;
};

struct LodConfig {
  int level = 0;
  int sample_stride = 1;
};

struct ActiveAircraftZone {
  double min_east_m = 0.0;
  double max_east_m = 0.0;
  double min_north_m = 0.0;
  double max_north_m = 0.0;
  int sample_stride = 1;
};

struct ControlPoint {
  std::string id;
  double source_x_m = 0.0;
  double source_y_m = 0.0;
  double expected_orthometric_height_m = 0.0;
  double source_vertical_error_m = 0.0;
};

struct TerrainConfig {
  std::string schema_version;
  std::string package_id_hint;
  std::string coverage_scope = "pilot-region";
  std::string transform_json;
  PilotRegion pilot_region;
  AxisAlignedTransform transform;
  std::vector<SourceTileConfig> source_tiles;
  std::vector<LodConfig> render_lods;
  ActiveAircraftZone active_aircraft_zone;
  std::vector<ControlPoint> control_points;
  double edge_tolerance_m = 1.0e-6;
  double boundary_clean_max_adjustment_m = 0.10;
};

struct TerrainNormal {
  double east = 0.0;
  double north = 0.0;
  double up = 1.0;
};

struct TerrainTile {
  std::string id;
  std::string source_manifest_id;
  std::filesystem::path source_path;
  double declared_vertical_error_m = 0.0;
  std::vector<double> east_values;
  std::vector<double> north_values;
  std::vector<double> orthometric_heights;
  std::vector<double> ellipsoidal_heights;
  std::vector<TerrainNormal> normals;
  std::size_t rows = 0U;
  std::size_t cols = 0U;

  [[nodiscard]] std::size_t index(std::size_t row, std::size_t col) const noexcept {
    return row * cols + col;
  }

  [[nodiscard]] double min_east_m() const noexcept { return east_values.front(); }
  [[nodiscard]] double max_east_m() const noexcept { return east_values.back(); }
  [[nodiscard]] double min_north_m() const noexcept { return north_values.front(); }
  [[nodiscard]] double max_north_m() const noexcept { return north_values.back(); }
};

bool continuous_rect_coverage(const std::vector<Bounds>& bounds,
                              const Bounds& required_bounds) {
  if (bounds.empty()) {
    return false;
  }

  std::vector<double> east_breaks = {
    required_bounds.min_east_m,
    required_bounds.max_east_m,
  };
  for (const Bounds& candidate : bounds) {
    if (candidate.max_east_m < required_bounds.min_east_m + kCoordinateToleranceM ||
        candidate.min_east_m > required_bounds.max_east_m - kCoordinateToleranceM ||
        candidate.max_north_m < required_bounds.min_north_m + kCoordinateToleranceM ||
        candidate.min_north_m > required_bounds.max_north_m - kCoordinateToleranceM) {
      continue;
    }
    east_breaks.push_back(
      std::clamp(candidate.min_east_m, required_bounds.min_east_m, required_bounds.max_east_m));
    east_breaks.push_back(
      std::clamp(candidate.max_east_m, required_bounds.min_east_m, required_bounds.max_east_m));
  }
  std::sort(east_breaks.begin(), east_breaks.end());
  east_breaks.erase(std::unique(east_breaks.begin(),
                                east_breaks.end(),
                                [](double lhs, double rhs) {
                                  return std::fabs(lhs - rhs) <= kCoordinateToleranceM;
                                }),
                    east_breaks.end());

  if (east_breaks.size() < 2U ||
      east_breaks.front() > required_bounds.min_east_m + kCoordinateToleranceM ||
      east_breaks.back() < required_bounds.max_east_m - kCoordinateToleranceM) {
    return false;
  }

  for (std::size_t i = 0U; i + 1U < east_breaks.size(); ++i) {
    const double slab_min = east_breaks[i];
    const double slab_max = east_breaks[i + 1U];
    if (slab_max - slab_min <= kCoordinateToleranceM) {
      continue;
    }
    const double slab_mid = (slab_min + slab_max) * 0.5;
    std::vector<std::pair<double, double>> north_intervals;
    for (const Bounds& candidate : bounds) {
      if (slab_mid >= candidate.min_east_m - kCoordinateToleranceM &&
          slab_mid <= candidate.max_east_m + kCoordinateToleranceM) {
        const double clipped_min =
          std::max(candidate.min_north_m, required_bounds.min_north_m);
        const double clipped_max =
          std::min(candidate.max_north_m, required_bounds.max_north_m);
        if (clipped_max > clipped_min + kCoordinateToleranceM) {
          north_intervals.emplace_back(clipped_min, clipped_max);
        }
      }
    }
    if (north_intervals.empty()) {
      return false;
    }
    std::sort(north_intervals.begin(), north_intervals.end());
    double covered_until = required_bounds.min_north_m;
    for (const auto& [north_min, north_max] : north_intervals) {
      if (north_min > covered_until + kCoordinateToleranceM) {
        return false;
      }
      covered_until = std::max(covered_until, north_max);
      if (covered_until >= required_bounds.max_north_m - kCoordinateToleranceM) {
        break;
      }
    }
    if (covered_until < required_bounds.max_north_m - kCoordinateToleranceM) {
      return false;
    }
  }

  return true;
}

struct ControlPointResult {
  std::string id;
  double east_m = 0.0;
  double north_m = 0.0;
  double sampled_ellipsoidal_height_m = 0.0;
  double expected_ellipsoidal_height_m = 0.0;
  double absolute_error_m = 0.0;
  double allowed_error_m = 0.0;
  bool passed = false;
};

struct EdgeContinuityResult {
  std::size_t adjacent_pair_count = 0U;
  std::size_t compared_sample_count = 0U;
  std::size_t unmatched_sample_count = 0U;
  double max_abs_step_m = 0.0;
  bool passed = true;
};

struct BoundaryCleanResult {
  std::size_t compared_sample_count = 0U;
  std::size_t adjusted_sample_count = 0U;
  double max_pre_clean_step_m = 0.0;
  double max_adjustment_m = 0.0;
};

struct TileFileMetadata {
  std::string tile_id;
  std::string path;
  std::uintmax_t size_bytes = 0U;
  std::size_t rows = 0U;
  std::size_t cols = 0U;
  double min_east_m = 0.0;
  double max_east_m = 0.0;
  double min_north_m = 0.0;
  double max_north_m = 0.0;
};

struct LodOutputMetadata {
  int level = 0;
  int sample_stride = 1;
  std::vector<TileFileMetadata> tiles;
};

struct TerrainPackageOutputs {
  std::vector<LodOutputMetadata> render_lods;
  std::vector<TileFileMetadata> collision_tiles;
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

bool is_finite(double value) noexcept {
  return std::isfinite(value);
}

bool nearly_equal(double lhs, double rhs, double tolerance = kCoordinateToleranceM) noexcept {
  return std::fabs(lhs - rhs) <= tolerance;
}

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
  std::ostringstream output;
  output << std::setprecision(17) << value;
  return output.str();
}

std::string render_json(const JsonValue& value) {
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
      for (std::size_t i = 0; i < value.array.size(); ++i) {
        if (i != 0U) {
          output += ",";
        }
        output += render_json(value.array[i]);
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
        output += render_json(child);
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

std::string lowercase_ascii(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const char ch : text) {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return lowered;
}

std::string sanitize_id(std::string_view text, std::string_view fallback) {
  std::string sanitized;
  bool previous_dash = false;
  for (const char ch : text) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0) {
      sanitized.push_back(static_cast<char>(std::tolower(uch)));
      previous_dash = false;
    } else if (!previous_dash && !sanitized.empty()) {
      sanitized.push_back('-');
      previous_dash = true;
    }
  }
  while (!sanitized.empty() && sanitized.back() == '-') {
    sanitized.pop_back();
  }
  if (sanitized.empty()) {
    return std::string{fallback};
  }
  return sanitized;
}

std::string normalized_relative_path_string(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

const JsonValue* find_member(const JsonValue::Object& object, std::string_view key) {
  const auto it = object.find(std::string{key});
  if (it == object.end()) {
    return nullptr;
  }
  return &it->second;
}

const JsonValue::Object* require_object(const JsonValue::Object& object,
                                        std::string_view key,
                                        ValidationReport& report,
                                        std::string_view code_prefix,
                                        std::string_view subject,
                                        std::string_view source_id = {}) {
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
                                      std::string_view subject) {
  const JsonValue* value = find_member(object, key);
  const std::string code = std::string{code_prefix} + "." + std::string{key};
  if (value == nullptr) {
    add_issue(report,
              "error",
              code + ".missing",
              std::string{subject} + " is missing required array '" + std::string{key} + "'.");
    return nullptr;
  }
  if (value->type != JsonValue::Type::array_value) {
    add_issue(report,
              "error",
              code + ".invalid_type",
              std::string{subject} + " field '" + std::string{key} + "' must be an array.");
    return nullptr;
  }
  return &value->array;
}

std::optional<std::string> require_string(const JsonValue::Object& object,
                                          std::string_view key,
                                          ValidationReport& report,
                                          std::string_view code_prefix,
                                          std::string_view subject,
                                          std::string_view source_id = {}) {
  const JsonValue* value = find_member(object, key);
  const std::string code = std::string{code_prefix} + "." + std::string{key};
  if (value == nullptr) {
    add_issue(report,
              "error",
              code + ".missing",
              std::string{subject} + " is missing required field '" + std::string{key} + "'.",
              std::string{source_id});
    return std::nullopt;
  }
  if (value->type != JsonValue::Type::string_value) {
    add_issue(report,
              "error",
              code + ".invalid_type",
              std::string{subject} + " field '" + std::string{key} + "' must be a string.",
              std::string{source_id});
    return std::nullopt;
  }
  if (value->string.empty()) {
    add_issue(report,
              "error",
              code + ".empty",
              std::string{subject} + " field '" + std::string{key} + "' must not be empty.",
              std::string{source_id});
    return std::nullopt;
  }
  return value->string;
}

std::optional<double> require_number(const JsonValue::Object& object,
                                     std::string_view key,
                                     ValidationReport& report,
                                     std::string_view code_prefix,
                                     std::string_view subject,
                                     std::string_view source_id = {}) {
  const JsonValue* value = find_member(object, key);
  const std::string code = std::string{code_prefix} + "." + std::string{key};
  if (value == nullptr) {
    add_issue(report,
              "error",
              code + ".missing",
              std::string{subject} + " is missing required field '" + std::string{key} + "'.",
              std::string{source_id});
    return std::nullopt;
  }
  if (value->type != JsonValue::Type::number_value || !is_finite(value->number)) {
    add_issue(report,
              "error",
              code + ".invalid_type",
              std::string{subject} + " field '" + std::string{key} +
                "' must be a finite number.",
              std::string{source_id});
    return std::nullopt;
  }
  return value->number;
}

double optional_number(const JsonValue::Object& object,
                       std::string_view key,
                       double default_value) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->type != JsonValue::Type::number_value ||
      !is_finite(value->number)) {
    return default_value;
  }
  return value->number;
}

std::optional<int> require_positive_integer(const JsonValue::Object& object,
                                            std::string_view key,
                                            ValidationReport& report,
                                            std::string_view code_prefix,
                                            std::string_view subject,
                                            std::string_view source_id = {}) {
  const std::optional<double> value =
    require_number(object, key, report, code_prefix, subject, source_id);
  if (!value.has_value()) {
    return std::nullopt;
  }
  const double rounded = std::round(*value);
  if (!nearly_equal(*value, rounded) || rounded < 1.0 ||
      rounded > static_cast<double>(std::numeric_limits<int>::max())) {
    add_issue(report,
              "error",
              std::string{code_prefix} + "." + std::string{key} + ".invalid_value",
              std::string{subject} + " field '" + std::string{key} +
                "' must be a positive integer.",
              std::string{source_id});
    return std::nullopt;
  }
  return static_cast<int>(rounded);
}

std::optional<int> require_non_negative_integer(const JsonValue::Object& object,
                                                std::string_view key,
                                                ValidationReport& report,
                                                std::string_view code_prefix,
                                                std::string_view subject,
                                                std::string_view source_id = {}) {
  const std::optional<double> value =
    require_number(object, key, report, code_prefix, subject, source_id);
  if (!value.has_value()) {
    return std::nullopt;
  }
  const double rounded = std::round(*value);
  if (!nearly_equal(*value, rounded) || rounded < 0.0 ||
      rounded > static_cast<double>(std::numeric_limits<int>::max())) {
    add_issue(report,
              "error",
              std::string{code_prefix} + "." + std::string{key} + ".invalid_value",
              std::string{subject} + " field '" + std::string{key} +
                "' must be a non-negative integer.",
              std::string{source_id});
    return std::nullopt;
  }
  return static_cast<int>(rounded);
}

std::optional<TerrainConfig> parse_terrain_config(const JsonValue& root,
                                                  ValidationReport& report) {
  if (root.type != JsonValue::Type::object_value) {
    add_issue(report, "error", "terrain_config.invalid_type", "Terrain config root must be an object.");
    return std::nullopt;
  }

  const JsonValue::Object& object = root.object;
  TerrainConfig config;
  const std::optional<std::string> schema = require_string(
    object, "schemaVersion", report, "terrain_config", "Terrain config");
  if (schema.has_value()) {
    config.schema_version = *schema;
  }
  if (schema.has_value() && *schema != kTerrainConfigSchemaVersion &&
      *schema != kCzechTerrainConfigSchemaVersion) {
    add_issue(report,
              "error",
              "terrain_config.schemaVersion.unsupported",
              "Terrain config schemaVersion must be '" +
                std::string{kTerrainConfigSchemaVersion} + "' or '" +
                std::string{kCzechTerrainConfigSchemaVersion} + "'.");
  }
  if (schema.has_value() && *schema == kCzechTerrainConfigSchemaVersion) {
    config.coverage_scope = "czech-republic";
  }
  if (const JsonValue* coverage_scope = find_member(object, "coverageScope");
      coverage_scope != nullptr && coverage_scope->type == JsonValue::Type::string_value &&
      !coverage_scope->string.empty()) {
    config.coverage_scope = coverage_scope->string;
  }
  if (config.coverage_scope != "pilot-region" && config.coverage_scope != "czech-republic") {
    add_issue(report,
              "error",
              "terrain_config.coverageScope.unsupported",
              "Terrain config coverageScope must be 'pilot-region' or 'czech-republic'.");
  }
  if (schema.has_value() && *schema == kCzechTerrainConfigSchemaVersion &&
      config.coverage_scope != "czech-republic") {
    add_issue(report,
              "error",
              "terrain_config.coverageScope.schema_mismatch",
              "Czech Republic terrain config schema requires coverageScope 'czech-republic'.");
  }
  if (schema.has_value() && *schema == kTerrainConfigSchemaVersion &&
      config.coverage_scope == "czech-republic") {
    add_issue(report,
              "error",
              "terrain_config.coverageScope.schema_mismatch",
              "Czech Republic coverage requires the Czech Republic terrain config schema.");
  }
  if (const JsonValue* package_id_hint = find_member(object, "packageIdHint");
      package_id_hint != nullptr && package_id_hint->type == JsonValue::Type::string_value) {
    config.package_id_hint = package_id_hint->string;
  }

  const JsonValue::Object* pilot_region =
    require_object(object, "pilotRegion", report, "terrain_config", "Terrain config");
  if (pilot_region != nullptr) {
    if (const auto value =
          require_string(*pilot_region, "id", report, "terrain_config.pilotRegion", "Pilot region")) {
      config.pilot_region.id = *value;
    }
    if (const auto value = require_number(
          *pilot_region, "minEastM", report, "terrain_config.pilotRegion", "Pilot region")) {
      config.pilot_region.min_east_m = *value;
    }
    if (const auto value = require_number(
          *pilot_region, "minNorthM", report, "terrain_config.pilotRegion", "Pilot region")) {
      config.pilot_region.min_north_m = *value;
    }
    if (const auto value = require_number(
          *pilot_region, "widthM", report, "terrain_config.pilotRegion", "Pilot region")) {
      config.pilot_region.width_m = *value;
    }
    if (const auto value = require_number(
          *pilot_region, "heightM", report, "terrain_config.pilotRegion", "Pilot region")) {
      config.pilot_region.height_m = *value;
    }
    if (config.coverage_scope != "czech-republic" &&
        (!nearly_equal(config.pilot_region.width_m, kPilotRegionWidthM, 1.0e-3) ||
         !nearly_equal(config.pilot_region.height_m, kPilotRegionHeightM, 1.0e-3))) {
      add_issue(report,
                "error",
                "terrain_config.pilotRegion.size.invalid",
                "DMR 5G pilot terrain processing requires a declared 50 x 50 km region.");
    }
    if (config.coverage_scope == "czech-republic" &&
        (config.pilot_region.width_m <= kPilotRegionWidthM ||
         config.pilot_region.height_m <= kPilotRegionHeightM)) {
      add_issue(report,
                "error",
                "terrain_config.czechRepublic.extent.invalid",
                "Full Czech Republic terrain processing requires an extent larger than the 50 x 50 km pilot region.");
    }
  }

  const JsonValue* transform_value = find_member(object, "transform");
  const JsonValue::Object* transform =
    require_object(object, "transform", report, "terrain_config", "Terrain config");
  if (transform != nullptr && transform_value != nullptr) {
    config.transform_json = render_json(*transform_value);
    const JsonValue::Object* source_crs =
      require_object(*transform, "sourceCrs", report, "terrain_config.transform", "Transform");
    if (source_crs != nullptr) {
      (void)require_string(
        *source_crs, "authority", report, "terrain_config.transform.sourceCrs", "Source CRS");
      (void)require_string(
        *source_crs, "code", report, "terrain_config.transform.sourceCrs", "Source CRS");
    }
    const JsonValue::Object* target_crs =
      require_object(*transform, "targetCrs", report, "terrain_config.transform", "Transform");
    if (target_crs != nullptr) {
      (void)require_string(
        *target_crs, "authority", report, "terrain_config.transform.targetCrs", "Target CRS");
      (void)require_string(
        *target_crs, "code", report, "terrain_config.transform.targetCrs", "Target CRS");
    }
    const JsonValue::Object* source_height_system = require_object(
      *transform, "sourceHeightSystem", report, "terrain_config.transform", "Transform");
    if (source_height_system != nullptr) {
      (void)require_string(*source_height_system,
                           "name",
                           report,
                           "terrain_config.transform.sourceHeightSystem",
                           "Source height system");
      (void)require_string(*source_height_system,
                           "kind",
                           report,
                           "terrain_config.transform.sourceHeightSystem",
                           "Source height system");
    }
    const JsonValue::Object* target_height_system = require_object(
      *transform, "targetHeightSystem", report, "terrain_config.transform", "Transform");
    if (target_height_system != nullptr) {
      (void)require_string(*target_height_system,
                           "name",
                           report,
                           "terrain_config.transform.targetHeightSystem",
                           "Target height system");
      (void)require_string(*target_height_system,
                           "kind",
                           report,
                           "terrain_config.transform.targetHeightSystem",
                           "Target height system");
    }

    const JsonValue::Object* proj =
      require_object(*transform, "proj", report, "terrain_config.transform", "Transform");
    if (proj != nullptr) {
      (void)require_string(*proj, "version", report, "terrain_config.transform.proj", "PROJ config");
      (void)require_string(
        *proj, "pipeline", report, "terrain_config.transform.proj", "PROJ config");
    }

    const JsonValue::Object* geoid =
      require_object(*transform, "geoid", report, "terrain_config.transform", "Transform");
    if (geoid != nullptr) {
      (void)require_string(
        *geoid, "model", report, "terrain_config.transform.geoid", "Geoid config");
      (void)require_string(
        *geoid, "grid", report, "terrain_config.transform.geoid", "Geoid config");
      if (const auto value = require_number(
            *geoid, "undulationMeters", report, "terrain_config.transform.geoid", "Geoid config")) {
        config.transform.geoid_undulation_m = *value;
      }
    }

    const JsonValue::Object* source_to_project = require_object(
      *transform, "sourceToProject", report, "terrain_config.transform", "Transform");
    if (source_to_project != nullptr) {
      const double east_from_source_y_scale =
        optional_number(*source_to_project, "eastFromSourceYScale", 0.0);
      const double north_from_source_x_scale =
        optional_number(*source_to_project, "northFromSourceXScale", 0.0);
      if (!nearly_equal(east_from_source_y_scale, 0.0) ||
          !nearly_equal(north_from_source_x_scale, 0.0)) {
        add_issue(report,
                  "error",
                  "terrain_config.transform.sourceToProject.rotation_unsupported",
                  "DMR 5G pilot terrain packaging supports axis-aligned source-to-project grids.");
      }
      if (const auto value = require_number(*source_to_project,
                                            "eastFromSourceXScale",
                                            report,
                                            "terrain_config.transform.sourceToProject",
                                            "Source-to-project transform")) {
        config.transform.east_from_source_x_scale = *value;
      }
      if (const auto value = require_number(*source_to_project,
                                            "eastOffsetM",
                                            report,
                                            "terrain_config.transform.sourceToProject",
                                            "Source-to-project transform")) {
        config.transform.east_offset_m = *value;
      }
      if (const auto value = require_number(*source_to_project,
                                            "northFromSourceYScale",
                                            report,
                                            "terrain_config.transform.sourceToProject",
                                            "Source-to-project transform")) {
        config.transform.north_from_source_y_scale = *value;
      }
      if (const auto value = require_number(*source_to_project,
                                            "northOffsetM",
                                            report,
                                            "terrain_config.transform.sourceToProject",
                                            "Source-to-project transform")) {
        config.transform.north_offset_m = *value;
      }
      if (nearly_equal(config.transform.east_from_source_x_scale, 0.0) ||
          nearly_equal(config.transform.north_from_source_y_scale, 0.0)) {
        add_issue(report,
                  "error",
                  "terrain_config.transform.sourceToProject.scale.invalid",
                  "Source-to-project east and north scales must be non-zero.");
      }
    }

    const JsonValue::Object* height_transform = require_object(
      *transform, "heightTransform", report, "terrain_config.transform", "Transform");
    if (height_transform != nullptr) {
      if (const auto value = require_number(*height_transform,
                                            "sourceHeightScale",
                                            report,
                                            "terrain_config.transform.heightTransform",
                                            "Height transform")) {
        config.transform.height_scale = *value;
      }
      if (const auto value = require_number(*height_transform,
                                            "orthometricOffsetM",
                                            report,
                                            "terrain_config.transform.heightTransform",
                                            "Height transform")) {
        config.transform.orthometric_offset_m = *value;
      }
    }
  }

  const JsonValue::Array* source_tiles =
    require_array(object, "sourceTiles", report, "terrain_config", "Terrain config");
  if (source_tiles != nullptr) {
    if (source_tiles->empty()) {
      add_issue(report,
                "error",
                "terrain_config.sourceTiles.empty",
                "Terrain config must declare at least one DMR 5G source tile.");
    }
    std::set<std::string> tile_ids;
    for (std::size_t i = 0; i < source_tiles->size(); ++i) {
      const JsonValue& value = (*source_tiles)[i];
      if (value.type != JsonValue::Type::object_value) {
        add_issue(report,
                  "error",
                  "terrain_config.sourceTiles.entry.invalid_type",
                  "DMR 5G source tile entries must be objects.");
        continue;
      }
      const JsonValue::Object& tile_object = value.object;
      SourceTileConfig tile;
      const std::string fallback_id = "sourceTiles[" + std::to_string(i) + "]";
      if (const auto id =
            require_string(tile_object, "id", report, "terrain_config.sourceTiles", "Source tile")) {
        tile.id = *id;
      }
      const std::string issue_source_id = tile.id.empty() ? fallback_id : tile.id;
      if (const auto path = require_string(tile_object,
                                           "path",
                                           report,
                                           "terrain_config.sourceTiles",
                                           "Source tile",
                                           issue_source_id)) {
        tile.path = *path;
      }
      if (const auto format = require_string(tile_object,
                                             "format",
                                             report,
                                             "terrain_config.sourceTiles",
                                             "Source tile",
                                             issue_source_id)) {
        tile.format = lowercase_ascii(*format);
      }
      if (const auto error_m = require_number(tile_object,
                                              "declaredVerticalErrorM",
                                              report,
                                              "terrain_config.sourceTiles",
                                              "Source tile",
                                              issue_source_id)) {
        tile.declared_vertical_error_m = *error_m;
      }
      if (!tile.path.empty() && tile.path.is_absolute()) {
        add_issue(report,
                  "error",
                  "terrain_config.sourceTiles.path.absolute",
                  "DMR 5G source tile path must be relative to the source root.",
                  issue_source_id);
      }
      if (tile.format != "xyz" && tile.format != "arc-ascii-grid") {
        add_issue(report,
                  "error",
                  "terrain_config.sourceTiles.format.unsupported",
                  "DMR 5G source tile format must be 'xyz' or 'arc-ascii-grid'.",
                  issue_source_id);
      }
      if (tile.declared_vertical_error_m < 0.0) {
        add_issue(report,
                  "error",
                  "terrain_config.sourceTiles.declaredVerticalErrorM.invalid",
                  "Declared source vertical error must be non-negative.",
                  issue_source_id);
      }
      if (!tile.id.empty() && !tile_ids.insert(tile.id).second) {
        add_issue(report,
                  "error",
                  "terrain_config.sourceTiles.id.duplicate",
                  "DMR 5G source tile ids must be unique.",
                  tile.id);
      }
      config.source_tiles.push_back(std::move(tile));
    }
  }

  const JsonValue::Array* render_lods =
    require_array(object, "renderLods", report, "terrain_config", "Terrain config");
  if (render_lods != nullptr) {
    if (render_lods->empty()) {
      add_issue(report,
                "error",
                "terrain_config.renderLods.empty",
                "Terrain config must declare at least one render LOD.");
    }
    std::set<int> levels;
    for (std::size_t i = 0; i < render_lods->size(); ++i) {
      const JsonValue& value = (*render_lods)[i];
      if (value.type != JsonValue::Type::object_value) {
        add_issue(report,
                  "error",
                  "terrain_config.renderLods.entry.invalid_type",
                  "Render LOD entries must be objects.");
        continue;
      }
      LodConfig lod;
      if (const auto level = require_non_negative_integer(value.object,
                                                          "level",
                                                          report,
                                                          "terrain_config.renderLods",
                                                          "Render LOD")) {
        lod.level = *level;
      }
      if (const auto stride = require_positive_integer(value.object,
                                                       "sampleStride",
                                                       report,
                                                       "terrain_config.renderLods",
                                                       "Render LOD")) {
        lod.sample_stride = *stride;
      }
      if (!levels.insert(lod.level).second) {
        add_issue(report,
                  "error",
                  "terrain_config.renderLods.level.duplicate",
                  "Render LOD levels must be unique.");
      }
      config.render_lods.push_back(lod);
    }
    std::sort(config.render_lods.begin(), config.render_lods.end(), [](LodConfig lhs,
                                                                        LodConfig rhs) {
      return lhs.level < rhs.level;
    });
  }

  const JsonValue::Object* active_zone =
    require_object(object, "activeAircraftZone", report, "terrain_config", "Terrain config");
  if (active_zone != nullptr) {
    if (const auto value = require_number(*active_zone,
                                          "minEastM",
                                          report,
                                          "terrain_config.activeAircraftZone",
                                          "Active aircraft zone")) {
      config.active_aircraft_zone.min_east_m = *value;
    }
    if (const auto value = require_number(*active_zone,
                                          "maxEastM",
                                          report,
                                          "terrain_config.activeAircraftZone",
                                          "Active aircraft zone")) {
      config.active_aircraft_zone.max_east_m = *value;
    }
    if (const auto value = require_number(*active_zone,
                                          "minNorthM",
                                          report,
                                          "terrain_config.activeAircraftZone",
                                          "Active aircraft zone")) {
      config.active_aircraft_zone.min_north_m = *value;
    }
    if (const auto value = require_number(*active_zone,
                                          "maxNorthM",
                                          report,
                                          "terrain_config.activeAircraftZone",
                                          "Active aircraft zone")) {
      config.active_aircraft_zone.max_north_m = *value;
    }
    if (const JsonValue* stride = find_member(*active_zone, "sampleStride");
        stride != nullptr && stride->type == JsonValue::Type::number_value) {
      const double rounded = std::round(stride->number);
      if (nearly_equal(stride->number, rounded) && rounded >= 1.0 &&
          rounded <= static_cast<double>(std::numeric_limits<int>::max())) {
        config.active_aircraft_zone.sample_stride = static_cast<int>(rounded);
      } else {
        add_issue(report,
                  "error",
                  "terrain_config.activeAircraftZone.sampleStride.invalid",
                  "Active aircraft zone sampleStride must be a positive integer.");
      }
    }
    if (config.active_aircraft_zone.min_east_m > config.active_aircraft_zone.max_east_m ||
        config.active_aircraft_zone.min_north_m > config.active_aircraft_zone.max_north_m) {
      add_issue(report,
                "error",
                "terrain_config.activeAircraftZone.bounds.invalid",
                "Active aircraft zone bounds must be ordered.");
    }
  }

  const JsonValue::Array* control_points =
    require_array(object, "controlPoints", report, "terrain_config", "Terrain config");
  if (control_points != nullptr) {
    if (control_points->empty()) {
      add_issue(report,
                "error",
                "terrain_config.controlPoints.empty",
                "Terrain config must declare at least one control point.");
    }
    for (std::size_t i = 0; i < control_points->size(); ++i) {
      const JsonValue& value = (*control_points)[i];
      if (value.type != JsonValue::Type::object_value) {
        add_issue(report,
                  "error",
                  "terrain_config.controlPoints.entry.invalid_type",
                  "Control point entries must be objects.");
        continue;
      }
      ControlPoint point;
      const std::string fallback_id = "controlPoints[" + std::to_string(i) + "]";
      if (const auto id =
            require_string(value.object, "id", report, "terrain_config.controlPoints", "Control point")) {
        point.id = *id;
      }
      const std::string issue_source_id = point.id.empty() ? fallback_id : point.id;
      if (const auto source_x = require_number(value.object,
                                               "sourceX",
                                               report,
                                               "terrain_config.controlPoints",
                                               "Control point",
                                               issue_source_id)) {
        point.source_x_m = *source_x;
      }
      if (const auto source_y = require_number(value.object,
                                               "sourceY",
                                               report,
                                               "terrain_config.controlPoints",
                                               "Control point",
                                               issue_source_id)) {
        point.source_y_m = *source_y;
      }
      if (const auto expected_height = require_number(value.object,
                                                      "expectedOrthometricHeightM",
                                                      report,
                                                      "terrain_config.controlPoints",
                                                      "Control point",
                                                      issue_source_id)) {
        point.expected_orthometric_height_m = *expected_height;
      }
      if (const auto source_error = require_number(value.object,
                                                   "sourceVerticalErrorM",
                                                   report,
                                                   "terrain_config.controlPoints",
                                                   "Control point",
                                                   issue_source_id)) {
        point.source_vertical_error_m = *source_error;
      }
      if (point.source_vertical_error_m < 0.0) {
        add_issue(report,
                  "error",
                  "terrain_config.controlPoints.sourceVerticalErrorM.invalid",
                  "Control point declared source vertical error must be non-negative.",
                  issue_source_id);
      }
      config.control_points.push_back(point);
    }
  }

  config.edge_tolerance_m = optional_number(object, "edgeToleranceM", config.edge_tolerance_m);
  config.boundary_clean_max_adjustment_m =
    optional_number(object, "boundaryCleanMaxAdjustmentM", config.boundary_clean_max_adjustment_m);
  if (config.edge_tolerance_m < 0.0 || config.boundary_clean_max_adjustment_m < 0.0) {
    add_issue(report,
              "error",
              "terrain_config.tolerances.invalid",
              "Edge and boundary-clean tolerances must be non-negative.");
  }

  if (has_errors(report)) {
    return std::nullopt;
  }
  return config;
}

std::optional<TerrainConfig> load_terrain_config(const std::filesystem::path& path,
                                                 ValidationReport& report) {
  try {
    return parse_terrain_config(JsonParser{read_text_file(path)}.parse(), report);
  } catch (const std::exception& error) {
    add_issue(report, "error", "terrain_config.load_failed", error.what());
    return std::nullopt;
  }
}

void bind_source_tiles_to_manifest(TerrainConfig& config,
                                   const SourceManifest& source_manifest,
                                   ValidationReport& report) {
  std::map<std::string, const SourceDataset*> sources_by_path;
  for (const SourceDataset& source : source_manifest.sources) {
    const std::string normalized_path = normalized_relative_path_string(source.checksum.path);
    const auto insertion = sources_by_path.emplace(normalized_path, &source);
    if (!insertion.second) {
      add_issue(report,
                "error",
                "terrain.source_manifest.path.duplicate",
                "Validated source manifest contains duplicate checksum paths for terrain inputs.",
                source.id);
    }
  }

  for (SourceTileConfig& tile : config.source_tiles) {
    const std::string normalized_path = normalized_relative_path_string(tile.path);
    const auto source = sources_by_path.find(normalized_path);
    if (source == sources_by_path.end()) {
      add_issue(report,
                "error",
                "terrain.source_manifest.path_missing",
                "DMR 5G terrain source tile path is not declared in the validated source manifest.",
                tile.id);
      continue;
    }
    tile.source_manifest_id = source->second->id;
  }
}

std::vector<const SourceDataset*> terrain_lineage_sources(const SourceManifest& source_manifest,
                                                         const TerrainConfig& config) {
  std::vector<const SourceDataset*> sources;
  std::set<std::string> emitted_source_ids;
  for (const SourceTileConfig& tile : config.source_tiles) {
    if (tile.source_manifest_id.empty() ||
        !emitted_source_ids.insert(tile.source_manifest_id).second) {
      continue;
    }
    const auto source = std::find_if(source_manifest.sources.begin(),
                                     source_manifest.sources.end(),
                                     [&tile](const SourceDataset& candidate) {
                                       return candidate.id == tile.source_manifest_id;
                                     });
    if (source != source_manifest.sources.end()) {
      sources.push_back(&*source);
    }
  }
  return sources;
}

bool line_is_ignorable(std::string_view line) {
  for (const char ch : line) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
    return ch == '#';
  }
  return true;
}

std::vector<double> sorted_unique_values(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end(), [](double lhs, double rhs) {
                 return nearly_equal(lhs, rhs);
               }),
               values.end());
  return values;
}

std::optional<std::size_t> find_coordinate_index(const std::vector<double>& values,
                                                 double coordinate_m) {
  const auto lower = std::lower_bound(values.begin(), values.end(), coordinate_m);
  if (lower != values.end() && nearly_equal(*lower, coordinate_m)) {
    return static_cast<std::size_t>(std::distance(values.begin(), lower));
  }
  if (lower != values.begin()) {
    const auto previous = std::prev(lower);
    if (nearly_equal(*previous, coordinate_m)) {
      return static_cast<std::size_t>(std::distance(values.begin(), previous));
    }
  }
  return std::nullopt;
}

void push_projected_sample(std::vector<std::tuple<double, double, double, double>>& samples,
                           std::vector<double>& east_values,
                           std::vector<double>& north_values,
                           const AxisAlignedTransform& transform,
                           double source_x_m,
                           double source_y_m,
                           double source_height_m) {
  const double east_m = transform.east_m(source_x_m);
  const double north_m = transform.north_m(source_y_m);
  const double orthometric_height_m = transform.orthometric_height_m(source_height_m);
  const double ellipsoidal_height_m = transform.ellipsoidal_height_m(orthometric_height_m);
  samples.emplace_back(east_m, north_m, orthometric_height_m, ellipsoidal_height_m);
  east_values.push_back(east_m);
  north_values.push_back(north_m);
}

std::optional<TerrainTile> build_tile_from_samples(
  const SourceTileConfig& tile_config,
  const std::filesystem::path& source_path,
  std::vector<std::tuple<double, double, double, double>> samples,
  std::vector<double> east_values,
  std::vector<double> north_values,
  ValidationReport& report) {
  if (samples.empty()) {
    add_issue(report,
              "error",
              "terrain.dmr5g.tile.empty",
              "DMR 5G source tile contains no terrain samples.",
              tile_config.id);
    return std::nullopt;
  }

  TerrainTile tile;
  tile.id = tile_config.id;
  tile.source_manifest_id = tile_config.source_manifest_id;
  tile.source_path = source_path;
  tile.declared_vertical_error_m = tile_config.declared_vertical_error_m;
  tile.east_values = sorted_unique_values(std::move(east_values));
  tile.north_values = sorted_unique_values(std::move(north_values));
  tile.cols = tile.east_values.size();
  tile.rows = tile.north_values.size();

  if (tile.rows < 2U || tile.cols < 2U) {
    add_issue(report,
              "error",
              "terrain.dmr5g.tile.grid_too_small",
              "DMR 5G source tile must resolve to a grid with at least 2 rows and 2 columns.",
              tile.id);
    return std::nullopt;
  }

  const std::size_t sample_count = tile.rows * tile.cols;
  tile.orthometric_heights.assign(sample_count, std::numeric_limits<double>::quiet_NaN());
  tile.ellipsoidal_heights.assign(sample_count, std::numeric_limits<double>::quiet_NaN());
  std::vector<bool> assigned(sample_count, false);

  for (const auto& [east_m, north_m, orthometric_height_m, ellipsoidal_height_m] : samples) {
    const std::optional<std::size_t> col = find_coordinate_index(tile.east_values, east_m);
    const std::optional<std::size_t> row = find_coordinate_index(tile.north_values, north_m);
    if (!col.has_value() || !row.has_value()) {
      add_issue(report,
                "error",
                "terrain.dmr5g.tile.coordinate_index_failed",
                "Projected DMR 5G sample could not be mapped back into the tile grid.",
                tile.id);
      return std::nullopt;
    }
    const std::size_t index = tile.index(*row, *col);
    if (assigned[index]) {
      add_issue(report,
                "error",
                "terrain.dmr5g.tile.duplicate_sample",
                "DMR 5G source tile contains duplicate projected samples.",
                tile.id);
      return std::nullopt;
    }
    assigned[index] = true;
    tile.orthometric_heights[index] = orthometric_height_m;
    tile.ellipsoidal_heights[index] = ellipsoidal_height_m;
  }

  if (std::any_of(assigned.begin(), assigned.end(), [](bool value) { return !value; })) {
    add_issue(report,
              "error",
              "terrain.dmr5g.tile.incomplete_grid",
              "DMR 5G source tile samples must form a complete regular grid after projection.",
              tile.id);
    return std::nullopt;
  }

  return tile;
}

std::optional<TerrainTile> load_xyz_tile(const SourceTileConfig& tile_config,
                                         const std::filesystem::path& source_path,
                                         const AxisAlignedTransform& transform,
                                         ValidationReport& report) {
  std::ifstream input(source_path, std::ios::binary);
  if (!input) {
    add_issue(report,
              "error",
              "terrain.dmr5g.tile.open_failed",
              "Failed to open DMR 5G XYZ tile: " + source_path.string(),
              tile_config.id);
    return std::nullopt;
  }

  std::vector<std::tuple<double, double, double, double>> samples;
  std::vector<double> east_values;
  std::vector<double> north_values;
  std::string line;
  std::size_t line_number = 0U;
  while (std::getline(input, line)) {
    ++line_number;
    if (line_is_ignorable(line)) {
      continue;
    }
    std::istringstream row{line};
    double source_x_m = 0.0;
    double source_y_m = 0.0;
    double source_height_m = 0.0;
    if (!(row >> source_x_m >> source_y_m >> source_height_m)) {
      add_issue(report,
                "error",
                "terrain.dmr5g.tile.xyz.invalid_row",
                "Invalid XYZ row in " + source_path.string() + ":" + std::to_string(line_number),
                tile_config.id);
      return std::nullopt;
    }
    push_projected_sample(
      samples, east_values, north_values, transform, source_x_m, source_y_m, source_height_m);
  }

  return build_tile_from_samples(
    tile_config, source_path, std::move(samples), std::move(east_values), std::move(north_values), report);
}

std::optional<TerrainTile> load_arc_ascii_grid_tile(const SourceTileConfig& tile_config,
                                                    const std::filesystem::path& source_path,
                                                    const AxisAlignedTransform& transform,
                                                    ValidationReport& report) {
  std::ifstream input(source_path, std::ios::binary);
  if (!input) {
    add_issue(report,
              "error",
              "terrain.dmr5g.tile.open_failed",
              "Failed to open DMR 5G Arc ASCII grid tile: " + source_path.string(),
              tile_config.id);
    return std::nullopt;
  }

  std::map<std::string, double> header;
  for (int i = 0; i < 6; ++i) {
    std::string key;
    double value = 0.0;
    if (!(input >> key >> value)) {
      add_issue(report,
                "error",
                "terrain.dmr5g.tile.arc_ascii.header_invalid",
                "Arc ASCII grid tile has an incomplete header.",
                tile_config.id);
      return std::nullopt;
    }
    header.emplace(lowercase_ascii(key), value);
  }

  const auto header_number = [&header](std::string_view key) -> std::optional<double> {
    const auto it = header.find(std::string{key});
    if (it == header.end()) {
      return std::nullopt;
    }
    return it->second;
  };

  const std::optional<double> ncols_value = header_number("ncols");
  const std::optional<double> nrows_value = header_number("nrows");
  const std::optional<double> cellsize = header_number("cellsize");
  const std::optional<double> xllcorner = header_number("xllcorner");
  const std::optional<double> xllcenter = header_number("xllcenter");
  const std::optional<double> yllcorner = header_number("yllcorner");
  const std::optional<double> yllcenter = header_number("yllcenter");
  const double nodata = header_number("nodata_value").value_or(std::numeric_limits<double>::quiet_NaN());

  if (!ncols_value.has_value() || !nrows_value.has_value() || !cellsize.has_value() ||
      (!xllcorner.has_value() && !xllcenter.has_value()) ||
      (!yllcorner.has_value() && !yllcenter.has_value())) {
    add_issue(report,
              "error",
              "terrain.dmr5g.tile.arc_ascii.header_missing",
              "Arc ASCII grid tile is missing ncols, nrows, cellsize or lower-left coordinates.",
              tile_config.id);
    return std::nullopt;
  }

  const auto to_size = [](double value) -> std::optional<std::size_t> {
    const double rounded = std::round(value);
    if (!nearly_equal(value, rounded) || rounded < 1.0 ||
        rounded > static_cast<double>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(rounded);
  };
  const std::optional<std::size_t> ncols = to_size(*ncols_value);
  const std::optional<std::size_t> nrows = to_size(*nrows_value);
  if (!ncols.has_value() || !nrows.has_value()) {
    add_issue(report,
              "error",
              "terrain.dmr5g.tile.arc_ascii.shape_invalid",
              "Arc ASCII grid ncols and nrows must be positive integers.",
              tile_config.id);
    return std::nullopt;
  }

  const double lower_left_center_x_m =
    xllcenter.value_or(*xllcorner + (*cellsize * 0.5));
  const double lower_left_center_y_m =
    yllcenter.value_or(*yllcorner + (*cellsize * 0.5));

  std::vector<std::tuple<double, double, double, double>> samples;
  std::vector<double> east_values;
  std::vector<double> north_values;
  samples.reserve(*ncols * *nrows);
  east_values.reserve(*ncols * *nrows);
  north_values.reserve(*ncols * *nrows);

  for (std::size_t input_row = 0U; input_row < *nrows; ++input_row) {
    const std::size_t source_row_from_south = (*nrows - 1U) - input_row;
    for (std::size_t col = 0U; col < *ncols; ++col) {
      double source_height_m = 0.0;
      if (!(input >> source_height_m)) {
        add_issue(report,
                  "error",
                  "terrain.dmr5g.tile.arc_ascii.values_missing",
                  "Arc ASCII grid tile has fewer height samples than declared.",
                  tile_config.id);
        return std::nullopt;
      }
      if (is_finite(nodata) && nearly_equal(source_height_m, nodata)) {
        add_issue(report,
                  "error",
                  "terrain.dmr5g.tile.arc_ascii.nodata",
                  "DMR 5G pilot terrain tiles must not contain NODATA samples.",
                  tile_config.id);
        return std::nullopt;
      }
      const double source_x_m = lower_left_center_x_m + static_cast<double>(col) * *cellsize;
      const double source_y_m =
        lower_left_center_y_m + static_cast<double>(source_row_from_south) * *cellsize;
      push_projected_sample(
        samples, east_values, north_values, transform, source_x_m, source_y_m, source_height_m);
    }
  }

  return build_tile_from_samples(
    tile_config, source_path, std::move(samples), std::move(east_values), std::move(north_values), report);
}

std::vector<TerrainTile> load_tiles(const TerrainConfig& config,
                                    const std::filesystem::path& source_root,
                                    ValidationReport& report) {
  std::vector<TerrainTile> tiles;
  for (const SourceTileConfig& tile_config : config.source_tiles) {
    const std::filesystem::path source_path = source_root / tile_config.path;
    std::optional<TerrainTile> tile;
    if (tile_config.format == "xyz") {
      tile = load_xyz_tile(tile_config, source_path, config.transform, report);
    } else if (tile_config.format == "arc-ascii-grid") {
      tile = load_arc_ascii_grid_tile(tile_config, source_path, config.transform, report);
    }
    if (tile.has_value()) {
      tiles.push_back(std::move(*tile));
    }
  }
  return tiles;
}

void validate_czech_republic_terrain_coverage(const TerrainConfig& config,
                                              const std::vector<TerrainTile>& tiles,
                                              ValidationReport& report) {
  if (config.coverage_scope != "czech-republic" || tiles.empty()) {
    return;
  }

  std::vector<Bounds> tile_bounds;
  tile_bounds.reserve(tiles.size());
  for (const TerrainTile& tile : tiles) {
    tile_bounds.push_back({
      tile.min_east_m(),
      tile.max_east_m(),
      tile.min_north_m(),
      tile.max_north_m(),
    });
  }

  const Bounds required_bounds{
    config.pilot_region.min_east_m,
    config.pilot_region.max_east_m(),
    config.pilot_region.min_north_m,
    config.pilot_region.max_north_m(),
  };
  if (!continuous_rect_coverage(tile_bounds, required_bounds)) {
    add_issue(report,
              "error",
              "terrain_config.czechRepublic.sourceTiles.coverage_incomplete",
              "Full Czech Republic terrain processing requires continuous DMR source tile coverage across the declared package bounds.");
  }
}

bool interval_overlaps(double lhs_min, double lhs_max, double rhs_min, double rhs_max) noexcept {
  return lhs_min <= rhs_max + kCoordinateToleranceM && rhs_min <= lhs_max + kCoordinateToleranceM;
}

bool coordinate_in_interval(double value, double min_value, double max_value) noexcept {
  return value >= min_value - kCoordinateToleranceM && value <= max_value + kCoordinateToleranceM;
}

void average_boundary_samples(TerrainTile& lhs,
                              std::size_t lhs_row,
                              std::size_t lhs_col,
                              TerrainTile& rhs,
                              std::size_t rhs_row,
                              std::size_t rhs_col,
                              BoundaryCleanResult& result,
                              const TerrainConfig& config,
                              ValidationReport& report) {
  const std::size_t lhs_index = lhs.index(lhs_row, lhs_col);
  const std::size_t rhs_index = rhs.index(rhs_row, rhs_col);
  const double pre_step =
    std::fabs(lhs.ellipsoidal_heights[lhs_index] - rhs.ellipsoidal_heights[rhs_index]);
  result.compared_sample_count += 1U;
  result.max_pre_clean_step_m = std::max(result.max_pre_clean_step_m, pre_step);
  if (pre_step > config.boundary_clean_max_adjustment_m) {
    add_issue(report,
              "error",
              "terrain.edge.boundary_step_exceeds_cleaning_tolerance",
              "Adjacent DMR 5G source tile boundary step exceeds the configured cleaning tolerance.",
              lhs.id + "/" + rhs.id);
  }
  if (pre_step > config.edge_tolerance_m) {
    result.adjusted_sample_count += 1U;
    result.max_adjustment_m = std::max(result.max_adjustment_m, pre_step * 0.5);
  }

  const double averaged_orthometric =
    (lhs.orthometric_heights[lhs_index] + rhs.orthometric_heights[rhs_index]) * 0.5;
  const double averaged_ellipsoidal =
    (lhs.ellipsoidal_heights[lhs_index] + rhs.ellipsoidal_heights[rhs_index]) * 0.5;
  lhs.orthometric_heights[lhs_index] = averaged_orthometric;
  rhs.orthometric_heights[rhs_index] = averaged_orthometric;
  lhs.ellipsoidal_heights[lhs_index] = averaged_ellipsoidal;
  rhs.ellipsoidal_heights[rhs_index] = averaged_ellipsoidal;
}

void average_vertical_edge(TerrainTile& west,
                           TerrainTile& east,
                           BoundaryCleanResult& result,
                           const TerrainConfig& config,
                           ValidationReport& report) {
  if (!interval_overlaps(
        west.min_north_m(), west.max_north_m(), east.min_north_m(), east.max_north_m())) {
    return;
  }
  const std::size_t west_col = west.cols - 1U;
  const std::size_t east_col = 0U;
  for (std::size_t west_row = 0U; west_row < west.rows; ++west_row) {
    const std::optional<std::size_t> east_row =
      find_coordinate_index(east.north_values, west.north_values[west_row]);
    if (!east_row.has_value()) {
      continue;
    }
    average_boundary_samples(
      west, west_row, west_col, east, *east_row, east_col, result, config, report);
  }
}

void average_horizontal_edge(TerrainTile& south,
                             TerrainTile& north,
                             BoundaryCleanResult& result,
                             const TerrainConfig& config,
                             ValidationReport& report) {
  if (!interval_overlaps(
        south.min_east_m(), south.max_east_m(), north.min_east_m(), north.max_east_m())) {
    return;
  }
  const std::size_t south_row = south.rows - 1U;
  const std::size_t north_row = 0U;
  for (std::size_t south_col = 0U; south_col < south.cols; ++south_col) {
    const std::optional<std::size_t> north_col =
      find_coordinate_index(north.east_values, south.east_values[south_col]);
    if (!north_col.has_value()) {
      continue;
    }
    average_boundary_samples(
      south, south_row, south_col, north, north_row, *north_col, result, config, report);
  }
}

BoundaryCleanResult clean_tile_boundaries(std::vector<TerrainTile>& tiles,
                                          const TerrainConfig& config,
                                          ValidationReport& report) {
  BoundaryCleanResult result;
  for (std::size_t i = 0U; i < tiles.size(); ++i) {
    for (std::size_t j = i + 1U; j < tiles.size(); ++j) {
      TerrainTile& lhs = tiles[i];
      TerrainTile& rhs = tiles[j];
      if (nearly_equal(lhs.max_east_m(), rhs.min_east_m())) {
        average_vertical_edge(lhs, rhs, result, config, report);
      } else if (nearly_equal(rhs.max_east_m(), lhs.min_east_m())) {
        average_vertical_edge(rhs, lhs, result, config, report);
      }
      if (nearly_equal(lhs.max_north_m(), rhs.min_north_m())) {
        average_horizontal_edge(lhs, rhs, result, config, report);
      } else if (nearly_equal(rhs.max_north_m(), lhs.min_north_m())) {
        average_horizontal_edge(rhs, lhs, result, config, report);
      }
    }
  }
  return result;
}

TerrainNormal normalized_normal(double slope_east, double slope_north) {
  const double east = -slope_east;
  const double north = -slope_north;
  const double up = 1.0;
  const double length = std::sqrt(east * east + north * north + up * up);
  if (length <= 0.0 || !is_finite(length)) {
    return {};
  }
  return {east / length, north / length, up / length};
}

void compute_tile_normals(TerrainTile& tile) {
  tile.normals.assign(tile.rows * tile.cols, {});
  for (std::size_t row = 0U; row < tile.rows; ++row) {
    const std::size_t row_south = row == 0U ? row : row - 1U;
    const std::size_t row_north = row + 1U == tile.rows ? row : row + 1U;
    const double north_delta = tile.north_values[row_north] - tile.north_values[row_south];
    for (std::size_t col = 0U; col < tile.cols; ++col) {
      const std::size_t col_west = col == 0U ? col : col - 1U;
      const std::size_t col_east = col + 1U == tile.cols ? col : col + 1U;
      const double east_delta = tile.east_values[col_east] - tile.east_values[col_west];
      const double slope_east =
        east_delta == 0.0
          ? 0.0
          : (tile.ellipsoidal_heights[tile.index(row, col_east)] -
             tile.ellipsoidal_heights[tile.index(row, col_west)]) /
              east_delta;
      const double slope_north =
        north_delta == 0.0
          ? 0.0
          : (tile.ellipsoidal_heights[tile.index(row_north, col)] -
             tile.ellipsoidal_heights[tile.index(row_south, col)]) /
              north_delta;
      tile.normals[tile.index(row, col)] = normalized_normal(slope_east, slope_north);
    }
  }
}

void compute_normals(std::vector<TerrainTile>& tiles) {
  for (TerrainTile& tile : tiles) {
    compute_tile_normals(tile);
  }
}

std::vector<std::size_t> sampled_indices(std::size_t count, int stride) {
  std::vector<std::size_t> indices;
  if (count == 0U) {
    return indices;
  }
  const std::size_t effective_stride = static_cast<std::size_t>(std::max(1, stride));
  for (std::size_t i = 0U; i < count; i += effective_stride) {
    indices.push_back(i);
  }
  if (indices.back() != count - 1U) {
    indices.push_back(count - 1U);
  }
  return indices;
}

std::vector<std::size_t> clipped_sampled_indices(const std::vector<double>& values,
                                                 double min_m,
                                                 double max_m,
                                                 int stride) {
  std::vector<std::size_t> indices;
  if (values.empty() || max_m < values.front() - kCoordinateToleranceM ||
      min_m > values.back() + kCoordinateToleranceM) {
    return indices;
  }

  const auto lower = std::lower_bound(values.begin(), values.end(), min_m);
  std::size_t start = 0U;
  if (lower == values.end()) {
    start = values.size() - 1U;
  } else if (lower == values.begin() || nearly_equal(*lower, min_m)) {
    start = static_cast<std::size_t>(std::distance(values.begin(), lower));
  } else {
    start = static_cast<std::size_t>(std::distance(values.begin(), lower)) - 1U;
  }

  const auto upper = std::upper_bound(values.begin(), values.end(), max_m);
  std::size_t end = values.size() - 1U;
  if (upper == values.begin()) {
    end = 0U;
  } else if (upper == values.end()) {
    end = values.size() - 1U;
  } else {
    const std::size_t upper_index =
      static_cast<std::size_t>(std::distance(values.begin(), upper));
    if (nearly_equal(values[upper_index - 1U], max_m)) {
      end = upper_index - 1U;
    } else {
      end = upper_index;
    }
  }

  if (start > end) {
    return indices;
  }

  const std::size_t effective_stride = static_cast<std::size_t>(std::max(1, stride));
  for (std::size_t i = start; i <= end; i += effective_stride) {
    indices.push_back(i);
    if (end - i < effective_stride) {
      break;
    }
  }
  if (indices.empty() || indices.back() != end) {
    indices.push_back(end);
  }
  return indices;
}

std::string relative_output_path(const std::filesystem::path& output_directory,
                                 const std::filesystem::path& path) {
  std::error_code error;
  const std::filesystem::path relative = std::filesystem::relative(path, output_directory, error);
  if (error) {
    return path.generic_string();
  }
  return relative.generic_string();
}

TileFileMetadata write_render_tile(const TerrainTile& tile,
                                   const LodConfig& lod,
                                   const std::filesystem::path& output_directory) {
  const std::filesystem::path path =
    output_directory / "render" / ("lod" + std::to_string(lod.level)) /
    (sanitize_id(tile.id, "tile") + ".terrain.csv");
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open " + path.string());
  }
  output << "east_m,north_m,ellipsoidal_height_m,orthometric_height_m,"
            "normal_east,normal_north,normal_up\n";
  output << std::fixed << std::setprecision(6);
  const std::vector<std::size_t> row_indices = sampled_indices(tile.rows, lod.sample_stride);
  const std::vector<std::size_t> col_indices = sampled_indices(tile.cols, lod.sample_stride);
  for (const std::size_t row : row_indices) {
    for (const std::size_t col : col_indices) {
      const std::size_t index = tile.index(row, col);
      const TerrainNormal normal = tile.normals[index];
      output << tile.east_values[col] << "," << tile.north_values[row] << ","
             << tile.ellipsoidal_heights[index] << "," << tile.orthometric_heights[index] << ","
             << normal.east << "," << normal.north << "," << normal.up << "\n";
    }
  }
  if (!output) {
    throw std::runtime_error("failed to write " + path.string());
  }
  output.close();
  if (!output) {
    throw std::runtime_error("failed to close " + path.string());
  }
  return TileFileMetadata{
    tile.id,
    relative_output_path(output_directory, path),
    std::filesystem::file_size(path),
    row_indices.size(),
    col_indices.size(),
    tile.min_east_m(),
    tile.max_east_m(),
    tile.min_north_m(),
    tile.max_north_m(),
  };
}

bool tile_intersects_active_zone(const TerrainTile& tile, const ActiveAircraftZone& zone) {
  return interval_overlaps(tile.min_east_m(), tile.max_east_m(), zone.min_east_m, zone.max_east_m) &&
         interval_overlaps(
           tile.min_north_m(), tile.max_north_m(), zone.min_north_m, zone.max_north_m);
}

void validate_tile_metadata(const TileFileMetadata& metadata,
                            ValidationReport& report,
                            std::string_view code_prefix,
                            std::string_view source_id) {
  if (metadata.path.empty() || metadata.rows == 0U || metadata.cols == 0U ||
      metadata.min_east_m > metadata.max_east_m ||
      metadata.min_north_m > metadata.max_north_m) {
    add_issue(report,
              "error",
              std::string{code_prefix} + ".metadata.invalid",
              "Generated terrain tile metadata does not match a non-empty written sample grid.",
              std::string{source_id});
  }
}

void validate_collision_coverage(const TerrainTile& tile,
                                 const ActiveAircraftZone& zone,
                                 const TileFileMetadata& metadata,
                                 ValidationReport& report) {
  validate_tile_metadata(metadata, report, "terrain.collision", tile.id);
  const double covered_min_east_m = std::max(tile.min_east_m(), zone.min_east_m);
  const double covered_max_east_m = std::min(tile.max_east_m(), zone.max_east_m);
  const double covered_min_north_m = std::max(tile.min_north_m(), zone.min_north_m);
  const double covered_max_north_m = std::min(tile.max_north_m(), zone.max_north_m);
  if (metadata.min_east_m > covered_min_east_m + kCoordinateToleranceM ||
      metadata.max_east_m < covered_max_east_m - kCoordinateToleranceM ||
      metadata.min_north_m > covered_min_north_m + kCoordinateToleranceM ||
      metadata.max_north_m < covered_max_north_m - kCoordinateToleranceM) {
    add_issue(report,
              "error",
              "terrain.collision.coverage_incomplete",
              "Physical collision tile samples do not cover the active aircraft zone overlap.",
              tile.id);
  }
}

std::optional<TileFileMetadata> write_collision_tile(const TerrainTile& tile,
                                                     const ActiveAircraftZone& zone,
                                                     const std::filesystem::path& output_directory) {
  const std::vector<std::size_t> row_indices =
    clipped_sampled_indices(tile.north_values, zone.min_north_m, zone.max_north_m, zone.sample_stride);
  const std::vector<std::size_t> col_indices =
    clipped_sampled_indices(tile.east_values, zone.min_east_m, zone.max_east_m, zone.sample_stride);
  if (row_indices.empty() || col_indices.empty()) {
    return std::nullopt;
  }

  const std::filesystem::path path =
    output_directory / "collision" / (sanitize_id(tile.id, "tile") + ".collision.csv");
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open " + path.string());
  }
  output << "east_m,north_m,ellipsoidal_height_m,orthometric_height_m\n";
  output << std::fixed << std::setprecision(6);

  for (const std::size_t row : row_indices) {
    for (const std::size_t col : col_indices) {
      const std::size_t index = tile.index(row, col);
      output << tile.east_values[col] << "," << tile.north_values[row] << ","
             << tile.ellipsoidal_heights[index] << "," << tile.orthometric_heights[index] << "\n";
    }
  }
  if (!output) {
    throw std::runtime_error("failed to write " + path.string());
  }
  output.close();
  if (!output) {
    throw std::runtime_error("failed to close " + path.string());
  }

  return TileFileMetadata{
    tile.id,
    relative_output_path(output_directory, path),
    std::filesystem::file_size(path),
    row_indices.size(),
    col_indices.size(),
    tile.east_values[col_indices.front()],
    tile.east_values[col_indices.back()],
    tile.north_values[row_indices.front()],
    tile.north_values[row_indices.back()],
  };
}

TerrainPackageOutputs write_package_tiles(const std::vector<TerrainTile>& tiles,
                                          const TerrainConfig& config,
                                          const std::filesystem::path& output_directory,
                                          ValidationReport& report) {
  TerrainPackageOutputs outputs;
  for (const LodConfig& lod : config.render_lods) {
    LodOutputMetadata lod_output;
    lod_output.level = lod.level;
    lod_output.sample_stride = lod.sample_stride;
    for (const TerrainTile& tile : tiles) {
      TileFileMetadata render_tile = write_render_tile(tile, lod, output_directory);
      validate_tile_metadata(render_tile, report, "terrain.render", tile.id);
      lod_output.tiles.push_back(std::move(render_tile));
    }
    outputs.render_lods.push_back(std::move(lod_output));
  }

  for (const TerrainTile& tile : tiles) {
    if (!tile_intersects_active_zone(tile, config.active_aircraft_zone)) {
      continue;
    }
    std::optional<TileFileMetadata> collision_tile =
      write_collision_tile(tile, config.active_aircraft_zone, output_directory);
    if (!collision_tile.has_value()) {
      add_issue(report,
                "error",
                "terrain.collision.tile_empty",
                "Active aircraft zone overlap did not produce a physical collision sample grid.",
                tile.id);
      continue;
    }
    validate_collision_coverage(tile, config.active_aircraft_zone, *collision_tile, report);
    outputs.collision_tiles.push_back(std::move(*collision_tile));
  }
  if (outputs.collision_tiles.empty()) {
    add_issue(report,
              "error",
              "terrain.collision.empty",
              "Active aircraft zone did not produce any physical collision tiles.");
  }
  return outputs;
}

std::optional<double> bilinear_sample_tile(const TerrainTile& tile, double east_m, double north_m) {
  if (east_m < tile.min_east_m() - kCoordinateToleranceM ||
      east_m > tile.max_east_m() + kCoordinateToleranceM ||
      north_m < tile.min_north_m() - kCoordinateToleranceM ||
      north_m > tile.max_north_m() + kCoordinateToleranceM) {
    return std::nullopt;
  }

  auto bracket_index = [](const std::vector<double>& values,
                          double coordinate_m) -> std::optional<std::pair<std::size_t, std::size_t>> {
    const auto upper = std::lower_bound(values.begin(), values.end(), coordinate_m);
    if (upper == values.end()) {
      if (nearly_equal(values.back(), coordinate_m)) {
        return std::pair<std::size_t, std::size_t>{values.size() - 1U, values.size() - 1U};
      }
      return std::nullopt;
    }
    const std::size_t upper_index = static_cast<std::size_t>(std::distance(values.begin(), upper));
    if (nearly_equal(*upper, coordinate_m) || upper_index == 0U) {
      return std::pair<std::size_t, std::size_t>{upper_index, upper_index};
    }
    return std::pair<std::size_t, std::size_t>{upper_index - 1U, upper_index};
  };

  const std::optional<std::pair<std::size_t, std::size_t>> col =
    bracket_index(tile.east_values, east_m);
  const std::optional<std::pair<std::size_t, std::size_t>> row =
    bracket_index(tile.north_values, north_m);
  if (!col.has_value() || !row.has_value()) {
    return std::nullopt;
  }

  const auto [col0, col1] = *col;
  const auto [row0, row1] = *row;
  const double east_span = tile.east_values[col1] - tile.east_values[col0];
  const double north_span = tile.north_values[row1] - tile.north_values[row0];
  const double east_t = east_span == 0.0 ? 0.0 : (east_m - tile.east_values[col0]) / east_span;
  const double north_t = north_span == 0.0 ? 0.0 : (north_m - tile.north_values[row0]) / north_span;
  const double h00 = tile.ellipsoidal_heights[tile.index(row0, col0)];
  const double h10 = tile.ellipsoidal_heights[tile.index(row0, col1)];
  const double h01 = tile.ellipsoidal_heights[tile.index(row1, col0)];
  const double h11 = tile.ellipsoidal_heights[tile.index(row1, col1)];
  const double south = h00 + (h10 - h00) * east_t;
  const double north = h01 + (h11 - h01) * east_t;
  return south + (north - south) * north_t;
}

std::optional<double> sample_tiles(const std::vector<TerrainTile>& tiles,
                                   double east_m,
                                   double north_m) {
  for (const TerrainTile& tile : tiles) {
    if (const std::optional<double> height = bilinear_sample_tile(tile, east_m, north_m)) {
      return height;
    }
  }
  return std::nullopt;
}

std::vector<ControlPointResult> validate_control_points(const TerrainConfig& config,
                                                        const std::vector<TerrainTile>& tiles,
                                                        ValidationReport& report) {
  std::vector<ControlPointResult> results;
  for (const ControlPoint& point : config.control_points) {
    ControlPointResult result;
    result.id = point.id;
    result.east_m = config.transform.east_m(point.source_x_m);
    result.north_m = config.transform.north_m(point.source_y_m);
    const double expected_orthometric_height_m =
      config.transform.orthometric_height_m(point.expected_orthometric_height_m);
    result.expected_ellipsoidal_height_m =
      config.transform.ellipsoidal_height_m(expected_orthometric_height_m);
    result.allowed_error_m = point.source_vertical_error_m + 0.10;

    const std::optional<double> sampled_height =
      sample_tiles(tiles, result.east_m, result.north_m);
    if (!sampled_height.has_value()) {
      add_issue(report,
                "error",
                "terrain.control_point.outside_tiles",
                "Control point is outside the generated DMR 5G pilot terrain tiles.",
                point.id);
      results.push_back(result);
      continue;
    }
    result.sampled_ellipsoidal_height_m = *sampled_height;
    result.absolute_error_m =
      std::fabs(result.sampled_ellipsoidal_height_m - result.expected_ellipsoidal_height_m);
    result.passed = result.absolute_error_m <= result.allowed_error_m;
    if (!result.passed) {
      add_issue(report,
                "error",
                "terrain.control_point.height_error",
                "Transformed terrain height exceeds declared source error plus 0.10 m.",
                point.id);
    }
    results.push_back(result);
  }
  return results;
}

void compare_vertical_edge(const TerrainTile& west,
                           const TerrainTile& east,
                           EdgeContinuityResult& result) {
  if (!interval_overlaps(
        west.min_north_m(), west.max_north_m(), east.min_north_m(), east.max_north_m())) {
    return;
  }
  result.adjacent_pair_count += 1U;
  const double overlap_min_north_m = std::max(west.min_north_m(), east.min_north_m());
  const double overlap_max_north_m = std::min(west.max_north_m(), east.max_north_m());
  const std::size_t west_col = west.cols - 1U;
  const std::size_t east_col = 0U;
  for (std::size_t west_row = 0U; west_row < west.rows; ++west_row) {
    const double north_m = west.north_values[west_row];
    if (!coordinate_in_interval(north_m, overlap_min_north_m, overlap_max_north_m)) {
      continue;
    }
    const std::optional<std::size_t> east_row =
      find_coordinate_index(east.north_values, north_m);
    if (!east_row.has_value()) {
      result.unmatched_sample_count += 1U;
      continue;
    }
    const double step = std::fabs(
      west.ellipsoidal_heights[west.index(west_row, west_col)] -
      east.ellipsoidal_heights[east.index(*east_row, east_col)]);
    result.compared_sample_count += 1U;
    result.max_abs_step_m = std::max(result.max_abs_step_m, step);
  }
  for (std::size_t east_row = 0U; east_row < east.rows; ++east_row) {
    const double north_m = east.north_values[east_row];
    if (!coordinate_in_interval(north_m, overlap_min_north_m, overlap_max_north_m)) {
      continue;
    }
    if (!find_coordinate_index(west.north_values, north_m).has_value()) {
      result.unmatched_sample_count += 1U;
    }
  }
}

void compare_horizontal_edge(const TerrainTile& south,
                             const TerrainTile& north,
                             EdgeContinuityResult& result) {
  if (!interval_overlaps(
        south.min_east_m(), south.max_east_m(), north.min_east_m(), north.max_east_m())) {
    return;
  }
  result.adjacent_pair_count += 1U;
  const double overlap_min_east_m = std::max(south.min_east_m(), north.min_east_m());
  const double overlap_max_east_m = std::min(south.max_east_m(), north.max_east_m());
  const std::size_t south_row = south.rows - 1U;
  const std::size_t north_row = 0U;
  for (std::size_t south_col = 0U; south_col < south.cols; ++south_col) {
    const double east_m = south.east_values[south_col];
    if (!coordinate_in_interval(east_m, overlap_min_east_m, overlap_max_east_m)) {
      continue;
    }
    const std::optional<std::size_t> north_col =
      find_coordinate_index(north.east_values, east_m);
    if (!north_col.has_value()) {
      result.unmatched_sample_count += 1U;
      continue;
    }
    const double step = std::fabs(
      south.ellipsoidal_heights[south.index(south_row, south_col)] -
      north.ellipsoidal_heights[north.index(north_row, *north_col)]);
    result.compared_sample_count += 1U;
    result.max_abs_step_m = std::max(result.max_abs_step_m, step);
  }
  for (std::size_t north_col = 0U; north_col < north.cols; ++north_col) {
    const double east_m = north.east_values[north_col];
    if (!coordinate_in_interval(east_m, overlap_min_east_m, overlap_max_east_m)) {
      continue;
    }
    if (!find_coordinate_index(south.east_values, east_m).has_value()) {
      result.unmatched_sample_count += 1U;
    }
  }
}

EdgeContinuityResult validate_edge_continuity(const std::vector<TerrainTile>& tiles,
                                              double edge_tolerance_m,
                                              ValidationReport& report) {
  EdgeContinuityResult result;
  for (std::size_t i = 0U; i < tiles.size(); ++i) {
    for (std::size_t j = i + 1U; j < tiles.size(); ++j) {
      const TerrainTile& lhs = tiles[i];
      const TerrainTile& rhs = tiles[j];
      if (nearly_equal(lhs.max_east_m(), rhs.min_east_m())) {
        compare_vertical_edge(lhs, rhs, result);
      } else if (nearly_equal(rhs.max_east_m(), lhs.min_east_m())) {
        compare_vertical_edge(rhs, lhs, result);
      }
      if (nearly_equal(lhs.max_north_m(), rhs.min_north_m())) {
        compare_horizontal_edge(lhs, rhs, result);
      } else if (nearly_equal(rhs.max_north_m(), lhs.min_north_m())) {
        compare_horizontal_edge(rhs, lhs, result);
      }
    }
  }
  result.passed = result.max_abs_step_m <= edge_tolerance_m &&
                  result.unmatched_sample_count == 0U;
  if (result.max_abs_step_m > edge_tolerance_m) {
    add_issue(report,
              "error",
              "terrain.edge.continuity_failed",
              "Adjacent generated DMR 5G pilot tiles have an unintended height step.");
  }
  if (result.unmatched_sample_count != 0U) {
    add_issue(report,
              "error",
              "terrain.edge.unmatched_samples",
              "Adjacent generated DMR 5G pilot tile edges do not expose matching sample coordinates.");
  }
  return result;
}

std::string render_tile_metadata_array(const std::vector<TileFileMetadata>& tiles,
                                       std::string_view indent) {
  std::ostringstream output;
  output << "[\n";
  for (std::size_t i = 0U; i < tiles.size(); ++i) {
    const TileFileMetadata& tile = tiles[i];
    output << indent << "  {\n";
    output << indent << "    \"tileId\": " << json_quote(tile.tile_id) << ",\n";
    output << indent << "    \"path\": " << json_quote(tile.path) << ",\n";
    output << indent << "    \"sizeBytes\": " << tile.size_bytes << ",\n";
    output << indent << "    \"rows\": " << tile.rows << ",\n";
    output << indent << "    \"cols\": " << tile.cols << ",\n";
    output << indent << "    \"bounds\": {\"minEastM\": " << render_number(tile.min_east_m)
           << ", \"maxEastM\": " << render_number(tile.max_east_m)
           << ", \"minNorthM\": " << render_number(tile.min_north_m)
           << ", \"maxNorthM\": " << render_number(tile.max_north_m) << "}\n";
    output << indent << "  }";
    if (i + 1U != tiles.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "]";
  return output.str();
}

std::string render_control_point_results(const std::vector<ControlPointResult>& results,
                                         std::string_view indent) {
  std::ostringstream output;
  output << "[\n";
  for (std::size_t i = 0U; i < results.size(); ++i) {
    const ControlPointResult& point = results[i];
    output << indent << "  {\n";
    output << indent << "    \"id\": " << json_quote(point.id) << ",\n";
    output << indent << "    \"eastM\": " << render_number(point.east_m) << ",\n";
    output << indent << "    \"northM\": " << render_number(point.north_m) << ",\n";
    output << indent << "    \"sampledEllipsoidalHeightM\": "
           << render_number(point.sampled_ellipsoidal_height_m) << ",\n";
    output << indent << "    \"expectedEllipsoidalHeightM\": "
           << render_number(point.expected_ellipsoidal_height_m) << ",\n";
    output << indent << "    \"absoluteErrorM\": " << render_number(point.absolute_error_m)
           << ",\n";
    output << indent << "    \"allowedErrorM\": " << render_number(point.allowed_error_m) << ",\n";
    output << indent << "    \"passed\": " << (point.passed ? "true" : "false") << "\n";
    output << indent << "  }";
    if (i + 1U != results.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "]";
  return output.str();
}

std::uintmax_t total_tile_bytes(const std::vector<TileFileMetadata>& tiles) {
  std::uintmax_t total = 0U;
  for (const TileFileMetadata& tile : tiles) {
    total += tile.size_bytes;
  }
  return total;
}

std::uintmax_t total_render_bytes(const TerrainPackageOutputs& outputs) {
  std::uintmax_t total = 0U;
  for (const LodOutputMetadata& lod : outputs.render_lods) {
    total += total_tile_bytes(lod.tiles);
  }
  return total;
}

std::size_t total_render_tiles(const TerrainPackageOutputs& outputs) {
  std::size_t total = 0U;
  for (const LodOutputMetadata& lod : outputs.render_lods) {
    total += lod.tiles.size();
  }
  return total;
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

std::string render_sources(const ValidationReport& report, std::string_view indent) {
  std::ostringstream output;
  output << "[\n";
  for (std::size_t i = 0U; i < report.sources.size(); ++i) {
    const SourceValidationRecord& source = report.sources[i];
    output << indent << "  {\n";
    output << indent << "    \"sourceId\": " << json_quote(source.source_id) << ",\n";
    output << indent << "    \"path\": " << json_quote(source.path) << ",\n";
    output << indent << "    \"checksumAlgorithm\": "
           << json_quote(source.checksum_algorithm) << ",\n";
    output << indent << "    \"declaredChecksum\": " << json_quote(source.declared_checksum)
           << ",\n";
    output << indent << "    \"computedChecksum\": " << json_quote(source.computed_checksum)
           << ",\n";
    output << indent << "    \"checksumVerified\": "
           << (source.checksum_verified ? "true" : "false") << "\n";
    output << indent << "  }";
    if (i + 1U != report.sources.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "]";
  return output.str();
}

std::string render_terrain_validation_report(
  const ValidationReport& report,
  const std::vector<ControlPointResult>& control_points,
  EdgeContinuityResult edge_continuity,
  BoundaryCleanResult boundary_clean) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(report.schema_version) << ",\n";
  output << "  \"passed\": " << (report.passed ? "true" : "false") << ",\n";
  output << "  \"sourceManifestPath\": " << json_quote(report.source_manifest_path) << ",\n";
  output << "  \"packageManifestPath\": " << json_quote(report.package_manifest_path) << ",\n";
  output << "  \"packageId\": " << json_quote(report.package_id) << ",\n";
  output << "  \"issues\": " << render_issues(report, "  ") << ",\n";
  output << "  \"sources\": " << render_sources(report, "  ") << ",\n";
  output << "  \"terrainValidation\": {\n";
  output << "    \"controlPoints\": {\n";
  output << "      \"count\": " << control_points.size() << ",\n";
  output << "      \"points\": " << render_control_point_results(control_points, "      ") << "\n";
  output << "    },\n";
  output << "    \"edgeContinuity\": {\n";
  output << "      \"adjacentPairCount\": " << edge_continuity.adjacent_pair_count << ",\n";
  output << "      \"comparedSampleCount\": " << edge_continuity.compared_sample_count << ",\n";
  output << "      \"unmatchedSampleCount\": " << edge_continuity.unmatched_sample_count
         << ",\n";
  output << "      \"maxAbsStepM\": " << render_number(edge_continuity.max_abs_step_m) << ",\n";
  output << "      \"passed\": " << (edge_continuity.passed ? "true" : "false") << "\n";
  output << "    },\n";
  output << "    \"boundaryCleaning\": {\n";
  output << "      \"comparedSampleCount\": " << boundary_clean.compared_sample_count << ",\n";
  output << "      \"adjustedSampleCount\": " << boundary_clean.adjusted_sample_count << ",\n";
  output << "      \"maxPreCleanStepM\": " << render_number(boundary_clean.max_pre_clean_step_m)
         << ",\n";
  output << "      \"maxAdjustmentM\": " << render_number(boundary_clean.max_adjustment_m)
         << "\n";
  output << "    }\n";
  output << "  }\n";
  output << "}\n";
  return output.str();
}

std::string make_package_id(std::string_view package_name,
                            std::string_view package_version,
                            std::string_view package_id_hint) {
  if (!package_id_hint.empty()) {
    return sanitize_id(package_id_hint, "dmr5g-pilot-terrain");
  }
  return sanitize_id(package_name, "dmr5g-pilot-terrain") + "-" +
         sanitize_id(package_version, "unversioned");
}

std::string render_terrain_package_manifest(
  const Dmr5gPilotTerrainOptions& options,
  const SourceManifest& source_manifest,
  const TerrainConfig& config,
  const TerrainPackageOutputs& outputs,
  const std::vector<ControlPointResult>& control_points,
  EdgeContinuityResult edge_continuity,
  BoundaryCleanResult boundary_clean,
  std::string_view package_id) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(kTerrainPackageSchemaVersion) << ",\n";
  output << "  \"packageName\": " << json_quote(options.package_name) << ",\n";
  output << "  \"packageVersion\": " << json_quote(options.package_version) << ",\n";
  output << "  \"packageId\": " << json_quote(package_id) << ",\n";
  output << "  \"sourceManifestVersion\": "
         << json_quote(source_manifest.manifest_version) << ",\n";
  output << "  \"coverage\": {\n";
  output << "    \"scope\": " << json_quote(config.coverage_scope) << ",\n";
  output << "    \"countryCode\": \"CZ\",\n";
  output << "    \"completeWithinDeclaredBounds\": "
         << (config.coverage_scope == "czech-republic" ? "true" : "false") << "\n";
  output << "  },\n";
  output << "  \"sourceLineage\": [\n";
  const std::vector<const SourceDataset*> lineage_sources =
    terrain_lineage_sources(source_manifest, config);
  for (std::size_t i = 0U; i < lineage_sources.size(); ++i) {
    const SourceDataset& source = *lineage_sources[i];
    output << "    {\"sourceId\": " << json_quote(source.id)
           << ", \"datasetName\": " << json_quote(source.dataset_name)
           << ", \"version\": " << json_quote(source.version) << "}";
    if (i + 1U != lineage_sources.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"pilotRegion\": {\n";
  output << "    \"id\": " << json_quote(config.pilot_region.id) << ",\n";
  output << "    \"minEastM\": " << render_number(config.pilot_region.min_east_m) << ",\n";
  output << "    \"minNorthM\": " << render_number(config.pilot_region.min_north_m) << ",\n";
  output << "    \"widthM\": " << render_number(config.pilot_region.width_m) << ",\n";
  output << "    \"heightM\": " << render_number(config.pilot_region.height_m) << "\n";
  output << "  },\n";
  output << "  \"recordedProjGeoidConfiguration\": " << config.transform_json << ",\n";
  output << "  \"terrainMetadata\": {\n";
  output << "    \"heightEncoding\": \"meters-f64-csv\",\n";
  output << "    \"heightFields\": [\"ellipsoidal_height_m\", \"orthometric_height_m\"],\n";
  output << "    \"normalFrame\": \"project-local-ENU\",\n";
  output << "    \"normalEncoding\": \"unit-vector-f64-csv\",\n";
  output << "    \"edgeCleaned\": true,\n";
  output << "    \"collisionTilesAreSeparate\": true\n";
  output << "  },\n";
  output << "  \"renderLods\": [\n";
  for (std::size_t i = 0U; i < outputs.render_lods.size(); ++i) {
    const LodOutputMetadata& lod = outputs.render_lods[i];
    output << "    {\n";
    output << "      \"level\": " << lod.level << ",\n";
    output << "      \"sampleStride\": " << lod.sample_stride << ",\n";
    output << "      \"tiles\": " << render_tile_metadata_array(lod.tiles, "      ") << "\n";
    output << "    }";
    if (i + 1U != outputs.render_lods.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"collisionTiles\": " << render_tile_metadata_array(outputs.collision_tiles, "  ")
         << ",\n";
  output << "  \"streaming\": {\n";
  output << "    \"runtimeNetworkRequired\": false,\n";
  output << "    \"externalMapApis\": [],\n";
  output << "    \"addressing\": \"project-local-ENU-tile-bounds\",\n";
  output << "    \"coverageScope\": " << json_quote(config.coverage_scope) << ",\n";
  output << "    \"interruptionFreeWithinDeclaredBounds\": true\n";
  output << "  },\n";
  output << "  \"packagingLayout\": {\n";
  output << "    \"renderRoot\": \"render\",\n";
  output << "    \"collisionRoot\": \"collision\",\n";
  output << "    \"renderTileCount\": " << total_render_tiles(outputs) << ",\n";
  output << "    \"collisionTileCount\": " << outputs.collision_tiles.size() << ",\n";
  output << "    \"renderBytes\": " << total_render_bytes(outputs) << ",\n";
  output << "    \"collisionBytes\": " << total_tile_bytes(outputs.collision_tiles) << ",\n";
  output << "    \"totalBytes\": "
         << (total_render_bytes(outputs) + total_tile_bytes(outputs.collision_tiles)) << "\n";
  output << "  },\n";
  output << "  \"validation\": {\n";
  output << "    \"controlPoints\": {\n";
  output << "      \"maximumAllowedErrorMarginM\": 0.10,\n";
  output << "      \"points\": " << render_control_point_results(control_points, "      ") << "\n";
  output << "    },\n";
  output << "    \"edgeContinuity\": {\n";
  output << "      \"toleranceM\": " << render_number(config.edge_tolerance_m) << ",\n";
  output << "      \"adjacentPairCount\": " << edge_continuity.adjacent_pair_count << ",\n";
  output << "      \"comparedSampleCount\": " << edge_continuity.compared_sample_count << ",\n";
  output << "      \"unmatchedSampleCount\": " << edge_continuity.unmatched_sample_count
         << ",\n";
  output << "      \"maxAbsStepM\": " << render_number(edge_continuity.max_abs_step_m) << ",\n";
  output << "      \"passed\": " << (edge_continuity.passed ? "true" : "false") << "\n";
  output << "    },\n";
  output << "    \"boundaryCleaning\": {\n";
  output << "      \"maxPreCleanStepM\": " << render_number(boundary_clean.max_pre_clean_step_m)
         << ",\n";
  output << "      \"maxAdjustmentM\": " << render_number(boundary_clean.max_adjustment_m)
         << ",\n";
  output << "      \"adjustedSampleCount\": " << boundary_clean.adjusted_sample_count << "\n";
  output << "    }\n";
  output << "  }\n";
  output << "}\n";
  return output.str();
}

void write_report_if_requested(const Dmr5gPilotTerrainOptions& options,
                               const ValidationReport& report,
                               const std::vector<ControlPointResult>& control_points = {},
                               EdgeContinuityResult edge_continuity = {},
                               BoundaryCleanResult boundary_clean = {}) {
  if (!options.report_path.empty()) {
    write_text_file(options.report_path,
                    render_terrain_validation_report(
                      report, control_points, edge_continuity, boundary_clean));
  }
}

} // namespace

Dmr5gPilotTerrainResult process_dmr5g_pilot_terrain(
  const Dmr5gPilotTerrainOptions& options) {
  Dmr5gPilotTerrainResult result;
  result.report.schema_version = std::string{kValidationReportSchemaVersion};
  result.report.source_manifest_path = options.source_manifest_path.generic_string();
  result.report.package_manifest_path =
    (options.output_directory / "terrain-package.json").generic_string();

  std::vector<ControlPointResult> control_point_results;
  EdgeContinuityResult edge_continuity;
  BoundaryCleanResult boundary_clean;

  try {
    if (options.source_manifest_path.empty()) {
      add_issue(result.report,
                "error",
                "terrain.options.source_manifest_path.missing",
                "DMR 5G pilot terrain processing requires a source manifest path.");
    }
    if (options.terrain_config_path.empty()) {
      add_issue(result.report,
                "error",
                "terrain.options.terrain_config_path.missing",
                "DMR 5G pilot terrain processing requires a terrain config path.");
    }
    if (options.output_directory.empty()) {
      add_issue(result.report,
                "error",
                "terrain.options.output_directory.missing",
                "DMR 5G pilot terrain processing requires an output directory.");
    }
    if (options.package_version.empty()) {
      add_issue(result.report,
                "error",
                "terrain.options.package_version.missing",
                "DMR 5G pilot terrain processing requires a package version.");
    }
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report);
      return result;
    }

    ValidateOptions validate_options;
    validate_options.source_manifest_path = options.source_manifest_path;
    validate_options.source_root = options.source_root;
    validate_options.verify_checksums = options.verify_checksums;
    ValidationResult source_validation = validate_source_manifest(validate_options);
    result.report = std::move(source_validation.report);
    result.report.source_manifest_path = options.source_manifest_path.generic_string();
    result.report.package_manifest_path =
      (options.output_directory / "terrain-package.json").generic_string();
    if (!source_validation.accepted()) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report);
      return result;
    }

    std::optional<TerrainConfig> config = load_terrain_config(options.terrain_config_path, result.report);
    if (!config.has_value()) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report);
      return result;
    }
    if (options.require_czech_republic_scope &&
        (config->schema_version != kCzechTerrainConfigSchemaVersion ||
         config->coverage_scope != "czech-republic")) {
      add_issue(result.report,
                "error",
                "terrain.options.czech_republic_config.required",
                "The Czech Republic terrain command requires the Czech Republic schema and coverageScope 'czech-republic'.");
    }
    bind_source_tiles_to_manifest(*config, *source_validation.manifest, result.report);
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report);
      return result;
    }

    const std::filesystem::path source_root =
      options.source_root.empty() ? options.source_manifest_path.parent_path() : options.source_root;
    std::vector<TerrainTile> tiles = load_tiles(*config, source_root, result.report);
    if (tiles.empty()) {
      add_issue(result.report,
                "error",
                "terrain.dmr5g.tiles.empty",
                "No DMR 5G pilot terrain tiles were ingested.");
    }
    validate_czech_republic_terrain_coverage(*config, tiles, result.report);
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report);
      return result;
    }

    boundary_clean = clean_tile_boundaries(tiles, *config, result.report);
    compute_normals(tiles);
    control_point_results = validate_control_points(*config, tiles, result.report);
    edge_continuity = validate_edge_continuity(tiles, config->edge_tolerance_m, result.report);
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report, control_point_results, edge_continuity, boundary_clean);
      return result;
    }

    TerrainPackageOutputs outputs =
      write_package_tiles(tiles, *config, options.output_directory, result.report);
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report, control_point_results, edge_continuity, boundary_clean);
      return result;
    }

    const std::string package_id =
      make_package_id(options.package_name, options.package_version, config->package_id_hint);
    result.report.package_id = package_id;
    const std::filesystem::path package_manifest_path =
      options.output_directory / "terrain-package.json";
    write_text_file(package_manifest_path,
                    render_terrain_package_manifest(options,
                                                    *source_validation.manifest,
                                                    *config,
                                                    outputs,
                                                    control_point_results,
                                                    edge_continuity,
                                                    boundary_clean,
                                                    package_id));
    result.package_manifest_path = package_manifest_path;
    finalize_report(result.report);
    write_report_if_requested(options, result.report, control_point_results, edge_continuity, boundary_clean);
  } catch (const std::exception& error) {
    add_issue(result.report, "error", "terrain.processing_failed", error.what());
    result.package_manifest_path.clear();
    finalize_report(result.report);
    write_report_if_requested(options, result.report, control_point_results, edge_continuity, boundary_clean);
  }

  return result;
}

Dmr5gPilotTerrainResult process_dmr5g_czech_republic_terrain(
  const Dmr5gCzechRepublicTerrainOptions& options) {
  Dmr5gCzechRepublicTerrainOptions scoped_options = options;
  scoped_options.require_czech_republic_scope = true;
  return process_dmr5g_pilot_terrain(scoped_options);
}

} // namespace flying::data_pipeline
