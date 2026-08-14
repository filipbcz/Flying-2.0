#include <cassert>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef FLYING_REPO_SOURCE_DIR
#define FLYING_REPO_SOURCE_DIR "."
#endif

namespace {

struct Json {
  enum class Type { null_value, bool_value, number_value, string_value, array_value, object_value };
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

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

  Json parse() {
    skip();
    Json value = parse_value();
    skip();
    if (index_ != text_.size()) {
      throw std::runtime_error("unexpected trailing JSON content");
    }
    return value;
  }

 private:
  Json parse_value() {
    skip();
    if (index_ >= text_.size()) {
      throw std::runtime_error("unexpected JSON end");
    }
    const char c = text_[index_];
    if (c == '{') {
      return parse_object();
    }
    if (c == '[') {
      return parse_array();
    }
    if (c == '"') {
      Json value;
      value.type = Json::Type::string_value;
      value.string = parse_string();
      return value;
    }
    if (c == 't') {
      consume("true");
      Json value;
      value.type = Json::Type::bool_value;
      value.boolean = true;
      return value;
    }
    if (c == 'f') {
      consume("false");
      Json value;
      value.type = Json::Type::bool_value;
      value.boolean = false;
      return value;
    }
    if (c == 'n') {
      consume("null");
      return {};
    }
    return parse_number();
  }

  Json parse_object() {
    expect('{');
    Json value;
    value.type = Json::Type::object_value;
    skip();
    if (try_consume('}')) {
      return value;
    }
    while (true) {
      std::string key = parse_string();
      skip();
      expect(':');
      value.object.emplace(std::move(key), parse_value());
      skip();
      if (try_consume('}')) {
        return value;
      }
      expect(',');
      skip();
    }
  }

  Json parse_array() {
    expect('[');
    Json value;
    value.type = Json::Type::array_value;
    skip();
    if (try_consume(']')) {
      return value;
    }
    while (true) {
      value.array.push_back(parse_value());
      skip();
      if (try_consume(']')) {
        return value;
      }
      expect(',');
    }
  }

  Json parse_number() {
    const std::size_t begin = index_;
    if (index_ < text_.size() && text_[index_] == '-') {
      ++index_;
    }
    while (index_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[index_]))) {
      ++index_;
    }
    if (index_ < text_.size() && text_[index_] == '.') {
      ++index_;
      while (index_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[index_]))) {
        ++index_;
      }
    }
    if (index_ < text_.size() && (text_[index_] == 'e' || text_[index_] == 'E')) {
      ++index_;
      if (index_ < text_.size() && (text_[index_] == '+' || text_[index_] == '-')) {
        ++index_;
      }
      while (index_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[index_]))) {
        ++index_;
      }
    }
    if (begin == index_) {
      throw std::runtime_error("expected JSON value");
    }
    Json value;
    value.type = Json::Type::number_value;
    value.number = std::stod(std::string{text_.substr(begin, index_ - begin)});
    return value;
  }

  std::string parse_string() {
    expect('"');
    std::string output;
    while (index_ < text_.size()) {
      const char c = text_[index_++];
      if (c == '"') {
        return output;
      }
      if (c == '\\') {
        if (index_ >= text_.size()) {
          throw std::runtime_error("unterminated JSON escape");
        }
        const char escaped = text_[index_++];
        output.push_back(escaped == 'n' ? '\n' : escaped);
      } else {
        output.push_back(c);
      }
    }
    throw std::runtime_error("unterminated JSON string");
  }

  void skip() {
    while (index_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[index_]))) {
      ++index_;
    }
  }

  void consume(std::string_view literal) {
    if (text_.substr(index_, literal.size()) != literal) {
      throw std::runtime_error("unexpected JSON literal");
    }
    index_ += literal.size();
  }

  void expect(char c) {
    skip();
    if (index_ >= text_.size() || text_[index_] != c) {
      throw std::runtime_error("unexpected JSON character");
    }
    ++index_;
  }

  bool try_consume(char c) {
    if (index_ < text_.size() && text_[index_] == c) {
      ++index_;
      return true;
    }
    return false;
  }

  std::string_view text_;
  std::size_t index_ = 0;
};

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

const Json& at(const Json& value, std::string_view key) {
  assert(value.type == Json::Type::object_value);
  const auto found = value.object.find(std::string{key});
  assert(found != value.object.end());
  return found->second;
}

const Json& at(const Json& value, std::size_t index) {
  assert(value.type == Json::Type::array_value);
  assert(index < value.array.size());
  return value.array[index];
}

std::string string_at(const Json& value, std::string_view key) {
  const Json& child = at(value, key);
  assert(child.type == Json::Type::string_value);
  return child.string;
}

double number_at(const Json& value, std::string_view key) {
  const Json& child = at(value, key);
  assert(child.type == Json::Type::number_value);
  return child.number;
}

bool bool_at(const Json& value, std::string_view key) {
  const Json& child = at(value, key);
  assert(child.type == Json::Type::bool_value);
  return child.boolean;
}

void require_sha256(const Json& checksum) {
  assert(string_at(checksum, "algorithm") == "sha256");
  assert(string_at(checksum, "value").size() == 64U);
}

} // namespace

