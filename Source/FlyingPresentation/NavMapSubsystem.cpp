#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace flying::presentation {

enum class NavMapLayer {
  airports,
  runways,
  obstacles,
  airspaces,
};

struct NavMapView {
  double center_east_m = 25'000.0;
  double center_north_m = 25'000.0;
  double zoom = 1.0;
};

struct NavMapLayerToggle {
  NavMapLayer layer = NavMapLayer::airports;
  bool visible = true;
};

struct NavMapPackageStatus {
  bool opened = false;
  bool runtime_network_required = true;
  std::string attribution;
  std::map<std::string, std::size_t> renderable_layer_counts;
  NavMapView view;
  std::vector<NavMapLayerToggle> layer_toggles;
};

namespace {

constexpr std::array<std::string_view, 4> kMandatoryToggleLayers = {
  "airports",
  "runways",
  "obstacles",
  "airspaces",
};

constexpr std::array<std::string_view, 6> kRequiredRenderableLayers = {
  "zabaged-base",
  "geonames-labels",
  "airports",
  "runways",
  "obstacles",
  "airspaces",
};

struct JsonValue {
  using Object = std::map<std::string, JsonValue>;
  using Array = std::vector<JsonValue>;
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value;
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  [[nodiscard]] JsonValue parse() {
    JsonValue value = parse_value();
    skip_ws();
    if (pos_ != text_.size()) {
      throw std::runtime_error("trailing content after JSON document");
    }
    return value;
  }

