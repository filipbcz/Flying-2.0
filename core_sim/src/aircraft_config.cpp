#include "flying/core_sim/aircraft_config.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace flying::core_sim {
namespace {

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
    std::size_t parsed = 0;
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

  void consume_literal(std::string_view literal) {
    if (text_.substr(index_, literal.size()) != literal) {
      throw std::runtime_error("invalid JSON literal");
    }
    index_ += literal.size();
  }

  void expect(char expected) {
    skip_whitespace();
    if (index_ >= text_.size() || text_[index_] != expected) {
      throw std::runtime_error("unexpected JSON punctuation");
    }
    ++index_;
  }

  [[nodiscard]] bool try_consume(char expected) noexcept {
    skip_whitespace();
    if (index_ < text_.size() && text_[index_] == expected) {
      ++index_;
      return true;
    }
    return false;
  }

  void require_digit() {
    if (index_ >= text_.size() ||
        std::isdigit(static_cast<unsigned char>(text_[index_])) == 0) {
      throw std::runtime_error("expected JSON digit");
    }
  }

  void skip_whitespace() noexcept {
    while (index_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[index_])) != 0) {
      ++index_;
    }
  }

  std::string_view text_;
  std::size_t index_{};
};

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("unable to open aircraft configuration: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] const JsonValue* find_member(const JsonValue::Object& object,
                                           std::string_view key) noexcept {
  const auto found = object.find(std::string(key));
  return found == object.end() ? nullptr : &found->second;
}

[[nodiscard]] const JsonValue::Object& as_object(const JsonValue& value,
                                                 std::string_view context) {
  if (value.type != JsonValue::Type::object_value) {
    throw std::runtime_error(std::string(context) + " must be an object");
  }
  return value.object;
}

[[nodiscard]] const JsonValue::Array& as_array(const JsonValue& value,
                                               std::string_view context) {
  if (value.type != JsonValue::Type::array_value) {
    throw std::runtime_error(std::string(context) + " must be an array");
  }
  return value.array;
}

[[nodiscard]] const JsonValue::Object& require_object(const JsonValue::Object& object,
                                                      std::string_view key,
                                                      std::string_view context) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " is required");
  }
  return as_object(*value, std::string(context) + "." + std::string(key));
}

[[nodiscard]] const JsonValue::Array& require_array(const JsonValue::Object& object,
                                                    std::string_view key,
                                                    std::string_view context) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " is required");
  }
  return as_array(*value, std::string(context) + "." + std::string(key));
}

[[nodiscard]] std::string require_string(const JsonValue::Object& object,
                                         std::string_view key,
                                         std::string_view context) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " is required");
  }
  if (value->type != JsonValue::Type::string_value) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " must be a string");
  }
  return value->string;
}

[[nodiscard]] double require_number(const JsonValue::Object& object,
                                    std::string_view key,
                                    std::string_view context) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " is required");
  }
  if (value->type != JsonValue::Type::number_value || !std::isfinite(value->number)) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) +
                             " must be a finite number");
  }
  return value->number;
}

[[nodiscard]] bool require_bool(const JsonValue::Object& object,
                                std::string_view key,
                                std::string_view context) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " is required");
  }
  if (value->type != JsonValue::Type::bool_value) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " must be a bool");
  }
  return value->boolean;
}

[[nodiscard]] bool optional_bool(const JsonValue::Object& object,
                                 std::string_view key,
                                 bool fallback,
                                 std::string_view context) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr) {
    return fallback;
  }
  if (value->type != JsonValue::Type::bool_value) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " must be a bool");
  }
  return value->boolean;
}

[[nodiscard]] int require_int(const JsonValue::Object& object,
                              std::string_view key,
                              std::string_view context) {
  const double value = require_number(object, key, context);
  const double rounded = std::round(value);
  if (std::abs(value - rounded) > 1.0e-9) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) +
                             " must be an integer");
  }
  return static_cast<int>(rounded);
}

[[nodiscard]] std::vector<std::string> parse_string_array(const JsonValue::Object& object,
                                                          std::string_view key,
                                                          std::string_view context) {
  const JsonValue::Array& array = require_array(object, key, context);
  std::vector<std::string> values;
  for (std::size_t index = 0; index < array.size(); ++index) {
    if (array[index].type != JsonValue::Type::string_value) {
      throw std::runtime_error(std::string(context) + "." + std::string(key) +
                               " entries must be strings");
    }
    values.push_back(array[index].string);
  }
  return values;
}

[[nodiscard]] std::vector<std::string> optional_string_array(
    const JsonValue::Object& object,
    std::string_view key,
    std::string_view context) {
  if (find_member(object, key) == nullptr) {
    return {};
  }
  return parse_string_array(object, key, context);
}

[[nodiscard]] Vector3d parse_vector_object(const JsonValue::Object& object,
                                           std::string_view context) {
  return {
    require_number(object, "x", context),
    require_number(object, "y", context),
    require_number(object, "z", context),
  };
}

[[nodiscard]] AircraftInertiaTensor parse_inertia_object(const JsonValue::Object& object,
                                                         std::string_view context) {
  return {
    require_number(object, "ixx", context),
    require_number(object, "iyy", context),
    require_number(object, "izz", context),
    require_number(object, "ixy", context),
    require_number(object, "ixz", context),
    require_number(object, "iyz", context),
  };
}

[[nodiscard]] AircraftValueMetadata parse_scalar_metadata(const JsonValue::Object& object,
                                                          std::string_view context) {
  const JsonValue::Object& validity = require_object(object, "validity", context);
  AircraftValueMetadata metadata{};
  metadata.unit = require_string(object, "unit", context);
  metadata.source_refs = parse_string_array(object, "sourceRefs", context);
  metadata.confidence = require_string(object, "confidence", context);
  metadata.validity_min = require_number(validity, "min", std::string(context) + ".validity");
  metadata.validity_max = require_number(validity, "max", std::string(context) + ".validity");
  const std::string validity_unit =
      require_string(validity, "unit", std::string(context) + ".validity");
  if (validity_unit != metadata.unit) {
    throw std::runtime_error(std::string(context) + " validity unit must match value unit");
  }
  return metadata;
}

[[nodiscard]] AircraftScalar parse_scalar(const JsonValue::Object& object,
                                          std::string_view key,
                                          std::string_view context) {
  const JsonValue::Object& scalar = require_object(object, key, context);
  return {require_number(scalar, "value", std::string(context) + "." + std::string(key)),
          parse_scalar_metadata(scalar, std::string(context) + "." + std::string(key))};
}

[[nodiscard]] AircraftVectorMetadata parse_vector_metadata(const JsonValue::Object& object,
                                                           std::string_view context) {
  const JsonValue::Object& validity = require_object(object, "validity", context);
  AircraftVectorMetadata metadata{};
  metadata.unit = require_string(object, "unit", context);
  metadata.source_refs = parse_string_array(object, "sourceRefs", context);
  metadata.confidence = require_string(object, "confidence", context);
  metadata.validity_min = parse_vector_object(
      require_object(validity, "min", std::string(context) + ".validity"),
      std::string(context) + ".validity.min");
  metadata.validity_max = parse_vector_object(
      require_object(validity, "max", std::string(context) + ".validity"),
      std::string(context) + ".validity.max");
  const std::string validity_unit =
      require_string(validity, "unit", std::string(context) + ".validity");
  if (validity_unit != metadata.unit) {
    throw std::runtime_error(std::string(context) + " validity unit must match value unit");
  }
  return metadata;
}

[[nodiscard]] AircraftVectorValue parse_vector_value(const JsonValue::Object& object,
                                                     std::string_view key,
                                                     std::string_view context) {
  const JsonValue::Object& vector = require_object(object, key, context);
  return {
    parse_vector_object(require_object(vector, "value",
                                       std::string(context) + "." + std::string(key)),
                        std::string(context) + "." + std::string(key) + ".value"),
    parse_vector_metadata(vector, std::string(context) + "." + std::string(key)),
  };
}