int main() {
  const std::filesystem::path manifest_path =
    std::filesystem::path(FLYING_REPO_SOURCE_DIR) / "Data" / "Terrain" /
    "SliceManifest.json";
  const Json manifest = JsonParser{read_file(manifest_path)}.parse();

  assert(string_at(manifest, "schemaVersion") == "flying.terrain-slice.v1");
  assert(string_at(manifest, "packageId") == "dmr5g-cz-initial-50km-slice");

  const Json& coverage = at(manifest, "coverage");
  const Json& bounds = at(coverage, "bounds");
  assert(string_at(coverage, "countryCode") == "CZ");
  assert(number_at(bounds, "widthM") == 50000.0);
  assert(number_at(bounds, "heightM") == 50000.0);
  assert(bool_at(coverage, "offlineRuntime"));

  const Json& provenance = at(manifest, "provenance");
  assert(string_at(provenance, "sourceDataset") == "DMR 5G");
  assert(string_at(provenance, "publisher") == "CUZK");
  assert(string_at(provenance, "sourceVersion") == "2026.08-approved-slice");
  assert(string_at(provenance, "sourceEffectiveDate") == "2026-08-01");
  assert(string_at(provenance, "licenseEvidence") == "docs/licenses/source-attribution.yml");

  const Json& transforms = at(manifest, "transforms");
  assert(string_at(at(transforms, "sourceCrs"), "code") == "5514");
  assert(string_at(at(transforms, "targetCrs"), "code") == "CZ-SLICE-ENU");
  assert(string_at(at(transforms, "sourceHeightSystem"), "kind") == "orthometric");
  assert(string_at(at(transforms, "targetHeightSystem"), "kind") == "ellipsoidal");
  assert(number_at(at(transforms, "geoid"), "undulationMeters") == 45.25);
  assert(string_at(at(transforms, "proj"), "version") == "9.4-recorded");
  assert(number_at(at(transforms, "sourceToProject"), "eastOffsetM") == 750000.0);
  assert(number_at(at(transforms, "sourceToProject"), "northOffsetM") == 1050000.0);
  assert(number_at(at(transforms, "heightTransform"), "sourceHeightScale") == 1.0);

  const Json& transform_validation = at(manifest, "transformValidation");
  assert(bool_at(transform_validation, "passed"));
  assert(number_at(transform_validation, "transformedSampleCount") == 132.0);
  assert(number_at(transform_validation, "sampleSpacingM") == 5000.0);

  const Json& source_tiles = at(manifest, "sourceTiles");
  assert(source_tiles.array.size() == 2U);
  assert(number_at(at(at(source_tiles, 0), "bounds"), "minEastM") == 0.0);
  assert(number_at(at(at(source_tiles, 1), "bounds"), "maxEastM") == 50000.0);
  for (const Json& tile : source_tiles.array) {
    assert(string_at(tile, "format") == "dmr5g-epsg5514-bpv-csv");
    assert(number_at(tile, "sourceSampleCount") == 66.0);
    assert(number_at(at(tile, "sourceBounds"), "maxSourceY") == -1000000.0);
    require_sha256(at(tile, "checksum"));
  }

  const Json& terrain_metadata = at(manifest, "terrainMetadata");
  assert(string_at(terrain_metadata, "normalFrame") == "project-local-ENU");
  assert(string_at(terrain_metadata, "waterMaskEncoding") == "uint8-csv");
  assert(string_at(terrain_metadata, "materialMaskEncoding") == "uint8-csv");
  assert(bool_at(terrain_metadata, "collisionTilesAreSeparate"));

  const Json& render_lods = at(manifest, "renderLods");
  assert(render_lods.array.size() == 3U);
  assert(number_at(at(render_lods, 0), "sampleStride") == 1.0);
  assert(number_at(at(render_lods, 1), "sampleStride") == 2.0);
  assert(number_at(at(render_lods, 2), "sampleStride") == 4.0);
  for (const Json& lod : render_lods.array) {
    assert(at(lod, "tiles").array.size() == 2U);
    for (const Json& tile : at(lod, "tiles").array) {
      assert(number_at(tile, "sourceSampleSpacingM") == 5000.0);
      require_sha256(at(tile, "checksum"));
    }
  }
  assert(at(manifest, "collisionTiles").array.size() == 2U);

  const Json& validation = at(manifest, "validation");
  const Json& control_points = at(at(validation, "controlPoints"), "points");
  assert(control_points.array.size() == 3U);
  for (const Json& point : control_points.array) {
    assert(!string_at(point, "source").empty());
    assert(number_at(point, "absoluteErrorM") <= number_at(point, "allowedErrorM"));
    assert(bool_at(point, "passed"));
  }

  const Json& edge = at(validation, "edgeContinuity");
  assert(number_at(edge, "comparedSampleCount") == 11.0);
  assert(number_at(edge, "unmatchedSampleCount") == 0.0);
  assert(number_at(edge, "failedSampleCount") == 0.0);
  assert(number_at(edge, "maxAbsStepM") <= number_at(edge, "toleranceM"));
  assert(bool_at(edge, "passed"));
  assert(number_at(at(validation, "boundaryCleaning"), "adjustedSampleCount") == 0.0);

  return 0;
}