 private:
  void skip_ws() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' ||
            text_[pos_] == '\t')) {
      ++pos_;
    }
  }

  [[nodiscard]] bool consume(char expected) {
    skip_ws();
    if (pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  void expect(char expected) {
    if (!consume(expected)) {
      throw std::runtime_error("unexpected JSON token");
    }
  }

  [[nodiscard]] JsonValue parse_value() {
    skip_ws();
    if (pos_ >= text_.size()) {
      throw std::runtime_error("unexpected end of JSON document");
    }
    const char ch = text_[pos_];
    if (ch == '{') {
      return JsonValue{parse_object()};
    }
    if (ch == '[') {
      return JsonValue{parse_array()};
    }
    if (ch == '"') {
      return JsonValue{parse_string()};
    }
    if (text_.substr(pos_, 4) == "true") {
      pos_ += 4;
      return JsonValue{true};
    }
    if (text_.substr(pos_, 5) == "false") {
      pos_ += 5;
      return JsonValue{false};
    }
    if (text_.substr(pos_, 4) == "null") {
      pos_ += 4;
      return JsonValue{nullptr};
    }
    return JsonValue{parse_number()};
  }

  [[nodiscard]] JsonValue::Object parse_object() {
    JsonValue::Object object;
    expect('{');
    if (consume('}')) {
      return object;
    }
    while (true) {
      const std::string key = parse_string();
      expect(':');
      object.emplace(key, parse_value());
      if (consume('}')) {
        return object;
      }
      expect(',');
    }
  }

  [[nodiscard]] JsonValue::Array parse_array() {
    JsonValue::Array array;
    expect('[');
    if (consume(']')) {
      return array;
    }
    while (true) {
      array.push_back(parse_value());
      if (consume(']')) {
        return array;
      }
      expect(',');
    }
  }

  [[nodiscard]] std::string parse_string() {
    expect('"');
    std::string output;
    while (pos_ < text_.size()) {
      const char ch = text_[pos_++];
      if (ch == '"') {
        return output;
      }
      if (ch != '\\') {
        output.push_back(ch);
        continue;
      }
      if (pos_ >= text_.size()) {
        throw std::runtime_error("invalid JSON escape");
      }
      const char escaped = text_[pos_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          output.push_back(escaped);
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

  [[nodiscard]] double parse_number() {
    const std::size_t begin = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      ++pos_;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    return std::stod(std::string{text_.substr(begin, pos_ - begin)});
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

[[nodiscard]] const JsonValue::Object& as_object(const JsonValue& value, std::string_view field) {
  const auto* object = std::get_if<JsonValue::Object>(&value.value);
  if (object == nullptr) {
    throw std::runtime_error(std::string(field) + " must be a JSON object");
  }
  return *object;
}

[[nodiscard]] const JsonValue::Array& as_array(const JsonValue& value, std::string_view field) {
  const auto* array = std::get_if<JsonValue::Array>(&value.value);
  if (array == nullptr) {
    throw std::runtime_error(std::string(field) + " must be a JSON array");
  }
  return *array;
}

[[nodiscard]] const JsonValue& require_field(
    const JsonValue::Object& object,
    std::string_view field) {
  const auto found = object.find(std::string(field));
  if (found == object.end()) {
    throw std::runtime_error("offline navigation map JSON missing required field: " + std::string(field));
  }
  return found->second;
}

[[nodiscard]] bool bool_field(const JsonValue::Object& object, std::string_view field) {
  const auto* value = std::get_if<bool>(&require_field(object, field).value);
  if (value == nullptr) {
    throw std::runtime_error("offline navigation map JSON field must be boolean");
  }
  return *value;
}

[[nodiscard]] std::string string_field(const JsonValue::Object& object, std::string_view field) {
  const auto* value = std::get_if<std::string>(&require_field(object, field).value);
  if (value == nullptr) {
    throw std::runtime_error("offline navigation map JSON field must be string");
  }
  return *value;
}

[[nodiscard]] bool contains_remote_reference(std::string_view text) {
  return text.find("://") != std::string_view::npos ||
         text.find("access_token") != std::string_view::npos ||
         text.find("api_key") != std::string_view::npos ||
         text.find("mapbox") != std::string_view::npos;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("offline navigation map file is missing");
  }
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::vector<unsigned char> read_binary_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("offline navigation map tile archive is missing");
  }
  return std::vector<unsigned char>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

[[nodiscard]] std::uint64_t read_uint64_le(
    const std::vector<unsigned char>& bytes,
    std::size_t offset) {
  if (offset + 8U > bytes.size()) {
    throw std::runtime_error("PMTiles header is truncated");
  }
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
  }
  return value;
}

[[nodiscard]] JsonValue load_pmtiles_payload(const std::filesystem::path& path) {
  const std::vector<unsigned char> archive = read_binary_file(path);
  if (archive.size() <= 127U || archive[0] != 'P' || archive[1] != 'M' ||
      archive[2] != 'T' || archive[3] != 'i' || archive[4] != 'l' ||
      archive[5] != 'e' || archive[6] != 's' || archive[7] < 3U) {
    throw std::runtime_error("offline navigation map tile archive is not local PMTiles");
  }
  const std::uint64_t tile_data_offset = read_uint64_le(archive, 56U);
  const std::uint64_t tile_data_length = read_uint64_le(archive, 64U);
  if (tile_data_offset < 127U || tile_data_length == 0U ||
      tile_data_offset + tile_data_length > archive.size()) {
    throw std::runtime_error("offline navigation map PMTiles payload is invalid");
  }
  const auto begin = archive.begin() + static_cast<std::ptrdiff_t>(tile_data_offset);
  const std::string payload(begin, begin + static_cast<std::ptrdiff_t>(tile_data_length));
  return JsonParser(payload).parse();
}

[[nodiscard]] NavMapLayer layer_from_id(std::string_view layer_id) {
  if (layer_id == "airports") {
    return NavMapLayer::airports;
  }
  if (layer_id == "runways") {
    return NavMapLayer::runways;
  }
  if (layer_id == "obstacles") {
    return NavMapLayer::obstacles;
  }
  if (layer_id == "airspaces") {
    return NavMapLayer::airspaces;
  }
  throw std::runtime_error("unknown offline navigation map toggle layer");
}

void require_offline_runtime(const JsonValue::Object& object, std::string_view field) {
  const JsonValue::Object& runtime = as_object(require_field(object, field), field);
  if (bool_field(runtime, "runtimeNetworkRequired")) {
    throw std::runtime_error("offline navigation map requires runtime networking");
  }
  if (!as_array(require_field(runtime, "externalMapApis"), "externalMapApis").empty() ||
      !as_array(require_field(runtime, "remoteTileServerUrls"), "remoteTileServerUrls").empty()) {
    throw std::runtime_error("offline navigation map contains remote map dependencies");
  }
}

[[nodiscard]] std::map<std::string, std::size_t> load_renderable_layer_counts(
    const JsonValue::Object& tile_payload) {
  if (bool_field(tile_payload, "runtimeNetworkRequired")) {
    throw std::runtime_error("offline navigation map tile payload requires networking");
  }
  const JsonValue::Object& layers = as_object(require_field(tile_payload, "layers"), "layers");
  std::map<std::string, std::size_t> counts;
  for (const std::string_view layer_id : kRequiredRenderableLayers) {
    const JsonValue::Object& layer =
        as_object(require_field(layers, layer_id), layer_id);
    const char* collection = layer_id == std::string_view("geonames-labels") ? "labels" : "features";
    const std::size_t count = as_array(require_field(layer, collection), collection).size();
    if (count == 0U) {
      throw std::runtime_error("offline navigation map layer has no renderable data");
    }
    counts.emplace(std::string(layer_id), count);
  }
  return counts;
}

}  // namespace

class NavMapSubsystem {
 public:
  [[nodiscard]] NavMapPackageStatus open_offline_package(
      const std::filesystem::path& manifest_path,
      const std::filesystem::path& tile_archive_path) const {
    const std::string manifest = read_text_file(manifest_path);
    if (contains_remote_reference(manifest)) {
      throw std::runtime_error("offline navigation map manifest contains a remote reference");
    }
    const JsonValue manifest_document = JsonParser(manifest).parse();
    const JsonValue::Object& manifest_root = as_object(manifest_document, "manifest");
    require_offline_runtime(manifest_root, "runtimeDependencies");

    const std::filesystem::path style_path =
        manifest_path.parent_path() / string_field(as_object(require_field(manifest_root, "style"), "style"), "path");
    const std::string style = read_text_file(style_path);
    if (contains_remote_reference(style)) {
      throw std::runtime_error("offline navigation map style contains a remote reference");
    }
    const JsonValue style_document = JsonParser(style).parse();
    const JsonValue::Object& style_root = as_object(style_document, "style");
    if (bool_field(style_root, "runtimeNetworkRequired") ||
        !as_array(require_field(style_root, "externalMapApis"), "externalMapApis").empty() ||
        !as_array(require_field(style_root, "remoteTileServerUrls"), "remoteTileServerUrls").empty()) {
      throw std::runtime_error("offline navigation map style requires networking");
    }

    const JsonValue tile_payload_document = load_pmtiles_payload(tile_archive_path);
    const JsonValue::Object& tile_payload =
        as_object(tile_payload_document, "tile payload");

    NavMapPackageStatus status;
    status.opened = true;
    status.runtime_network_required = false;
    status.attribution = "CUZK CC BY 4.0 and approved Flying project fixtures";
    status.renderable_layer_counts = load_renderable_layer_counts(tile_payload);
    for (const std::string_view layer_id : kMandatoryToggleLayers) {
      status.layer_toggles.push_back({layer_from_id(layer_id), true});
    }
    return status;
  }

  void set_layer_visible(NavMapPackageStatus& status, NavMapLayer layer, bool visible) const {
    auto existing = std::find_if(
        status.layer_toggles.begin(),
        status.layer_toggles.end(),
        [layer](const NavMapLayerToggle& toggle) { return toggle.layer == layer; });
    if (existing == status.layer_toggles.end()) {
      status.layer_toggles.push_back({layer, visible});
    } else {
      existing->visible = visible;
    }
  }

  [[nodiscard]] bool is_layer_visible(const NavMapPackageStatus& status, NavMapLayer layer) const {
    const auto existing = std::find_if(
        status.layer_toggles.begin(),
        status.layer_toggles.end(),
        [layer](const NavMapLayerToggle& toggle) { return toggle.layer == layer; });
    return existing != status.layer_toggles.end() && existing->visible;
  }

  void pan_by(NavMapView& view, double east_m, double north_m) const noexcept {
    view.center_east_m += east_m;
    view.center_north_m += north_m;
  }
};

}  // namespace flying::presentation

#ifdef FLYING_NAV_MAP_SUBSYSTEM_SELF_TEST
#include <iostream>

int main() {
  try {
    flying::presentation::NavMapSubsystem subsystem;
    auto status = subsystem.open_offline_package(
        "Data/Map/SliceManifest.json",
        "Data/Map/Slice.pmtiles");
    if (!status.opened || status.runtime_network_required ||
        status.renderable_layer_counts.size() != 6U) {
      return 1;
    }
    flying::presentation::NavMapView view = status.view;
    subsystem.pan_by(view, 1200.0, -300.0);
    if (view.center_east_m != 26200.0 || view.center_north_m != 24700.0) {
      return 2;
    }
    subsystem.set_layer_visible(status, flying::presentation::NavMapLayer::runways, false);
    if (subsystem.is_layer_visible(status, flying::presentation::NavMapLayer::runways)) {
      return 3;
    }
    subsystem.set_layer_visible(status, flying::presentation::NavMapLayer::runways, true);
    subsystem.set_layer_visible(status, flying::presentation::NavMapLayer::airports, false);
    if (!subsystem.is_layer_visible(status, flying::presentation::NavMapLayer::runways) ||
        subsystem.is_layer_visible(status, flying::presentation::NavMapLayer::airports)) {
      return 4;
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 5;
  }
  return 0;
}
#endif