[[nodiscard]] AircraftInertiaTensorValue parse_inertia_tensor_value(
    const JsonValue::Object& object,
    std::string_view key,
    std::string_view context) {
  const std::string child_context = std::string(context) + "." + std::string(key);
  const JsonValue::Object& tensor = require_object(object, key, context);
  const JsonValue::Object& validity = require_object(tensor, "validity", child_context);
  AircraftInertiaTensorValue value{};
  value.value = parse_inertia_object(require_object(tensor, "value", child_context),
                                     child_context + ".value");
  value.unit = require_string(tensor, "unit", child_context);
  value.source_refs = parse_string_array(tensor, "sourceRefs", child_context);
  value.confidence = require_string(tensor, "confidence", child_context);
  value.validity_min =
      parse_inertia_object(require_object(validity, "min", child_context + ".validity"),
                           child_context + ".validity.min");
  value.validity_max =
      parse_inertia_object(require_object(validity, "max", child_context + ".validity"),
                           child_context + ".validity.max");
  const std::string validity_unit = require_string(validity, "unit", child_context + ".validity");
  if (validity_unit != value.unit) {
    throw std::runtime_error(child_context + " validity unit must match value unit");
  }
  return value;
}

[[nodiscard]] AircraftSourceReference parse_source_reference(const JsonValue& value,
                                                             std::size_t index) {
  const std::string context = "sourceReferences[" + std::to_string(index) + "]";
  const JsonValue::Object& object = as_object(value, context);
  return {
    require_string(object, "id", context),
    require_string(object, "title", context),
    require_string(object, "document", context),
    require_string(object, "license", context),
    require_string(object, "permittedUse", context),
    require_string(object, "provenance", context),
    require_string(object, "confidence", context),
    parse_string_array(object, "usedFor", context),
    optional_bool(object, "approvedForFaithfulClaim", false, context),
  };
}

[[nodiscard]] AircraftTable1d parse_table(const JsonValue& value, std::string_view context) {
  const JsonValue::Object& object = as_object(value, context);
  const JsonValue::Object& validity = require_object(object, "validityRange", context);
  AircraftTable1d table{};
  table.id = require_string(object, "id", context);
  table.input = require_string(object, "input", context);
  table.input_unit = require_string(object, "inputUnit", context);
  table.output = require_string(object, "output", context);
  table.output_unit = require_string(object, "outputUnit", context);
  table.confidence = require_string(object, "confidence", context);
  table.validity_min = require_number(validity, "min", std::string(context) + ".validityRange");
  table.validity_max = require_number(validity, "max", std::string(context) + ".validityRange");
  const std::string validity_unit =
      require_string(validity, "unit", std::string(context) + ".validityRange");
  if (validity_unit != table.input_unit) {
    throw std::runtime_error(std::string(context) + " validity unit must match input unit");
  }
  table.source_refs = parse_string_array(object, "sourceRefs", context);
  const JsonValue::Array& points = require_array(object, "points", context);
  for (std::size_t index = 0; index < points.size(); ++index) {
    const std::string point_context =
        std::string(context) + ".points[" + std::to_string(index) + "]";
    const JsonValue::Object& point = as_object(points[index], point_context);
    table.points.push_back({
      require_number(point, "x", point_context),
      require_number(point, "y", point_context),
    });
  }
  return table;
}

[[nodiscard]] std::vector<AircraftTable1d> parse_tables(const JsonValue::Object& object,
                                                        std::string_view key,
                                                        std::string_view context) {
  const JsonValue::Array& array = require_array(object, key, context);
  std::vector<AircraftTable1d> tables;
  for (std::size_t index = 0; index < array.size(); ++index) {
    tables.push_back(parse_table(array[index],
                                 std::string(context) + "." + std::string(key) +
                                     "[" + std::to_string(index) + "]"));
  }
  return tables;
}

[[nodiscard]] AircraftFuelStation parse_fuel_station(const JsonValue& value, std::size_t index) {
  const std::string context = "massBalance.fuelStations[" + std::to_string(index) + "]";
  const JsonValue::Object& object = as_object(value, context);
  return {
    require_string(object, "id", context),
    require_string(object, "displayName", context),
    parse_scalar(object, "capacityKg", context),
    parse_scalar(object, "unusableKg", context),
    parse_scalar(object, "defaultQuantityKg", context),
    parse_vector_value(object, "positionBodyM", context),
  };
}

[[nodiscard]] AircraftPayloadStation parse_payload_station(const JsonValue& value,
                                                           std::size_t index) {
  const std::string context = "massBalance.payloadStations[" + std::to_string(index) + "]";
  const JsonValue::Object& object = as_object(value, context);
  return {
    require_string(object, "id", context),
    require_string(object, "displayName", context),
    parse_scalar(object, "maxMassKg", context),
    parse_scalar(object, "defaultMassKg", context),
    parse_vector_value(object, "positionBodyM", context),
  };
}

[[nodiscard]] AircraftCgEnvelopePoint parse_cg_envelope_point(const JsonValue& value,
                                                              std::size_t index) {
  const std::string context = "massBalance.cgEnvelope[" + std::to_string(index) + "]";
  const JsonValue::Object& object = as_object(value, context);
  return {
    parse_scalar(object, "massKg", context),
    parse_vector_value(object, "forwardLimitBodyM", context),
    parse_vector_value(object, "aftLimitBodyM", context),
  };
}

[[nodiscard]] AircraftActuatorModel parse_actuator(const JsonValue& value, std::size_t index) {
  const std::string context = "actuators[" + std::to_string(index) + "]";
  const JsonValue::Object& object = as_object(value, context);
  return {
    require_string(object, "id", context),
    require_string(object, "surface", context),
    require_string(object, "command", context),
    parse_scalar(object, "minDeflectionRad", context),
    parse_scalar(object, "maxDeflectionRad", context),
    parse_scalar(object, "slewRateRadps", context),
    parse_scalar(object, "timeConstantS", context),
  };
}

[[nodiscard]] AircraftLandingGearContact parse_landing_gear(const JsonValue& value,
                                                            std::size_t index) {
  const std::string context = "landingGear.contacts[" + std::to_string(index) + "]";
  const JsonValue::Object& object = as_object(value, context);
  return {
    require_string(object, "id", context),
    require_string(object, "type", context),
    require_string(object, "brakeGroup", context),
    require_bool(object, "retractable", context),
    parse_vector_value(object, "positionBodyM", context),
    parse_scalar(object, "staticFriction", context),
    parse_scalar(object, "dynamicFriction", context),
    parse_scalar(object, "rollingFriction", context),
    parse_scalar(object, "springCoefficientNPerM", context),
    parse_scalar(object, "dampingCoefficientNSPerM", context),
    parse_scalar(object, "maxSteerRad", context),
  };
}

[[nodiscard]] AircraftBrakeModel parse_brake(const JsonValue& value, std::size_t index) {
  const std::string context = "brakes[" + std::to_string(index) + "]";
  const JsonValue::Object& object = as_object(value, context);
  return {
    require_string(object, "id", context),
    require_string(object, "wheel", context),
    require_string(object, "brakeGroup", context),
    parse_scalar(object, "maxTorqueNm", context),
    parse_scalar(object, "responseTimeS", context),
    parse_scalar(object, "parkingBrakeHoldNorm", context),
  };
}

[[nodiscard]] AircraftConfiguration parse_configuration(const JsonValue& root) {
  const JsonValue::Object& object = as_object(root, "aircraftConfig");
  const JsonValue::Object& aircraft = require_object(object, "aircraft", "aircraftConfig");
  const JsonValue::Object& validation =
      require_object(aircraft, "validation", "aircraftConfig.aircraft");
  const JsonValue::Object& license = require_object(object, "license", "aircraftConfig");
  const JsonValue::Object& provenance = require_object(object, "provenance", "aircraftConfig");
  const JsonValue::Object& geometry = require_object(object, "geometry", "aircraftConfig");
  const JsonValue::Object& mass_balance =
      require_object(object, "massBalance", "aircraftConfig");
  const JsonValue::Object& aerodynamics =
      require_object(object, "aerodynamics", "aircraftConfig");
  const JsonValue::Object& engine = require_object(object, "engine", "aircraftConfig");
  const JsonValue::Object& propeller = require_object(object, "propeller", "aircraftConfig");
  const JsonValue::Object& landing_gear =
      require_object(object, "landingGear", "aircraftConfig");

  AircraftConfiguration configuration{};
  configuration.schema_version = require_string(object, "schemaVersion", "aircraftConfig");
  configuration.identity.backend = require_string(aircraft, "backend", "aircraftConfig.aircraft");
  configuration.identity.model_name =
      require_string(aircraft, "modelName", "aircraftConfig.aircraft");
  configuration.identity.model_version =
      require_string(aircraft, "modelVersion", "aircraftConfig.aircraft");
  configuration.identity.data_root =
      require_string(aircraft, "dataRoot", "aircraftConfig.aircraft");
  configuration.identity.source_license =
      require_string(aircraft, "sourceLicense", "aircraftConfig.aircraft");
  configuration.display_name = require_string(aircraft, "displayName", "aircraftConfig.aircraft");
  configuration.validation_status =
      require_string(validation, "status", "aircraftConfig.aircraft.validation");
  configuration.validation_suite =
      require_string(validation, "requiredSuite", "aircraftConfig.aircraft.validation");
  configuration.validation_suite_status =
      require_string(validation, "suiteStatus", "aircraftConfig.aircraft.validation");
  configuration.validation_approved_references =
      optional_string_array(validation, "approvedReferences", "aircraftConfig.aircraft.validation");
  configuration.license_spdx = require_string(license, "spdxId", "aircraftConfig.license");
  configuration.license_notice = require_string(license, "notice", "aircraftConfig.license");
  configuration.provenance_summary =
      require_string(provenance, "summary", "aircraftConfig.provenance");

  const JsonValue::Array& source_refs =
      require_array(object, "sourceReferences", "aircraftConfig");
  for (std::size_t index = 0; index < source_refs.size(); ++index) {
    configuration.source_references.push_back(parse_source_reference(source_refs[index], index));
  }

  configuration.geometry.coordinate_frame =
      require_string(geometry, "coordinateFrame", "aircraftConfig.geometry");
  configuration.geometry.wing_area_m2 = parse_scalar(geometry, "wingAreaM2", "geometry");
  configuration.geometry.wingspan_m = parse_scalar(geometry, "wingspanM", "geometry");
  configuration.geometry.mean_aerodynamic_chord_m =
      parse_scalar(geometry, "meanAerodynamicChordM", "geometry");
  configuration.geometry.fuselage_length_m =
      parse_scalar(geometry, "fuselageLengthM", "geometry");
  configuration.geometry.horizontal_tail_area_m2 =
      parse_scalar(geometry, "horizontalTailAreaM2", "geometry");
  configuration.geometry.horizontal_tail_arm_m =
      parse_scalar(geometry, "horizontalTailArmM", "geometry");
  configuration.geometry.vertical_tail_area_m2 =
      parse_scalar(geometry, "verticalTailAreaM2", "geometry");
  configuration.geometry.vertical_tail_arm_m =
      parse_scalar(geometry, "verticalTailArmM", "geometry");
  configuration.geometry.propeller_ground_clearance_m =
      parse_scalar(geometry, "propellerGroundClearanceM", "geometry");
  configuration.geometry.aerodynamic_reference_point_body_m =
      parse_vector_value(geometry, "aerodynamicReferencePointBodyM", "geometry");
  configuration.geometry.visual_reference_point_body_m =
      parse_vector_value(geometry, "visualReferencePointBodyM", "geometry");

  configuration.mass_balance.inertia_reference =
      require_string(mass_balance, "inertiaReference", "massBalance");
  configuration.mass_balance.empty_mass_kg =
      parse_scalar(mass_balance, "emptyMassKg", "massBalance");
  configuration.mass_balance.max_takeoff_mass_kg =
      parse_scalar(mass_balance, "maxTakeoffMassKg", "massBalance");
  configuration.mass_balance.empty_cg_body_m =
      parse_vector_value(mass_balance, "emptyCgBodyM", "massBalance");
  configuration.mass_balance.empty_inertia_kg_m2 =
      parse_inertia_tensor_value(mass_balance, "emptyInertiaKgM2", "massBalance");
  const JsonValue::Array& cg_envelope =
      require_array(mass_balance, "cgEnvelope", "massBalance");
  for (std::size_t index = 0; index < cg_envelope.size(); ++index) {
    configuration.mass_balance.cg_envelope.push_back(
        parse_cg_envelope_point(cg_envelope[index], index));
  }
  const JsonValue::Array& fuel_stations =
      require_array(mass_balance, "fuelStations", "massBalance");
  for (std::size_t index = 0; index < fuel_stations.size(); ++index) {
    configuration.mass_balance.fuel_stations.push_back(
        parse_fuel_station(fuel_stations[index], index));
  }
  const JsonValue::Array& payload_stations =
      require_array(mass_balance, "payloadStations", "massBalance");
  for (std::size_t index = 0; index < payload_stations.size(); ++index) {
    configuration.mass_balance.payload_stations.push_back(
        parse_payload_station(payload_stations[index], index));
  }

  configuration.aerodynamics.coefficient_frame =
      require_string(aerodynamics, "coefficientFrame", "aerodynamics");
  configuration.aerodynamics.alpha_min_rad =
      parse_scalar(aerodynamics, "alphaMinRad", "aerodynamics");
  configuration.aerodynamics.alpha_max_rad =
      parse_scalar(aerodynamics, "alphaMaxRad", "aerodynamics");
  configuration.aerodynamics.beta_max_abs_rad =
      parse_scalar(aerodynamics, "betaMaxAbsRad", "aerodynamics");
  configuration.aerodynamics.tables = parse_tables(aerodynamics, "tables", "aerodynamics");

  configuration.engine.id = require_string(engine, "id", "engine");
  configuration.engine.type = require_string(engine, "type", "engine");
  configuration.engine.rated_power_w = parse_scalar(engine, "ratedPowerW", "engine");
  configuration.engine.idle_rpm = parse_scalar(engine, "idleRpm", "engine");
  configuration.engine.max_rpm = parse_scalar(engine, "maxRpm", "engine");
  configuration.engine.displacement_m3 = parse_scalar(engine, "displacementM3", "engine");
  configuration.engine.fuel_density_kg_per_l =
      parse_scalar(engine, "fuelDensityKgPerL", "engine");
  configuration.engine.tables = parse_tables(engine, "tables", "engine");

  configuration.propeller.id = require_string(propeller, "id", "propeller");
  configuration.propeller.type = require_string(propeller, "type", "propeller");
  configuration.propeller.blade_count = require_int(propeller, "bladeCount", "propeller");
  configuration.propeller.diameter_m = parse_scalar(propeller, "diameterM", "propeller");
  configuration.propeller.inertia_kg_m2 = parse_scalar(propeller, "inertiaKgM2", "propeller");
  configuration.propeller.min_pitch_rad = parse_scalar(propeller, "minPitchRad", "propeller");
  configuration.propeller.max_pitch_rad = parse_scalar(propeller, "maxPitchRad", "propeller");
  configuration.propeller.tables = parse_tables(propeller, "tables", "propeller");

  const JsonValue::Array& actuators = require_array(object, "actuators", "aircraftConfig");
  for (std::size_t index = 0; index < actuators.size(); ++index) {
    configuration.actuators.push_back(parse_actuator(actuators[index], index));
  }
  const JsonValue::Array& contacts = require_array(landing_gear, "contacts", "landingGear");
  for (std::size_t index = 0; index < contacts.size(); ++index) {
    configuration.landing_gear.push_back(parse_landing_gear(contacts[index], index));
  }
  const JsonValue::Array& brakes = require_array(object, "brakes", "aircraftConfig");
  for (std::size_t index = 0; index < brakes.size(); ++index) {
    configuration.brakes.push_back(parse_brake(brakes[index], index));
  }

  return configuration;
}

[[nodiscard]] bool is_finite(Vector3d value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool is_finite(const AircraftInertiaTensor& value) noexcept {
  return std::isfinite(value.ixx) && std::isfinite(value.iyy) &&
         std::isfinite(value.izz) && std::isfinite(value.ixy) &&
         std::isfinite(value.ixz) && std::isfinite(value.iyz);
}

[[nodiscard]] bool is_known_confidence(std::string_view confidence) noexcept {
  return confidence == "low" || confidence == "medium" || confidence == "high";
}

void append_source_errors(const std::vector<std::string>& refs,
                          const std::set<std::string>& known_sources,
                          std::string_view context,
                          std::vector<std::string>& errors) {
  if (refs.empty()) {
    errors.push_back(std::string(context) + " must reference at least one source");
  }
  for (const std::string& ref : refs) {
    if (known_sources.find(ref) == known_sources.end()) {
      errors.push_back(std::string(context) + " references unknown source " + ref);
    }
  }
}

void validate_scalar(const AircraftScalar& scalar,
                     std::string_view unit,
                     const std::set<std::string>& known_sources,
                     std::string_view context,
                     std::vector<std::string>& errors) {
  if (!std::isfinite(scalar.value)) {
    errors.push_back(std::string(context) + " value must be finite");
  }
  if (scalar.metadata.unit != unit) {
    errors.push_back(std::string(context) + " unit must be " + std::string(unit));
  }
  if (!is_known_confidence(scalar.metadata.confidence)) {
    errors.push_back(std::string(context) + " confidence must be low, medium, or high");
  }
  if (!std::isfinite(scalar.metadata.validity_min) ||
      !std::isfinite(scalar.metadata.validity_max) ||
      scalar.metadata.validity_min > scalar.metadata.validity_max ||
      scalar.value < scalar.metadata.validity_min ||
      scalar.value > scalar.metadata.validity_max) {
    errors.push_back(std::string(context) + " value must be inside a finite validity range");
  }
  append_source_errors(scalar.metadata.source_refs, known_sources, context, errors);
}

void validate_vector_value(const AircraftVectorValue& vector,
                           std::string_view unit,
                           const std::set<std::string>& known_sources,
                           std::string_view context,
                           std::vector<std::string>& errors) {
  if (!is_finite(vector.value)) {
    errors.push_back(std::string(context) + " value must be finite");
  }
  if (vector.metadata.unit != unit) {
    errors.push_back(std::string(context) + " unit must be " + std::string(unit));
  }
  if (!is_known_confidence(vector.metadata.confidence)) {
    errors.push_back(std::string(context) + " confidence must be low, medium, or high");
  }
  if (!is_finite(vector.metadata.validity_min) ||
      !is_finite(vector.metadata.validity_max) ||
      vector.metadata.validity_min.x > vector.metadata.validity_max.x ||
      vector.metadata.validity_min.y > vector.metadata.validity_max.y ||
      vector.metadata.validity_min.z > vector.metadata.validity_max.z ||
      vector.value.x < vector.metadata.validity_min.x ||
      vector.value.x > vector.metadata.validity_max.x ||
      vector.value.y < vector.metadata.validity_min.y ||
      vector.value.y > vector.metadata.validity_max.y ||
      vector.value.z < vector.metadata.validity_min.z ||
      vector.value.z > vector.metadata.validity_max.z) {
    errors.push_back(std::string(context) + " value must be inside a finite validity range");
  }
  append_source_errors(vector.metadata.source_refs, known_sources, context, errors);
}

void validate_inertia_tensor_value(const AircraftInertiaTensorValue& tensor,
                                   const std::set<std::string>& known_sources,
                                   std::string_view context,
                                   std::vector<std::string>& errors) {
  if (tensor.unit != "kilogram_meter_squared") {
    errors.push_back(std::string(context) + " unit must be kilogram_meter_squared");
  }
  if (!is_known_confidence(tensor.confidence)) {
    errors.push_back(std::string(context) + " confidence must be low, medium, or high");
  }
  if (!is_finite(tensor.value) ||
      tensor.value.ixx <= 0.0 ||
      tensor.value.iyy <= 0.0 ||
      tensor.value.izz <= 0.0 ||
      !is_finite(tensor.validity_min) ||
      !is_finite(tensor.validity_max) ||
      tensor.validity_min.ixx > tensor.value.ixx ||
      tensor.validity_max.ixx < tensor.value.ixx ||
      tensor.validity_min.iyy > tensor.value.iyy ||
      tensor.validity_max.iyy < tensor.value.iyy ||
      tensor.validity_min.izz > tensor.value.izz ||
      tensor.validity_max.izz < tensor.value.izz) {
    errors.push_back(std::string(context) +
                     " must contain positive finite diagonal moments inside validity bounds");
  }
  append_source_errors(tensor.source_refs, known_sources, context, errors);
}

void validate_table(const AircraftTable1d& table,
                    const std::set<std::string>& known_sources,
                    std::string_view context,
                    std::vector<std::string>& errors) {
  if (table.id.empty() || table.input.empty() || table.output.empty()) {
    errors.push_back(std::string(context) + " id, input, and output must be populated");
  }
  if (table.input_unit.empty() || table.output_unit.empty()) {
    errors.push_back(std::string(context) + " units must be populated");
  }
  if (!is_known_confidence(table.confidence)) {
    errors.push_back(std::string(context) + " confidence must be low, medium, or high");
  }
  if (!std::isfinite(table.validity_min) ||
      !std::isfinite(table.validity_max) ||
      table.validity_min >= table.validity_max) {
    errors.push_back(std::string(context) + " validity range must be finite and ordered");
  }
  append_source_errors(table.source_refs, known_sources, context, errors);
  if (table.points.size() < 2) {
    errors.push_back(std::string(context) + " must contain at least two points");
    return;
  }
  for (std::size_t index = 0; index < table.points.size(); ++index) {
    const AircraftTablePoint point = table.points[index];
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      errors.push_back(std::string(context) + " points must be finite");
      break;
    }
    if (point.x < table.validity_min || point.x > table.validity_max) {
      errors.push_back(std::string(context) + " points must be inside the validity range");
      break;
    }
    if (index > 0 && point.x <= table.points[index - 1].x) {
      errors.push_back(std::string(context) + " points must be strictly increasing");
      break;
    }
  }
}

[[nodiscard]] bool has_table(const std::vector<AircraftTable1d>& tables,
                             std::string_view id) noexcept {
  return std::any_of(tables.begin(), tables.end(), [&](const AircraftTable1d& table) {
    return table.id == id;
  });
}

void require_table_ids(const std::vector<AircraftTable1d>& tables,
                       std::initializer_list<std::string_view> ids,
                       std::string_view context,
                       std::vector<std::string>& errors) {
  for (std::string_view id : ids) {
    if (!has_table(tables, id)) {
      errors.push_back(std::string(context) + " missing required table " + std::string(id));
    }
  }
}

[[nodiscard]] const AircraftFuelStation* find_fuel_station(
    const std::vector<AircraftFuelStation>& stations,
    std::string_view station_id) noexcept {
  const auto found = std::find_if(stations.begin(), stations.end(), [&](const auto& station) {
    return station.id == station_id;
  });
  return found == stations.end() ? nullptr : &*found;
}

[[nodiscard]] const AircraftPayloadStation* find_payload_station(
    const std::vector<AircraftPayloadStation>& stations,
    std::string_view station_id) noexcept {
  const auto found = std::find_if(stations.begin(), stations.end(), [&](const auto& station) {
    return station.id == station_id;
  });
  return found == stations.end() ? nullptr : &*found;
}

void add_point_mass_inertia(AircraftInertiaTensor& inertia, double mass_kg, Vector3d offset_m) {
  inertia.ixx += mass_kg * (offset_m.y * offset_m.y + offset_m.z * offset_m.z);
  inertia.iyy += mass_kg * (offset_m.x * offset_m.x + offset_m.z * offset_m.z);
  inertia.izz += mass_kg * (offset_m.x * offset_m.x + offset_m.y * offset_m.y);
  inertia.ixy -= mass_kg * offset_m.x * offset_m.y;
  inertia.ixz -= mass_kg * offset_m.x * offset_m.z;
  inertia.iyz -= mass_kg * offset_m.y * offset_m.z;
}

[[nodiscard]] double interpolate_limit(const std::vector<AircraftCgEnvelopePoint>& envelope,
                                       double mass_kg,
                                       bool forward_limit) {
  if (mass_kg < envelope.front().mass_kg.value || mass_kg > envelope.back().mass_kg.value) {
    throw std::invalid_argument("aircraft mass is outside the CG envelope mass range");
  }
  for (std::size_t index = 1; index < envelope.size(); ++index) {
    const double low_mass = envelope[index - 1].mass_kg.value;
    const double high_mass = envelope[index].mass_kg.value;
    if (mass_kg > high_mass) {
      continue;
    }
    const double t = high_mass == low_mass ? 0.0 : (mass_kg - low_mass) / (high_mass - low_mass);
    const double low_x = forward_limit
                             ? envelope[index - 1].forward_limit_body_m.value.x
                             : envelope[index - 1].aft_limit_body_m.value.x;
    const double high_x = forward_limit
                              ? envelope[index].forward_limit_body_m.value.x
                              : envelope[index].aft_limit_body_m.value.x;
    return low_x + (high_x - low_x) * t;
  }
  return forward_limit ? envelope.back().forward_limit_body_m.value.x
                       : envelope.back().aft_limit_body_m.value.x;
}

[[nodiscard]] bool cg_in_envelope(const std::vector<AircraftCgEnvelopePoint>& envelope,
                                  double mass_kg,
                                  Vector3d cg_body_m) {
  const double forward_x = interpolate_limit(envelope, mass_kg, true);
  const double aft_x = interpolate_limit(envelope, mass_kg, false);
  const double min_x = std::min(forward_x, aft_x);
  const double max_x = std::max(forward_x, aft_x);
  return cg_body_m.x >= min_x && cg_body_m.x <= max_x;
}

[[nodiscard]] std::string first_error(const std::vector<std::string>& errors) {
  return errors.empty() ? std::string{} : errors.front();
}

[[nodiscard]] const std::vector<AircraftTable1d>& table_group(
    const AircraftConfiguration& configuration,
    std::string_view table_id) noexcept {
  if (has_table(configuration.aerodynamics.tables, table_id)) {
    return configuration.aerodynamics.tables;
  }
  if (has_table(configuration.engine.tables, table_id)) {
    return configuration.engine.tables;
  }
  return configuration.propeller.tables;
}

[[nodiscard]] double checked_unit_interval(double value, std::string_view field_name) {
  if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
    throw std::invalid_argument(std::string(field_name) + " must be in [0, 1]");
  }
  return value;
}

} // namespace

std::filesystem::path default_aircraft_config_path() {
#ifdef FLYING_CORE_SIM_AIRCRAFT_CONFIG_DIR
  return std::filesystem::path{FLYING_CORE_SIM_AIRCRAFT_CONFIG_DIR} /
         "flying_trainer_one" / "aircraft-config.json";
#else
  return std::filesystem::path{"core_sim"} / "aircraft" / "flying_trainer_one" /
         "aircraft-config.json";
#endif
}

AircraftConfigurationLoadResult load_aircraft_configuration(const std::filesystem::path& path) {
  AircraftConfigurationLoadResult result{};
  try {
    result.configuration = parse_configuration(JsonParser{read_text_file(path)}.parse());
    result.errors = validate_aircraft_configuration(result.configuration);
    result.loaded = result.errors.empty();
  } catch (const std::exception& error) {
    result.errors.push_back(error.what());
  }
  return result;
}

AircraftConfigurationCompatibility check_aircraft_configuration_compatibility(
    std::string_view schema_version) {
  AircraftConfigurationCompatibility compatibility{};
  compatibility.target_schema_version = std::string{kAircraftConfigSchemaVersion};
  if (schema_version == kAircraftConfigSchemaVersion) {
    compatibility.supported = true;
    return compatibility;
  }
  if (schema_version == kAircraftConfigLegacySchemaVersion) {
    compatibility.supported = true;
    compatibility.requires_migration = true;
    return compatibility;
  }
  compatibility.errors.push_back("unsupported aircraft schemaVersion: " +
                                 std::string(schema_version));
  return compatibility;
}

AircraftConfiguration migrate_aircraft_configuration(AircraftConfiguration configuration) {
  const AircraftConfigurationCompatibility compatibility =
      check_aircraft_configuration_compatibility(configuration.schema_version);
  if (!compatibility.supported) {
    throw std::invalid_argument(compatibility.errors.front());
  }
  if (compatibility.requires_migration) {
    configuration.schema_version = std::string{kAircraftConfigSchemaVersion};
    if (configuration.validation_status.empty()) {
      configuration.validation_status = std::string{kAircraftConfigUnvalidatedStatus};
    }
    if (configuration.validation_suite_status.empty()) {
      configuration.validation_suite_status = "not_run";
    }
  }
  return configuration;
}

std::vector<std::string> validate_aircraft_configuration(
    const AircraftConfiguration& configuration) {
  std::vector<std::string> errors;
  const AircraftConfigurationCompatibility compatibility =
      check_aircraft_configuration_compatibility(configuration.schema_version);
  if (!compatibility.supported || compatibility.requires_migration) {
    errors.push_back("aircraft schemaVersion must be flying.aircraft-config.v1");
  }
  if (configuration.identity.backend.empty() ||
      configuration.identity.model_name.empty() ||
      configuration.identity.model_version.empty() ||
      configuration.identity.data_root.empty() ||
      configuration.identity.source_license.empty()) {
    errors.push_back("aircraft identity must include backend, model, version, data root, and license");
  }
  if (configuration.display_name.empty()) {
    errors.push_back("aircraft display name must be populated");
  }
  if (configuration.validation_status == kAircraftConfigUnvalidatedStatus) {
    if (configuration.validation_suite_status != "not_run") {
      errors.push_back("unvalidated aircraft validation suite status must be not_run");
    }
  } else if (configuration.validation_status == kAircraftConfigFaithfulStatus) {
    if (configuration.validation_suite_status != "passed") {
      errors.push_back("faithful aircraft model requires a passed aircraft validation suite");
    }
    if (configuration.validation_approved_references.empty()) {
      errors.push_back("faithful aircraft model requires approved validation references");
    }
  } else {
    errors.push_back("aircraft validation status must be unvalidated or faithful");
  }
  if (configuration.validation_suite.empty()) {
    errors.push_back("aircraft validation suite requirement must be recorded");
  }
  if (configuration.license_spdx.empty() || configuration.license_notice.empty()) {
    errors.push_back("aircraft license metadata must be recorded");
  }
  if (configuration.provenance_summary.empty()) {
    errors.push_back("aircraft provenance summary must be recorded");
  }

  std::set<std::string> known_sources;
  for (const AircraftSourceReference& source : configuration.source_references) {
    if (source.id.empty() || source.title.empty() || source.document.empty() ||
        source.license.empty() || source.permitted_use.empty() ||
        source.provenance.empty() || source.used_for.empty()) {
      errors.push_back("aircraft source references must include id, title, document, license, permitted use, provenance, and used_for");
    }
    if (!is_known_confidence(source.confidence)) {
      errors.push_back("aircraft source reference confidence must be low, medium, or high");
    }
    if (!known_sources.insert(source.id).second) {
      errors.push_back("aircraft source reference ids must be unique");
    }
  }
  if (known_sources.empty()) {
    errors.push_back("aircraft source references must not be empty");
  }
  if (configuration.validation_status == kAircraftConfigFaithfulStatus) {
    for (const std::string& ref_id : configuration.validation_approved_references) {
      const auto source = std::find_if(configuration.source_references.begin(),
                                       configuration.source_references.end(),
                                       [&](const AircraftSourceReference& candidate) {
                                         return candidate.id == ref_id;
                                       });
      if (source == configuration.source_references.end()) {
        errors.push_back("faithful aircraft validation reference is unknown: " + ref_id);
      } else if (!source->approved_for_faithful_claim ||
                 std::find(source->used_for.begin(), source->used_for.end(), "validation") ==
                     source->used_for.end()) {
        errors.push_back("faithful aircraft validation reference is not approved: " + ref_id);
      }
    }
  }

  const AircraftGeometryModel& geometry = configuration.geometry;
  if (geometry.coordinate_frame != "body-x-forward-y-right-z-down-si") {
    errors.push_back("aircraft geometry coordinate frame must be body-x-forward-y-right-z-down-si");
  }
  validate_scalar(geometry.wing_area_m2, "square_meter", known_sources, "geometry.wingAreaM2", errors);
  validate_scalar(geometry.wingspan_m, "meter", known_sources, "geometry.wingspanM", errors);
  validate_scalar(geometry.mean_aerodynamic_chord_m, "meter", known_sources,
                  "geometry.meanAerodynamicChordM", errors);
  validate_scalar(geometry.fuselage_length_m, "meter", known_sources,
                  "geometry.fuselageLengthM", errors);
  validate_scalar(geometry.horizontal_tail_area_m2, "square_meter", known_sources,
                  "geometry.horizontalTailAreaM2", errors);
  validate_scalar(geometry.horizontal_tail_arm_m, "meter", known_sources,
                  "geometry.horizontalTailArmM", errors);
  validate_scalar(geometry.vertical_tail_area_m2, "square_meter", known_sources,
                  "geometry.verticalTailAreaM2", errors);
  validate_scalar(geometry.vertical_tail_arm_m, "meter", known_sources,
                  "geometry.verticalTailArmM", errors);
  validate_scalar(geometry.propeller_ground_clearance_m, "meter", known_sources,
                  "geometry.propellerGroundClearanceM", errors);
  validate_vector_value(geometry.aerodynamic_reference_point_body_m, "meter", known_sources,
                        "geometry.aerodynamicReferencePointBodyM", errors);
  validate_vector_value(geometry.visual_reference_point_body_m, "meter", known_sources,
                        "geometry.visualReferencePointBodyM", errors);

  const AircraftMassBalanceModel& mass = configuration.mass_balance;
  if (mass.inertia_reference.empty()) {
    errors.push_back("massBalance inertia reference must be recorded");
  }
  validate_scalar(mass.empty_mass_kg, "kilogram", known_sources, "massBalance.emptyMassKg", errors);
  validate_scalar(mass.max_takeoff_mass_kg, "kilogram", known_sources,
                  "massBalance.maxTakeoffMassKg", errors);
  validate_vector_value(mass.empty_cg_body_m, "meter", known_sources,
                        "massBalance.emptyCgBodyM", errors);
  validate_inertia_tensor_value(mass.empty_inertia_kg_m2, known_sources,
                                "massBalance.emptyInertiaKgM2", errors);
  if (mass.empty_mass_kg.value >= mass.max_takeoff_mass_kg.value) {
    errors.push_back("massBalance empty mass must be below max takeoff mass");
  }
  if (mass.cg_envelope.size() < 2) {
    errors.push_back("massBalance CG envelope must contain at least two mass breakpoints");
  }
  for (std::size_t index = 0; index < mass.cg_envelope.size(); ++index) {
    const std::string context = "massBalance.cgEnvelope[" + std::to_string(index) + "]";
    validate_scalar(mass.cg_envelope[index].mass_kg, "kilogram", known_sources,
                    context + ".massKg", errors);
    validate_vector_value(mass.cg_envelope[index].forward_limit_body_m, "meter", known_sources,
                          context + ".forwardLimitBodyM", errors);
    validate_vector_value(mass.cg_envelope[index].aft_limit_body_m, "meter", known_sources,
                          context + ".aftLimitBodyM", errors);
    if (index > 0 &&
        mass.cg_envelope[index].mass_kg.value <= mass.cg_envelope[index - 1].mass_kg.value) {
      errors.push_back("massBalance CG envelope masses must be strictly increasing");
    }
  }
  if (mass.fuel_stations.empty()) {
    errors.push_back("massBalance must contain at least one fuel station");
  }
  std::set<std::string> station_ids;
  for (const AircraftFuelStation& station : mass.fuel_stations) {
    const std::string context = "massBalance.fuelStations." + station.id;
    if (station.id.empty() || station.display_name.empty()) {
      errors.push_back("fuel stations must include id and display name");
    }
    if (!station_ids.insert("fuel:" + station.id).second) {
      errors.push_back("fuel station ids must be unique");
    }
    validate_scalar(station.capacity_kg, "kilogram", known_sources,
                    context + ".capacityKg", errors);
    validate_scalar(station.unusable_kg, "kilogram", known_sources,
                    context + ".unusableKg", errors);
    validate_scalar(station.default_quantity_kg, "kilogram", known_sources,
                    context + ".defaultQuantityKg", errors);
    validate_vector_value(station.position_body_m, "meter", known_sources,
                          context + ".positionBodyM", errors);
    if (station.unusable_kg.value > station.capacity_kg.value ||
        station.default_quantity_kg.value > station.capacity_kg.value) {
      errors.push_back(context + " fuel quantities must not exceed capacity");
    }
  }
  if (mass.payload_stations.empty()) {
    errors.push_back("massBalance must contain at least one payload station");
  }
  for (const AircraftPayloadStation& station : mass.payload_stations) {
    const std::string context = "massBalance.payloadStations." + station.id;
    if (station.id.empty() || station.display_name.empty()) {
      errors.push_back("payload stations must include id and display name");
    }
    if (!station_ids.insert("payload:" + station.id).second) {
      errors.push_back("payload station ids must be unique");
    }
    validate_scalar(station.max_mass_kg, "kilogram", known_sources,
                    context + ".maxMassKg", errors);
    validate_scalar(station.default_mass_kg, "kilogram", known_sources,
                    context + ".defaultMassKg", errors);
    validate_vector_value(station.position_body_m, "meter", known_sources,
                          context + ".positionBodyM", errors);
    if (station.default_mass_kg.value > station.max_mass_kg.value) {
      errors.push_back(context + " default payload must not exceed max mass");
    }
  }

  const AircraftAerodynamicModel& aero = configuration.aerodynamics;
  if (aero.coefficient_frame != "body-stability-coefficients") {
    errors.push_back("aerodynamics coefficient frame must be body-stability-coefficients");
  }
  validate_scalar(aero.alpha_min_rad, "radian", known_sources,
                  "aerodynamics.alphaMinRad", errors);
  validate_scalar(aero.alpha_max_rad, "radian", known_sources,
                  "aerodynamics.alphaMaxRad", errors);
  validate_scalar(aero.beta_max_abs_rad, "radian", known_sources,
                  "aerodynamics.betaMaxAbsRad", errors);
  if (aero.alpha_min_rad.value >= aero.alpha_max_rad.value) {
    errors.push_back("aerodynamics alpha range must be ordered");
  }
  for (std::size_t index = 0; index < aero.tables.size(); ++index) {
    validate_table(aero.tables[index], known_sources,
                   "aerodynamics.tables[" + std::to_string(index) + "]", errors);
  }
  require_table_ids(aero.tables,
                    {"cl_alpha", "cd_alpha", "cm_alpha", "cy_beta", "cl_beta",
                     "cn_beta", "cl_aileron", "cm_elevator", "cn_rudder",
                     "cl_flaps", "cd_flaps", "cm_flaps"},
                    "aerodynamics", errors);

  const AircraftEngineModel& engine = configuration.engine;
  if (engine.id.empty() || engine.type.empty()) {
    errors.push_back("engine id and type must be populated");
  }
  validate_scalar(engine.rated_power_w, "watt", known_sources, "engine.ratedPowerW", errors);
  validate_scalar(engine.idle_rpm, "revolution_per_minute", known_sources,
                  "engine.idleRpm", errors);
  validate_scalar(engine.max_rpm, "revolution_per_minute", known_sources,
                  "engine.maxRpm", errors);
  validate_scalar(engine.displacement_m3, "cubic_meter", known_sources,
                  "engine.displacementM3", errors);
  validate_scalar(engine.fuel_density_kg_per_l, "kilogram_per_liter", known_sources,
                  "engine.fuelDensityKgPerL", errors);
  if (engine.idle_rpm.value >= engine.max_rpm.value) {
    errors.push_back("engine idle RPM must be below max RPM");
  }
  for (std::size_t index = 0; index < engine.tables.size(); ++index) {
    validate_table(engine.tables[index], known_sources,
                   "engine.tables[" + std::to_string(index) + "]", errors);
  }
  require_table_ids(engine.tables, {"power_fraction_throttle", "power_fraction_rpm",
                                    "fuel_flow_lph_power_fraction"},
                    "engine", errors);

  const AircraftPropellerModel& propeller = configuration.propeller;
  if (propeller.id.empty() || propeller.type.empty()) {
    errors.push_back("propeller id and type must be populated");
  }
  if (propeller.blade_count <= 0) {
    errors.push_back("propeller blade count must be positive");
  }
  validate_scalar(propeller.diameter_m, "meter", known_sources,
                  "propeller.diameterM", errors);
  validate_scalar(propeller.inertia_kg_m2, "kilogram_meter_squared", known_sources,
                  "propeller.inertiaKgM2", errors);
  validate_scalar(propeller.min_pitch_rad, "radian", known_sources,
                  "propeller.minPitchRad", errors);
  validate_scalar(propeller.max_pitch_rad, "radian", known_sources,
                  "propeller.maxPitchRad", errors);
  if (propeller.min_pitch_rad.value > propeller.max_pitch_rad.value) {
    errors.push_back("propeller pitch range must be ordered");
  }
  for (std::size_t index = 0; index < propeller.tables.size(); ++index) {
    validate_table(propeller.tables[index], known_sources,
                   "propeller.tables[" + std::to_string(index) + "]", errors);
  }
  require_table_ids(propeller.tables, {"thrust_coefficient_advance_ratio",
                                       "power_coefficient_advance_ratio"},
                    "propeller", errors);

  if (configuration.actuators.size() < 4) {
    errors.push_back("actuators must include primary surfaces and flaps");
  }
  std::set<std::string> actuator_surfaces;
  for (const AircraftActuatorModel& actuator : configuration.actuators) {
    const std::string context = "actuators." + actuator.id;
    if (actuator.id.empty() || actuator.surface.empty() || actuator.command.empty()) {
      errors.push_back("actuators must include id, surface, and command");
    }
    actuator_surfaces.insert(actuator.surface);
    validate_scalar(actuator.min_deflection_rad, "radian", known_sources,
                    context + ".minDeflectionRad", errors);
    validate_scalar(actuator.max_deflection_rad, "radian", known_sources,
                    context + ".maxDeflectionRad", errors);
    validate_scalar(actuator.slew_rate_radps, "radian_per_second", known_sources,
                    context + ".slewRateRadps", errors);
    validate_scalar(actuator.time_constant_s, "second", known_sources,
                    context + ".timeConstantS", errors);
    if (actuator.min_deflection_rad.value > actuator.max_deflection_rad.value) {
      errors.push_back(context + " deflection range must be ordered");
    }
  }
  for (std::string_view surface : {"aileron", "elevator", "rudder", "flaps"}) {
    if (actuator_surfaces.find(std::string(surface)) == actuator_surfaces.end()) {
      errors.push_back("actuators missing required surface " + std::string(surface));
    }
  }

  if (configuration.landing_gear.size() < 3) {
    errors.push_back("landing gear must include nose, left main, and right main contacts");
  }
  for (const AircraftLandingGearContact& gear : configuration.landing_gear) {
    const std::string context = "landingGear." + gear.id;
    if (gear.id.empty() || gear.type.empty() || gear.brake_group.empty()) {
      errors.push_back("landing gear contacts must include id, type, and brake group");
    }
    validate_vector_value(gear.position_body_m, "meter", known_sources,
                          context + ".positionBodyM", errors);
    validate_scalar(gear.static_friction, "coefficient", known_sources,
                    context + ".staticFriction", errors);
    validate_scalar(gear.dynamic_friction, "coefficient", known_sources,
                    context + ".dynamicFriction", errors);
    validate_scalar(gear.rolling_friction, "coefficient", known_sources,
                    context + ".rollingFriction", errors);
    validate_scalar(gear.spring_coefficient_n_per_m, "newton_per_meter", known_sources,
                    context + ".springCoefficientNPerM", errors);
    validate_scalar(gear.damping_coefficient_n_s_per_m, "newton_second_per_meter",
                    known_sources, context + ".dampingCoefficientNSPerM", errors);
    validate_scalar(gear.max_steer_rad, "radian", known_sources,
                    context + ".maxSteerRad", errors);
  }

  if (configuration.brakes.size() < 2) {
    errors.push_back("brakes must include left and right brake models");
  }
  for (const AircraftBrakeModel& brake : configuration.brakes) {
    const std::string context = "brakes." + brake.id;
    if (brake.id.empty() || brake.wheel.empty() || brake.brake_group.empty()) {
      errors.push_back("brakes must include id, wheel, and brake group");
    }
    validate_scalar(brake.max_torque_nm, "newton_meter", known_sources,
                    context + ".maxTorqueNm", errors);
    validate_scalar(brake.response_time_s, "second", known_sources,
                    context + ".responseTimeS", errors);
    validate_scalar(brake.parking_brake_hold_norm, "normalized", known_sources,
                    context + ".parkingBrakeHoldNorm", errors);
  }

  return errors;
}

AircraftLoadout make_default_aircraft_loadout(const AircraftConfiguration& configuration) {
  AircraftLoadout loadout{};
  for (const AircraftFuelStation& station : configuration.mass_balance.fuel_stations) {
    loadout.fuel.push_back({station.id, station.default_quantity_kg.value});
  }
  for (const AircraftPayloadStation& station : configuration.mass_balance.payload_stations) {
    loadout.payload.push_back({station.id, station.default_mass_kg.value});
  }
  return loadout;
}

AircraftMassBalanceState compute_aircraft_mass_balance(
    const AircraftConfiguration& configuration,
    const AircraftLoadout& loadout) {
  const std::vector<std::string> errors = validate_aircraft_configuration(configuration);
  if (!errors.empty()) {
    throw std::invalid_argument("aircraft configuration is invalid: " + first_error(errors));
  }

  const AircraftMassBalanceModel& model = configuration.mass_balance;
  double total_mass_kg = model.empty_mass_kg.value;
  double fuel_mass_kg = 0.0;
  double payload_mass_kg = 0.0;
  Vector3d weighted_position = model.empty_cg_body_m.value * model.empty_mass_kg.value;
  std::vector<std::pair<double, Vector3d>> point_masses;
  point_masses.push_back({model.empty_mass_kg.value, model.empty_cg_body_m.value});

  std::set<std::string> seen_fuel;
  for (const AircraftLoadout::StationQuantity& quantity : loadout.fuel) {
    const AircraftFuelStation* station =
        find_fuel_station(model.fuel_stations, quantity.station_id);
    if (station == nullptr) {
      throw std::invalid_argument("unknown fuel station in aircraft loadout: " +
                                  quantity.station_id);
    }
    if (!seen_fuel.insert(quantity.station_id).second) {
      throw std::invalid_argument("duplicate fuel station in aircraft loadout: " +
                                  quantity.station_id);
    }
    if (!std::isfinite(quantity.mass_kg) ||
        quantity.mass_kg < 0.0 ||
        quantity.mass_kg > station->capacity_kg.value) {
      throw std::invalid_argument("fuel station mass is outside validated capacity: " +
                                  quantity.station_id);
    }
    total_mass_kg += quantity.mass_kg;
    fuel_mass_kg += quantity.mass_kg;
    weighted_position += station->position_body_m.value * quantity.mass_kg;
    point_masses.push_back({quantity.mass_kg, station->position_body_m.value});
  }

  std::set<std::string> seen_payload;
  for (const AircraftLoadout::StationQuantity& quantity : loadout.payload) {
    const AircraftPayloadStation* station =
        find_payload_station(model.payload_stations, quantity.station_id);
    if (station == nullptr) {
      throw std::invalid_argument("unknown payload station in aircraft loadout: " +
                                  quantity.station_id);
    }
    if (!seen_payload.insert(quantity.station_id).second) {
      throw std::invalid_argument("duplicate payload station in aircraft loadout: " +
                                  quantity.station_id);
    }
    if (!std::isfinite(quantity.mass_kg) ||
        quantity.mass_kg < 0.0 ||
        quantity.mass_kg > station->max_mass_kg.value) {
      throw std::invalid_argument("payload station mass is outside validated capacity: " +
                                  quantity.station_id);
    }
    total_mass_kg += quantity.mass_kg;
    payload_mass_kg += quantity.mass_kg;
    weighted_position += station->position_body_m.value * quantity.mass_kg;
    point_masses.push_back({quantity.mass_kg, station->position_body_m.value});
  }

  if (total_mass_kg > model.max_takeoff_mass_kg.value) {
    throw std::invalid_argument("aircraft loadout exceeds max takeoff mass");
  }

  const Vector3d cg_body_m = weighted_position / total_mass_kg;
  const bool inside_envelope = cg_in_envelope(model.cg_envelope, total_mass_kg, cg_body_m);
  if (!inside_envelope) {
    throw std::invalid_argument("aircraft loadout center of gravity is outside the envelope");
  }

  AircraftInertiaTensor inertia = model.empty_inertia_kg_m2.value;
  for (const auto& point_mass : point_masses) {
    add_point_mass_inertia(inertia, point_mass.first, point_mass.second - cg_body_m);
  }

  return {
    total_mass_kg,
    fuel_mass_kg,
    payload_mass_kg,
    cg_body_m,
    inertia,
    true,
  };
}

void apply_aircraft_loadout(CoreSimulator& simulator,
                            const AircraftConfiguration& configuration,
                            const AircraftLoadout& loadout) {
  simulator.set_aircraft_mass_balance(compute_aircraft_mass_balance(configuration, loadout));
}

double evaluate_aircraft_table(const AircraftTable1d& table, double x) {
  if (!std::isfinite(x)) {
    throw std::invalid_argument("aircraft table input must be finite");
  }
  if (table.points.size() < 2 || x < table.validity_min || x > table.validity_max) {
    throw std::invalid_argument("aircraft table input is outside the validated range: " +
                                table.id);
  }
  if (x <= table.points.front().x) {
    return table.points.front().y;
  }
  for (std::size_t index = 1; index < table.points.size(); ++index) {
    if (x <= table.points[index].x) {
      const double low_x = table.points[index - 1].x;
      const double high_x = table.points[index].x;
      const double t = (x - low_x) / (high_x - low_x);
      return table.points[index - 1].y +
             (table.points[index].y - table.points[index - 1].y) * t;
    }
  }
  return table.points.back().y;
}

const AircraftTable1d& require_aircraft_table(const AircraftConfiguration& configuration,
                                              std::string_view table_id) {
  const std::vector<AircraftTable1d>& tables = table_group(configuration, table_id);
  const auto found = std::find_if(tables.begin(), tables.end(), [&](const AircraftTable1d& table) {
    return table.id == table_id;
  });
  if (found == tables.end()) {
    throw std::invalid_argument("aircraft table is not present: " + std::string(table_id));
  }
  return *found;
}

AircraftAerodynamicCoefficients evaluate_aerodynamic_coefficients(
    const AircraftConfiguration& configuration,
    const AircraftControlInputSample& controls,
    double angle_of_attack_rad,
    double sideslip_rad) {
  const std::vector<std::string> errors = validate_aircraft_configuration(configuration);
  if (!errors.empty()) {
    throw std::invalid_argument("aircraft configuration is invalid: " + first_error(errors));
  }
  validate_aircraft_controls(controls);
  if (!std::isfinite(angle_of_attack_rad) ||
      angle_of_attack_rad < configuration.aerodynamics.alpha_min_rad.value ||
      angle_of_attack_rad > configuration.aerodynamics.alpha_max_rad.value) {
    throw std::invalid_argument("angle of attack is outside the configured aerodynamic range");
  }
  if (!std::isfinite(sideslip_rad) ||
      std::abs(sideslip_rad) > configuration.aerodynamics.beta_max_abs_rad.value) {
    throw std::invalid_argument("sideslip is outside the configured aerodynamic range");
  }

  AircraftAerodynamicCoefficients coefficients{};
  coefficients.lift =
      evaluate_aircraft_table(require_aircraft_table(configuration, "cl_alpha"),
                              angle_of_attack_rad) +
      evaluate_aircraft_table(require_aircraft_table(configuration, "cl_flaps"),
                              controls.flaps_norm);
  coefficients.drag =
      evaluate_aircraft_table(require_aircraft_table(configuration, "cd_alpha"),
                              angle_of_attack_rad) +
      evaluate_aircraft_table(require_aircraft_table(configuration, "cd_flaps"),
                              controls.flaps_norm);
  coefficients.pitch_moment =
      evaluate_aircraft_table(require_aircraft_table(configuration, "cm_alpha"),
                              angle_of_attack_rad) +
      evaluate_aircraft_table(require_aircraft_table(configuration, "cm_elevator"),
                              controls.elevator_norm) +
      evaluate_aircraft_table(require_aircraft_table(configuration, "cm_flaps"),
                              controls.flaps_norm);
  coefficients.side_force =
      evaluate_aircraft_table(require_aircraft_table(configuration, "cy_beta"), sideslip_rad);
  coefficients.roll_moment =
      evaluate_aircraft_table(require_aircraft_table(configuration, "cl_beta"), sideslip_rad) +
      evaluate_aircraft_table(require_aircraft_table(configuration, "cl_aileron"),
                              controls.aileron_norm);
  coefficients.yaw_moment =
      evaluate_aircraft_table(require_aircraft_table(configuration, "cn_beta"), sideslip_rad) +
      evaluate_aircraft_table(require_aircraft_table(configuration, "cn_rudder"),
                              controls.rudder_norm);
  return coefficients;
}

double evaluate_engine_power_w(const AircraftConfiguration& configuration,
                               double throttle_norm,
                               double rpm) {
  const std::vector<std::string> errors = validate_aircraft_configuration(configuration);
  if (!errors.empty()) {
    throw std::invalid_argument("aircraft configuration is invalid: " + first_error(errors));
  }
  checked_unit_interval(throttle_norm, "throttle_norm");
  if (!std::isfinite(rpm) ||
      rpm < configuration.engine.idle_rpm.value ||
      rpm > configuration.engine.max_rpm.value) {
    throw std::invalid_argument("engine RPM is outside the configured map range");
  }
  return configuration.engine.rated_power_w.value *
         evaluate_aircraft_table(require_aircraft_table(configuration, "power_fraction_throttle"),
                                 throttle_norm) *
         evaluate_aircraft_table(require_aircraft_table(configuration, "power_fraction_rpm"), rpm);
}

AircraftPropellerCoefficients evaluate_propeller_coefficients(
    const AircraftConfiguration& configuration,
    double advance_ratio) {
  const std::vector<std::string> errors = validate_aircraft_configuration(configuration);
  if (!errors.empty()) {
    throw std::invalid_argument("aircraft configuration is invalid: " + first_error(errors));
  }
  return {
    evaluate_aircraft_table(
        require_aircraft_table(configuration, "thrust_coefficient_advance_ratio"),
        advance_ratio),
    evaluate_aircraft_table(
        require_aircraft_table(configuration, "power_coefficient_advance_ratio"),
        advance_ratio),
  };
}

} // namespace flying::core_sim
