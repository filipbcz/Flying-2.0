#include "flying/data_pipeline/pilot_region_packages.hpp"

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
#include <utility>
#include <vector>

namespace flying::data_pipeline {
namespace {

constexpr std::string_view kConfigSchemaVersion =
  "flying.pilot-region-package-config.v1";
constexpr std::string_view kCzechConfigSchemaVersion =
  "flying.czech-republic-package-config.v1";
constexpr std::string_view kPackageSchemaVersion = "flying.pilot-region-package.v1";
constexpr std::string_view kVectorSchemaVersion = "flying.vector-layer-package.v1";
constexpr std::string_view kValidationReportSchemaVersion =
  "flying.validation-report.v1";
constexpr double kCoordinateToleranceM = 1.0e-6;

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
    unsigned int code_point = parse_hex_quad();
    if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
      if (index_ + 6U > text_.size() || text_[index_] != '\\' ||
          text_[index_ + 1U] != 'u') {
        throw std::runtime_error("invalid JSON surrogate pair");
      }
      index_ += 2U;
      const unsigned int low = parse_hex_quad();
      if (low < 0xDC00U || low > 0xDFFFU) {
        throw std::runtime_error("invalid JSON surrogate pair");
      }
      code_point =
        0x10000U + ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
    } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
      throw std::runtime_error("invalid JSON surrogate pair");
    }
    append_utf8(output, code_point);
  }

  unsigned int parse_hex_quad() {
    if (index_ + 4U > text_.size()) {
      throw std::runtime_error("incomplete JSON unicode escape");
    }
    unsigned int value = 0U;
    for (std::size_t i = 0; i < 4U; ++i) {
      const int digit = hex_digit(text_[index_++]);
      if (digit < 0) {
        throw std::runtime_error("invalid JSON unicode escape");
      }
      value = (value << 4U) | static_cast<unsigned int>(digit);
    }
    return value;
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

  static void append_utf8(std::string& output, unsigned int code_point) {
    if (code_point <= 0x7FU) {
      output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
      output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
      output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
      output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
      output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
      output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
      output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
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
  if (std::isfinite(value) == 0) {
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
      for (std::size_t i = 0; i < value.array.size(); ++i) {
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

void write_binary_file(const std::filesystem::path& path,
                       const std::vector<std::uint8_t>& bytes) {
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open " + path.string());
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("failed to write " + path.string());
  }
}

class Sha256 {
 public:
  void update(const unsigned char* data, std::size_t size) {
    total_size_ += size;
    while (size > 0U) {
      const std::size_t remaining = block_.size() - block_size_;
      const std::size_t copied = std::min(remaining, size);
      std::copy_n(data, copied, block_.begin() + static_cast<std::ptrdiff_t>(block_size_));
      block_size_ += copied;
      data += copied;
      size -= copied;
      if (block_size_ == block_.size()) {
        process_block(block_.data());
        block_size_ = 0U;
      }
    }
  }

  void update(std::string_view text) {
    update(reinterpret_cast<const unsigned char*>(text.data()), text.size());
  }

  std::string final_hex() {
    const std::uint64_t total_bits = static_cast<std::uint64_t>(total_size_) * 8ULL;
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56U) {
      while (block_size_ < block_.size()) {
        block_[block_size_++] = 0U;
      }
      process_block(block_.data());
      block_size_ = 0U;
    }
    while (block_size_ < 56U) {
      block_[block_size_++] = 0U;
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
      block_[block_size_++] = static_cast<unsigned char>((total_bits >> shift) & 0xFFULL);
    }
    process_block(block_.data());

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint32_t word : state_) {
      output << std::setw(8) << word;
    }
    return output.str();
  }

 private:
  static constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
  };

  static std::uint32_t rotate_right(std::uint32_t value, unsigned int shift) noexcept {
    return (value >> shift) | (value << (32U - shift));
  }

  static std::uint32_t load_big_endian_word(const unsigned char* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
  }

  void process_block(const unsigned char* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16U; ++i) {
      words[i] = load_big_endian_word(block + (i * 4U));
    }
    for (std::size_t i = 16U; i < words.size(); ++i) {
      const std::uint32_t s0 =
        rotate_right(words[i - 15U], 7U) ^ rotate_right(words[i - 15U], 18U) ^
        (words[i - 15U] >> 3U);
      const std::uint32_t s1 =
        rotate_right(words[i - 2U], 17U) ^ rotate_right(words[i - 2U], 19U) ^
        (words[i - 2U] >> 10U);
      words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < words.size(); ++i) {
      const std::uint32_t sum1 =
        rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
      const std::uint32_t choose = (e & f) ^ ((~e) & g);
      const std::uint32_t temp1 = h + sum1 + choose + kRoundConstants[i] + words[i];
      const std::uint32_t sum0 =
        rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = sum0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_ = {
    0x6a09e667U,
    0xbb67ae85U,
    0x3c6ef372U,
    0xa54ff53aU,
    0x510e527fU,
    0x9b05688cU,
    0x1f83d9abU,
    0x5be0cd19U,
  };
  std::array<unsigned char, 64> block_{};
  std::size_t block_size_ = 0U;
  std::size_t total_size_ = 0U;
};

std::string sha256_text(std::string_view text) {
  Sha256 sha;
  sha.update(text);
  return sha.final_hex();
}

std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open " + path.string());
  }
  Sha256 sha;
  std::array<unsigned char, 8192> buffer{};
  while (input) {
    input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count > 0) {
      sha.update(buffer.data(), static_cast<std::size_t>(read_count));
    }
  }
  if (!input.eof()) {
    throw std::runtime_error("failed to read " + path.string());
  }
  return sha.final_hex();
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

std::string normalized_relative_path_string(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

bool contains_remote_reference(std::string_view text) {
  const std::string lowered = lowercase_ascii(text);
  if (lowered.find("://") != std::string::npos || lowered.rfind("//", 0U) == 0U) {
    return true;
  }
  const std::size_t scheme_separator = lowered.find(":/");
  if (scheme_separator == std::string::npos || scheme_separator == 0U) {
    return false;
  }
  if (std::isalpha(static_cast<unsigned char>(lowered[0])) == 0) {
    return false;
  }
  return std::all_of(lowered.begin(),
                     lowered.begin() + static_cast<std::ptrdiff_t>(scheme_separator),
                     [](char ch) {
                       const unsigned char uch = static_cast<unsigned char>(ch);
                       return std::isalnum(uch) != 0 || ch == '+' || ch == '-' || ch == '.';
                     });
}

std::string normalized_runtime_reference_key(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (const char ch : text) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(uch)));
    }
  }
  return normalized;
}

bool is_disallowed_runtime_reference_key(std::string_view key) {
  const std::string normalized = normalized_runtime_reference_key(key);
  const std::array<std::string_view, 14U> disallowed = {
    "apikey",
    "accesskey",
    "accesstoken",
    "mapboxtoken",
    "urltemplate",
    "templateurl",
    "wmtsurl",
    "wmsurl",
    "tileurl",
    "tileurls",
    "tileserverurl",
    "tileserverurls",
    "remotetileserverurl",
    "remotetileserverurls",
  };
  return std::any_of(disallowed.begin(), disallowed.end(), [&normalized](std::string_view token) {
    return normalized == token;
  });
}

bool looks_like_remote_tile_reference(std::string_view text) {
  const std::string lowered = lowercase_ascii(text);
  if (!contains_remote_reference(lowered)) {
    return false;
  }
  const std::array<std::string_view, 12U> tile_indicators = {
    "{z}",
    "{x}",
    "{y}",
    "{bbox}",
    "tilematrix",
    "tilematrixset",
    "mapbox",
    "openstreetmap",
    "tiles.",
    "/tiles/",
    "/tile/",
    "arcgis/rest/services",
  };
  return std::any_of(tile_indicators.begin(),
                     tile_indicators.end(),
                     [&lowered](std::string_view token) {
                       return lowered.find(token) != std::string::npos;
                     });
}

const JsonValue* find_member(const JsonValue::Object& object, std::string_view key) {
  const auto it = object.find(std::string{key});
  if (it == object.end()) {
    return nullptr;
  }
  return &it->second;
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
  if (value->type != JsonValue::Type::number_value) {
    add_issue(report,
              "error",
              code + ".invalid_type",
              std::string{subject} + " field '" + std::string{key} + "' must be a number.",
              std::string{source_id});
    return std::nullopt;
  }
  return value->number;
}

std::optional<int> require_positive_int(const JsonValue::Object& object,
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
  if (std::fabs(*value - rounded) > 0.000001 || rounded < 1.0 ||
      rounded > static_cast<double>(std::numeric_limits<int>::max())) {
    add_issue(report,
              "error",
              std::string{code_prefix} + "." + std::string{key} + ".invalid",
              std::string{subject} + " field '" + std::string{key} +
                "' must be a positive integer.",
              std::string{source_id});
    return std::nullopt;
  }
  return static_cast<int>(rounded);
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
                                      std::string_view subject,
                                      std::string_view source_id = {}) {
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
  return &value->array;
}

struct Point2 {
  double east_m = 0.0;
  double north_m = 0.0;
};

struct Bounds {
  double min_east_m = 0.0;
  double max_east_m = 0.0;
  double min_north_m = 0.0;
  double max_north_m = 0.0;
};

struct PilotRegion {
  std::string id;
  double min_east_m = 0.0;
  double min_north_m = 0.0;
  double width_m = 0.0;
  double height_m = 0.0;

  [[nodiscard]] double max_east_m() const noexcept { return min_east_m + width_m; }
  [[nodiscard]] double max_north_m() const noexcept { return min_north_m + height_m; }

  [[nodiscard]] bool contains(Point2 point) const noexcept {
    return point.east_m >= min_east_m && point.east_m <= max_east_m() &&
           point.north_m >= min_north_m && point.north_m <= max_north_m();
  }
};

struct SourceToProjectTransform {
  double east_from_source_x_scale = 1.0;
  double east_from_source_y_scale = 0.0;
  double east_offset_m = 0.0;
  double north_from_source_x_scale = 0.0;
  double north_from_source_y_scale = 1.0;
  double north_offset_m = 0.0;

  [[nodiscard]] Point2 apply(double source_x, double source_y) const noexcept {
    return {
      (east_from_source_x_scale * source_x) + (east_from_source_y_scale * source_y) +
        east_offset_m,
      (north_from_source_x_scale * source_x) + (north_from_source_y_scale * source_y) +
        north_offset_m,
    };
  }
};

struct OrthoSourceConfig {
  std::string id;
  std::filesystem::path path;
  std::string format;
  Bounds bounds;
  std::string source_manifest_id;
};

struct OrthoConfig {
  int tile_size_px = 256;
  int mip_levels = 2;
  std::vector<OrthoSourceConfig> sources;
};

struct VectorLayerConfig {
  std::string category;
  std::filesystem::path path;
  std::string format = "geojson";
  double line_width_m = 0.0;
  std::optional<Bounds> bounds;
  std::string source_manifest_id;
};

struct GeoNamesConfig {
  std::filesystem::path path;
  std::string format = "csv";
  std::string x_field = "eastM";
  std::string y_field = "northM";
  std::optional<Bounds> bounds;
  std::string source_manifest_id;
};

struct PackageInputConfig {
  std::string role;
  std::filesystem::path path;
  std::optional<Bounds> bounds;
  std::string source_manifest_id;
};

struct MaterialLayerConfig {
  std::string category;
  std::string material;
};

struct MaskConfig {
  double cell_size_m = 0.0;
  std::string default_material = "terrain";
  std::vector<MaterialLayerConfig> material_layers;
};

struct GraphicsDensityProfileConfig {
  std::string id;
  double vegetation_density_scale = 1.0;
  double object_density_scale = 1.0;
};

struct WorldObjectsConfig {
  bool enabled = true;
  double active_collision_radius_m = 2000.0;
  double stream_in_distance_m = 12000.0;
  double stream_out_distance_m = 14500.0;
  double default_building_height_m = 8.0;
  double default_vegetation_height_m = 14.0;
  double default_obstacle_height_m = 30.0;
  double default_power_line_height_m = 18.0;
  std::vector<GraphicsDensityProfileConfig> graphics_profiles;
};

struct PilotPackageConfig {
  std::string schema_version;
  std::string package_id_hint;
  std::string coverage_scope = "pilot-region";
  PilotRegion pilot_region;
  SourceToProjectTransform transform;
  std::string transform_json;
  OrthoConfig ortho;
  std::vector<VectorLayerConfig> vector_layers;
  GeoNamesConfig geonames;
  std::vector<PackageInputConfig> package_inputs;
  MaskConfig masks;
  WorldObjectsConfig world_objects;
};

struct RegionManifest {
  std::string schema_version;
  std::string region_id;
  std::string display_name;
  std::string coverage_scope;
  std::string package_mode;
  Bounds project_bounds;
  double source_margin_m = 0.0;
  std::string data_root_variable;
  std::filesystem::path data_root_relative_path;
  std::string min_app_version;
  bool runtime_network_required = true;
};

std::optional<Bounds> parse_bounds(const JsonValue::Object& object,
                                   std::string_view key,
                                   ValidationReport& report,
                                   std::string_view code_prefix,
                                   std::string_view subject) {
  const JsonValue::Object* bounds_object =
    require_object(object, key, report, code_prefix, subject);
  if (bounds_object == nullptr) {
    return std::nullopt;
  }

  Bounds bounds;
  const std::string nested_prefix = std::string{code_prefix} + "." + std::string{key};
  if (const auto value =
        require_number(*bounds_object, "minEastM", report, nested_prefix, subject)) {
    bounds.min_east_m = *value;
  }
  if (const auto value =
        require_number(*bounds_object, "maxEastM", report, nested_prefix, subject)) {
    bounds.max_east_m = *value;
  }
  if (const auto value =
        require_number(*bounds_object, "minNorthM", report, nested_prefix, subject)) {
    bounds.min_north_m = *value;
  }
  if (const auto value =
        require_number(*bounds_object, "maxNorthM", report, nested_prefix, subject)) {
    bounds.max_north_m = *value;
  }
  if (bounds.max_east_m <= bounds.min_east_m || bounds.max_north_m <= bounds.min_north_m) {
    add_issue(report,
              "error",
              std::string{code_prefix} + "." + std::string{key} + ".invalid",
              std::string{subject} + " bounds must have positive width and height.");
  }
  return bounds;
}

bool validate_local_input_path(const std::filesystem::path& path,
                               ValidationReport& report,
                               std::string_view code_prefix,
                               std::string_view subject) {
  if (path.empty()) {
    return false;
  }
  const std::string path_string = path.generic_string();
  if (path.is_absolute()) {
    add_issue(report,
              "error",
              std::string{code_prefix} + ".absolute",
              std::string{subject} + " path must be relative to the source root.");
    return false;
  }
  if (contains_remote_reference(path_string)) {
    add_issue(report,
              "error",
              std::string{code_prefix} + ".remote_url",
              std::string{subject} +
                " path must reference a local source payload, not a remote URL.");
    return false;
  }
  for (const std::filesystem::path& part : path) {
    if (part == "..") {
      add_issue(report,
                "error",
                std::string{code_prefix} + ".parent_traversal",
                std::string{subject} +
                  " path must stay within the declared source root.");
      return false;
    }
  }
  return true;
}

std::optional<PilotPackageConfig> parse_package_config(const JsonValue& root,
                                                       ValidationReport& report) {
  if (root.type != JsonValue::Type::object_value) {
    add_issue(report, "error", "pilot.config.invalid_type", "Pilot package config root must be an object.");
    return std::nullopt;
  }

  const JsonValue::Object& object = root.object;
  PilotPackageConfig config;
  const std::optional<std::string> schema_version =
    require_string(object, "schemaVersion", report, "pilot.config", "Pilot package config");
  if (schema_version.has_value()) {
    config.schema_version = *schema_version;
  }
  if (schema_version.has_value() && *schema_version != kConfigSchemaVersion &&
      *schema_version != kCzechConfigSchemaVersion) {
    add_issue(report,
              "error",
              "pilot.config.schemaVersion.unsupported",
              "Pilot package config schemaVersion must be '" +
                std::string{kConfigSchemaVersion} + "' or '" +
                std::string{kCzechConfigSchemaVersion} + "'.");
  }
  if (schema_version.has_value() && *schema_version == kCzechConfigSchemaVersion) {
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
              "pilot.config.coverageScope.unsupported",
              "Pilot package config coverageScope must be 'pilot-region' or 'czech-republic'.");
  }
  if (schema_version.has_value() && *schema_version == kCzechConfigSchemaVersion &&
      config.coverage_scope != "czech-republic") {
    add_issue(report,
              "error",
              "pilot.config.coverageScope.schema_mismatch",
              "Czech Republic package config schema requires coverageScope 'czech-republic'.");
  }
  if (schema_version.has_value() && *schema_version == kConfigSchemaVersion &&
      config.coverage_scope == "czech-republic") {
    add_issue(report,
              "error",
              "pilot.config.coverageScope.schema_mismatch",
              "Czech Republic coverage requires the Czech Republic package config schema.");
  }
  if (const JsonValue* package_id_hint = find_member(object, "packageIdHint");
      package_id_hint != nullptr && package_id_hint->type == JsonValue::Type::string_value) {
    config.package_id_hint = package_id_hint->string;
  }

  if (const JsonValue::Object* region =
        require_object(object, "pilotRegion", report, "pilot.config", "Pilot package config")) {
    if (const auto value =
          require_string(*region, "id", report, "pilot.config.pilotRegion", "Pilot region")) {
      config.pilot_region.id = *value;
    }
    if (const auto value =
          require_number(*region, "minEastM", report, "pilot.config.pilotRegion", "Pilot region")) {
      config.pilot_region.min_east_m = *value;
    }
    if (const auto value =
          require_number(*region, "minNorthM", report, "pilot.config.pilotRegion", "Pilot region")) {
      config.pilot_region.min_north_m = *value;
    }
    if (const auto value =
          require_number(*region, "widthM", report, "pilot.config.pilotRegion", "Pilot region")) {
      config.pilot_region.width_m = *value;
    }
    if (const auto value =
          require_number(*region, "heightM", report, "pilot.config.pilotRegion", "Pilot region")) {
      config.pilot_region.height_m = *value;
    }
    if (config.pilot_region.width_m <= 0.0 || config.pilot_region.height_m <= 0.0) {
      add_issue(report,
                "error",
                "pilot.config.pilotRegion.extent.invalid",
                "Pilot region widthM and heightM must be positive.");
    }
    if (config.coverage_scope == "czech-republic" &&
        (config.pilot_region.width_m <= 50000.0 || config.pilot_region.height_m <= 50000.0)) {
      add_issue(report,
                "error",
                "pilot.config.czechRepublic.extent.invalid",
                "Full Czech Republic package processing requires an extent larger than the pilot region.");
    }
  }

  const JsonValue* transform_value = find_member(object, "transform");
  if (const JsonValue::Object* transform =
        require_object(object, "transform", report, "pilot.config", "Pilot package config")) {
    if (transform_value != nullptr) {
      config.transform_json = render_canonical_json(*transform_value);
    }
    if (const JsonValue::Object* source_to_project =
          require_object(*transform,
                         "sourceToProject",
                         report,
                         "pilot.config.transform",
                         "Pilot transform")) {
      const std::string prefix = "pilot.config.transform.sourceToProject";
      if (const auto value = require_number(*source_to_project,
                                            "eastFromSourceXScale",
                                            report,
                                            prefix,
                                            "Pilot source-to-project transform")) {
        config.transform.east_from_source_x_scale = *value;
      }
      if (const auto value = require_number(*source_to_project,
                                            "eastFromSourceYScale",
                                            report,
                                            prefix,
                                            "Pilot source-to-project transform")) {
        config.transform.east_from_source_y_scale = *value;
      }
      if (const auto value = require_number(*source_to_project,
                                            "eastOffsetM",
                                            report,
                                            prefix,
                                            "Pilot source-to-project transform")) {
        config.transform.east_offset_m = *value;
      }
      if (const auto value = require_number(*source_to_project,
                                            "northFromSourceXScale",
                                            report,
                                            prefix,
                                            "Pilot source-to-project transform")) {
        config.transform.north_from_source_x_scale = *value;
      }
      if (const auto value = require_number(*source_to_project,
                                            "northFromSourceYScale",
                                            report,
                                            prefix,
                                            "Pilot source-to-project transform")) {
        config.transform.north_from_source_y_scale = *value;
      }
      if (const auto value = require_number(*source_to_project,
                                            "northOffsetM",
                                            report,
                                            prefix,
                                            "Pilot source-to-project transform")) {
        config.transform.north_offset_m = *value;
      }
    }
  }

  if (const JsonValue::Object* ortho =
        require_object(object, "orthoImagery", report, "pilot.config", "Pilot package config")) {
    if (const auto value =
          require_positive_int(*ortho, "tileSizePx", report, "pilot.config.orthoImagery", "Ortofoto imagery")) {
      config.ortho.tile_size_px = *value;
    }
    if (const auto value =
          require_positive_int(*ortho, "mipLevels", report, "pilot.config.orthoImagery", "Ortofoto imagery")) {
      config.ortho.mip_levels = *value;
      if (*value < 2) {
        add_issue(report,
                  "error",
                  "pilot.config.orthoImagery.mipLevels.too_low",
                  "Ortofoto imagery mipLevels must be at least 2 so offline packages include mipmaps.");
      }
    }
    const JsonValue::Array* sources =
      require_array(*ortho, "sources", report, "pilot.config.orthoImagery", "Ortofoto imagery");
    if (sources != nullptr) {
      if (sources->empty()) {
        add_issue(report,
                  "error",
                  "pilot.config.orthoImagery.sources.empty",
                  "Ortofoto imagery requires at least one local source image.");
      }
      std::set<std::string> ids;
      for (std::size_t i = 0; i < sources->size(); ++i) {
        const JsonValue& entry = (*sources)[i];
        const std::string subject = "Ortofoto source";
        const std::string source_id = "<ortho[" + std::to_string(i) + "]>";
        if (entry.type != JsonValue::Type::object_value) {
          add_issue(report,
                    "error",
                    "pilot.config.orthoImagery.sources.invalid_type",
                    "Ortofoto source entries must be objects.",
                    source_id);
          continue;
        }
        const JsonValue::Object& source = entry.object;
        OrthoSourceConfig ortho_source;
        if (const auto value = require_string(
              source, "id", report, "pilot.config.orthoImagery.sources", subject, source_id)) {
          ortho_source.id = *value;
        }
        const std::string issue_source_id =
          ortho_source.id.empty() ? source_id : ortho_source.id;
        if (const auto value = require_string(source,
                                             "path",
                                             report,
                                             "pilot.config.orthoImagery.sources",
                                             subject,
                                             issue_source_id)) {
          ortho_source.path = *value;
          (void)validate_local_input_path(ortho_source.path,
                                          report,
                                          "pilot.config.orthoImagery.sources.path",
                                          subject);
        }
        if (const auto value = require_string(source,
                                             "format",
                                             report,
                                             "pilot.config.orthoImagery.sources",
                                             subject,
                                             issue_source_id)) {
          ortho_source.format = lowercase_ascii(*value);
        }
        if (ortho_source.format != "ppm-p3") {
          add_issue(report,
                    "error",
                    "pilot.config.orthoImagery.sources.format.unsupported",
                    "Pilot Ortofoto processing currently accepts local 'ppm-p3' source images.",
                    issue_source_id);
        }
        if (const auto bounds = parse_bounds(source,
                                            "bounds",
                                            report,
                                            "pilot.config.orthoImagery.sources",
                                            subject)) {
          ortho_source.bounds = *bounds;
        }
        if (!ortho_source.id.empty() && !ids.insert(ortho_source.id).second) {
          add_issue(report,
                    "error",
                    "pilot.config.orthoImagery.sources.id.duplicate",
                    "Ortofoto source ids must be unique.",
                    ortho_source.id);
        }
        config.ortho.sources.push_back(std::move(ortho_source));
      }
    }
  }

  const JsonValue::Array* vector_layers =
    require_array(object, "vectorLayers", report, "pilot.config", "Pilot package config");
  if (vector_layers != nullptr) {
    if (vector_layers->empty()) {
      add_issue(report,
                "error",
                "pilot.config.vectorLayers.empty",
                "Pilot package config must declare ZABAGED vector layers.");
    }
    for (std::size_t i = 0; i < vector_layers->size(); ++i) {
      const JsonValue& entry = (*vector_layers)[i];
      const std::string source_id = "<vectorLayer[" + std::to_string(i) + "]>";
      if (entry.type != JsonValue::Type::object_value) {
        add_issue(report,
                  "error",
                  "pilot.config.vectorLayers.invalid_type",
                  "Vector layer entries must be objects.",
                  source_id);
        continue;
      }
      const JsonValue::Object& layer = entry.object;
      VectorLayerConfig layer_config;
      if (const auto value =
            require_string(layer, "category", report, "pilot.config.vectorLayers", "Vector layer", source_id)) {
        layer_config.category = *value;
      }
      const std::string issue_source_id =
        layer_config.category.empty() ? source_id : layer_config.category;
      if (const auto value =
            require_string(layer, "path", report, "pilot.config.vectorLayers", "Vector layer", issue_source_id)) {
        layer_config.path = *value;
        (void)validate_local_input_path(layer_config.path,
                                        report,
                                        "pilot.config.vectorLayers.path",
                                        "Vector layer");
      }
      if (const JsonValue* format = find_member(layer, "format");
          format != nullptr && format->type == JsonValue::Type::string_value) {
        layer_config.format = lowercase_ascii(format->string);
      }
      if (layer_config.format != "geojson") {
        add_issue(report,
                  "error",
                  "pilot.config.vectorLayers.format.unsupported",
                  "Pilot ZABAGED conversion currently accepts local GeoJSON vector layers.",
                  issue_source_id);
      }
      if (const JsonValue* line_width = find_member(layer, "lineWidthM");
          line_width != nullptr && line_width->type == JsonValue::Type::number_value) {
        layer_config.line_width_m = line_width->number;
      }
      if (find_member(layer, "bounds") != nullptr) {
        if (const auto bounds =
              parse_bounds(layer, "bounds", report, "pilot.config.vectorLayers", "Vector layer")) {
          layer_config.bounds = *bounds;
        }
      }
      config.vector_layers.push_back(std::move(layer_config));
    }
  }

  const std::set<std::string> required_categories = {
    "airport",
    "notableObjects",
    "rail",
    "roads",
    "runway",
    "settlements",
    "vegetationAreas",
    "water",
  };
  std::set<std::string> declared_categories;
  for (const VectorLayerConfig& layer : config.vector_layers) {
    declared_categories.insert(layer.category);
  }
  for (const std::string& category : required_categories) {
    if (declared_categories.find(category) == declared_categories.end()) {
      add_issue(report,
                "error",
                "pilot.config.vectorLayers.category.missing",
                "Pilot package config must declare ZABAGED vector layer category '" +
                  category + "'.",
                category);
    }
  }

  if (const JsonValue::Object* geonames =
        require_object(object, "geonamesLabels", report, "pilot.config", "Pilot package config")) {
    if (const auto value =
          require_string(*geonames, "path", report, "pilot.config.geonamesLabels", "Geonames labels")) {
      config.geonames.path = *value;
      (void)validate_local_input_path(config.geonames.path,
                                      report,
                                      "pilot.config.geonamesLabels.path",
                                      "Geonames label source");
    }
    if (const JsonValue* format = find_member(*geonames, "format");
        format != nullptr && format->type == JsonValue::Type::string_value) {
      config.geonames.format = lowercase_ascii(format->string);
    }
    if (config.geonames.format != "csv") {
      add_issue(report,
                "error",
                "pilot.config.geonamesLabels.format.unsupported",
                "Pilot Geonames conversion currently accepts local CSV label files.");
    }
    if (const JsonValue* field = find_member(*geonames, "xField");
        field != nullptr && field->type == JsonValue::Type::string_value &&
        !field->string.empty()) {
      config.geonames.x_field = field->string;
    }
    if (const JsonValue* field = find_member(*geonames, "yField");
        field != nullptr && field->type == JsonValue::Type::string_value &&
        !field->string.empty()) {
      config.geonames.y_field = field->string;
    }
    if (find_member(*geonames, "bounds") != nullptr) {
      config.geonames.bounds =
        parse_bounds(*geonames, "bounds", report, "pilot.config.geonamesLabels", "Geonames labels");
    }
  }

  const JsonValue::Array* package_inputs =
    require_array(object, "packageInputs", report, "pilot.config", "Pilot package config");
  if (package_inputs != nullptr) {
    std::set<std::string> roles;
    for (std::size_t i = 0; i < package_inputs->size(); ++i) {
      const JsonValue& entry = (*package_inputs)[i];
      const std::string source_id = "<packageInput[" + std::to_string(i) + "]>";
      if (entry.type != JsonValue::Type::object_value) {
        add_issue(report,
                  "error",
                  "pilot.config.packageInputs.invalid_type",
                  "Package input entries must be objects.",
                  source_id);
        continue;
      }
      PackageInputConfig input;
      if (const auto value = require_string(entry.object,
                                            "role",
                                            report,
                                            "pilot.config.packageInputs",
                                            "Package input",
                                            source_id)) {
        input.role = *value;
      }
      const std::string issue_source_id = input.role.empty() ? source_id : input.role;
      if (const auto value = require_string(entry.object,
                                            "path",
                                            report,
                                            "pilot.config.packageInputs",
                                            "Package input",
                                            issue_source_id)) {
        input.path = *value;
        (void)validate_local_input_path(input.path,
                                        report,
                                        "pilot.config.packageInputs.path",
                                        "Package input");
      }
      if (find_member(entry.object, "bounds") != nullptr) {
        input.bounds =
          parse_bounds(entry.object, "bounds", report, "pilot.config.packageInputs", "Package input");
      }
      if (!input.role.empty() && !roles.insert(input.role).second) {
        add_issue(report,
                  "error",
                  "pilot.config.packageInputs.role.duplicate",
                  "Package input roles must be unique.",
                  input.role);
      }
      config.package_inputs.push_back(std::move(input));
    }
  }

  const std::set<std::string> required_package_input_roles = {
    "terrainElevation",
    "terrainCollision",
    "airportDatabase",
    "runwaySurfaces",
    "navigationMap",
    "maskSources",
    "worldObjectSources",
  };
  std::set<std::string> declared_package_input_roles;
  for (const PackageInputConfig& input : config.package_inputs) {
    declared_package_input_roles.insert(input.role);
  }
  for (const std::string& role : required_package_input_roles) {
    if (declared_package_input_roles.find(role) == declared_package_input_roles.end()) {
      add_issue(report,
                "error",
                "pilot.config.packageInputs.role.missing",
                "Region-aware package validation requires package input role '" + role + "'.",
                role);
    }
  }

  if (const JsonValue::Object* masks =
        require_object(object, "masks", report, "pilot.config", "Pilot package config")) {
    if (const auto value =
          require_number(*masks, "cellSizeM", report, "pilot.config.masks", "Mask configuration")) {
      config.masks.cell_size_m = *value;
    }
    if (config.masks.cell_size_m <= 0.0) {
      add_issue(report,
                "error",
                "pilot.config.masks.cellSizeM.invalid",
                "Mask cellSizeM must be positive.");
    }
    if (const JsonValue* default_material = find_member(*masks, "defaultMaterial");
        default_material != nullptr && default_material->type == JsonValue::Type::string_value &&
        !default_material->string.empty()) {
      config.masks.default_material = default_material->string;
    }
    const JsonValue::Array* material_layers =
      require_array(*masks, "materialLayers", report, "pilot.config.masks", "Mask configuration");
    if (material_layers != nullptr) {
      for (std::size_t i = 0; i < material_layers->size(); ++i) {
        const JsonValue& entry = (*material_layers)[i];
        const std::string source_id = "<materialLayer[" + std::to_string(i) + "]>";
        if (entry.type != JsonValue::Type::object_value) {
          add_issue(report,
                    "error",
                    "pilot.config.masks.materialLayers.invalid_type",
                    "Material layer entries must be objects.",
                    source_id);
          continue;
        }
        MaterialLayerConfig material_layer;
        if (const auto value = require_string(entry.object,
                                             "category",
                                             report,
                                             "pilot.config.masks.materialLayers",
                                             "Material layer",
                                             source_id)) {
          material_layer.category = *value;
        }
        if (const auto value = require_string(entry.object,
                                             "material",
                                             report,
                                             "pilot.config.masks.materialLayers",
                                             "Material layer",
                                             source_id)) {
          material_layer.material = *value;
        }
        config.masks.material_layers.push_back(std::move(material_layer));
      }
    }
  }

  config.world_objects.graphics_profiles = {
    {"low", 0.35, 0.50},
    {"medium", 0.65, 0.75},
    {"high", 1.00, 1.00},
  };
  if (const JsonValue* world_objects_value = find_member(object, "worldObjects");
      world_objects_value != nullptr) {
    if (world_objects_value->type != JsonValue::Type::object_value) {
      add_issue(report,
                "error",
                "pilot.config.worldObjects.invalid_type",
                "World object configuration must be an object when present.");
    } else {
      const JsonValue::Object& world_objects = world_objects_value->object;
      if (const JsonValue* enabled = find_member(world_objects, "enabled");
          enabled != nullptr && enabled->type == JsonValue::Type::bool_value) {
        config.world_objects.enabled = enabled->boolean;
      }
      if (const JsonValue* value = find_member(world_objects, "activeCollisionRadiusM");
          value != nullptr && value->type == JsonValue::Type::number_value) {
        config.world_objects.active_collision_radius_m = value->number;
      }
      if (const JsonValue* value = find_member(world_objects, "streamInDistanceM");
          value != nullptr && value->type == JsonValue::Type::number_value) {
        config.world_objects.stream_in_distance_m = value->number;
      }
      if (const JsonValue* value = find_member(world_objects, "streamOutDistanceM");
          value != nullptr && value->type == JsonValue::Type::number_value) {
        config.world_objects.stream_out_distance_m = value->number;
      }
      if (const JsonValue* heights_value = find_member(world_objects, "dmpHeightEstimates");
          heights_value != nullptr && heights_value->type == JsonValue::Type::object_value) {
        const JsonValue::Object& heights = heights_value->object;
        if (const JsonValue* value = find_member(heights, "defaultBuildingHeightM");
            value != nullptr && value->type == JsonValue::Type::number_value) {
          config.world_objects.default_building_height_m = value->number;
        }
        if (const JsonValue* value = find_member(heights, "defaultVegetationHeightM");
            value != nullptr && value->type == JsonValue::Type::number_value) {
          config.world_objects.default_vegetation_height_m = value->number;
        }
        if (const JsonValue* value = find_member(heights, "defaultObstacleHeightM");
            value != nullptr && value->type == JsonValue::Type::number_value) {
          config.world_objects.default_obstacle_height_m = value->number;
        }
        if (const JsonValue* value = find_member(heights, "defaultPowerLineHeightM");
            value != nullptr && value->type == JsonValue::Type::number_value) {
          config.world_objects.default_power_line_height_m = value->number;
        }
      }
      if (const JsonValue* profiles_value = find_member(world_objects, "graphicsProfiles");
          profiles_value != nullptr && profiles_value->type == JsonValue::Type::array_value &&
          !profiles_value->array.empty()) {
        config.world_objects.graphics_profiles.clear();
        for (const JsonValue& profile_value : profiles_value->array) {
          if (profile_value.type != JsonValue::Type::object_value) {
            add_issue(report,
                      "error",
                      "pilot.config.worldObjects.graphicsProfiles.invalid_type",
                      "World object graphics profiles must be objects.");
            continue;
          }
          GraphicsDensityProfileConfig profile;
          if (const JsonValue* id = find_member(profile_value.object, "id");
              id != nullptr && id->type == JsonValue::Type::string_value) {
            profile.id = id->string;
          }
          if (const JsonValue* value = find_member(profile_value.object, "vegetationDensityScale");
              value != nullptr && value->type == JsonValue::Type::number_value) {
            profile.vegetation_density_scale = value->number;
          }
          if (const JsonValue* value = find_member(profile_value.object, "objectDensityScale");
              value != nullptr && value->type == JsonValue::Type::number_value) {
            profile.object_density_scale = value->number;
          }
          if (profile.vegetation_density_scale < 0.0 ||
              profile.vegetation_density_scale > 1.0 ||
              profile.object_density_scale < 0.0 ||
              profile.object_density_scale > 1.0) {
            add_issue(report,
                      "error",
                      "pilot.config.worldObjects.graphicsProfiles.densityScale.invalid",
                      "World object graphics profile density scales must be between 0 and 1.");
          }
          if (!profile.id.empty()) {
            config.world_objects.graphics_profiles.push_back(std::move(profile));
          }
        }
      }
      if (config.world_objects.active_collision_radius_m <= 0.0 ||
          config.world_objects.stream_in_distance_m <= 0.0 ||
          config.world_objects.stream_out_distance_m <=
            config.world_objects.stream_in_distance_m) {
        add_issue(report,
                  "error",
                  "pilot.config.worldObjects.streaming.invalid",
                  "World object streaming distances and active collision radius must be positive with streamOutDistanceM greater than streamInDistanceM.");
      }
      if (config.world_objects.default_building_height_m <= 0.0 ||
          config.world_objects.default_vegetation_height_m <= 0.0 ||
          config.world_objects.default_obstacle_height_m <= 0.0 ||
          config.world_objects.default_power_line_height_m <= 0.0) {
        add_issue(report,
                  "error",
                  "pilot.config.worldObjects.dmpHeightEstimates.invalid",
                  "DMP-derived world object height estimates must be positive.");
      }
    }
  }

  if (has_errors(report)) {
    return std::nullopt;
  }
  return config;
}

std::optional<RegionManifest> parse_region_manifest(const JsonValue& root,
                                                    ValidationReport& report) {
  if (root.type != JsonValue::Type::object_value) {
    add_issue(report, "error", "region.manifest.invalid_type", "Region manifest root must be an object.");
    return std::nullopt;
  }

  const JsonValue::Object& object = root.object;
  RegionManifest manifest;
  if (const auto value =
        require_string(object, "schemaVersion", report, "region.manifest", "Region manifest")) {
    manifest.schema_version = *value;
  }
  if (manifest.schema_version != "flying.region-manifest.v1") {
    add_issue(report,
              "error",
              "region.manifest.schemaVersion.unsupported",
              "Region manifest schemaVersion must be 'flying.region-manifest.v1'.");
  }
  if (const auto value =
        require_string(object, "regionId", report, "region.manifest", "Region manifest")) {
    manifest.region_id = *value;
  }
  if (const auto value =
        require_string(object, "displayName", report, "region.manifest", "Region manifest")) {
    manifest.display_name = *value;
  }
  if (const auto value =
        require_string(object, "coverageScope", report, "region.manifest", "Region manifest")) {
    manifest.coverage_scope = *value;
  }
  if (manifest.coverage_scope != "pilot-region" &&
      manifest.coverage_scope != "czech-republic") {
    add_issue(report,
              "error",
              "region.manifest.coverageScope.unsupported",
              "Region manifest coverageScope must be 'pilot-region' or 'czech-republic'.");
  }
  if (const auto value =
        require_string(object, "packageMode", report, "region.manifest", "Region manifest")) {
    manifest.package_mode = *value;
  }
  if (manifest.package_mode != "private-local" && manifest.package_mode != "distributable") {
    add_issue(report,
              "error",
              "region.manifest.packageMode.unsupported",
              "Region manifest packageMode must be 'private-local' or 'distributable'.");
  }

  if (const JsonValue::Object* bounds =
        require_object(object, "bounds", report, "region.manifest", "Region manifest")) {
    if (const auto crs = require_string(*bounds, "crs", report, "region.manifest.bounds", "Region WGS-84 bounds");
        crs.has_value() && *crs != "EPSG:4326") {
      add_issue(report,
                "error",
                "region.manifest.bounds.crs.unsupported",
                "Region bounds must declare WGS-84 longitude/latitude CRS as EPSG:4326.");
    }
    const auto min_lon =
      require_number(*bounds, "minLonDeg", report, "region.manifest.bounds", "Region WGS-84 bounds");
    const auto max_lon =
      require_number(*bounds, "maxLonDeg", report, "region.manifest.bounds", "Region WGS-84 bounds");
    const auto min_lat =
      require_number(*bounds, "minLatDeg", report, "region.manifest.bounds", "Region WGS-84 bounds");
    const auto max_lat =
      require_number(*bounds, "maxLatDeg", report, "region.manifest.bounds", "Region WGS-84 bounds");
    if (min_lon.has_value() && max_lon.has_value() && min_lat.has_value() &&
        max_lat.has_value() && (*max_lon <= *min_lon || *max_lat <= *min_lat)) {
      add_issue(report,
                "error",
                "region.manifest.bounds.invalid",
                "Region WGS-84 bounds must have positive longitude and latitude extents.");
    }
  }

  if (const auto bounds =
        parse_bounds(object, "projectBounds", report, "region.manifest", "Region manifest")) {
    manifest.project_bounds = *bounds;
  }

  if (const JsonValue::Object* scope =
        require_object(object, "sourceScope", report, "region.manifest", "Region manifest")) {
    if (const auto margin =
          require_number(*scope, "allowedMarginM", report, "region.manifest.sourceScope", "Region source scope")) {
      manifest.source_margin_m = *margin;
      if (*margin < 0.0) {
        add_issue(report,
                  "error",
                  "region.manifest.sourceScope.allowedMarginM.invalid",
                  "Region source-scope allowedMarginM must be zero or greater.");
      }
    }
    const JsonValue* reject_synthetic = find_member(*scope, "rejectSyntheticFixtures");
    if (reject_synthetic == nullptr || reject_synthetic->type != JsonValue::Type::bool_value ||
        !reject_synthetic->boolean) {
      add_issue(report,
                "error",
                "region.manifest.sourceScope.rejectSyntheticFixtures.required",
                "Production region manifests must reject synthetic fixture inputs.");
    }
  }

  if (const JsonValue::Object* data_root =
        require_object(object, "dataRoot", report, "region.manifest", "Region manifest")) {
    if (const auto value =
          require_string(*data_root, "installVariable", report, "region.manifest.dataRoot", "Region data root")) {
      manifest.data_root_variable = *value;
    }
    if (manifest.data_root_variable != "FLYING_DATA_ROOT") {
      add_issue(report,
                "error",
                "region.manifest.dataRoot.installVariable.unsupported",
                "Region package installation must use the configured FLYING_DATA_ROOT variable.");
    }
    if (const auto value = require_string(*data_root,
                                          "defaultRelativePath",
                                          report,
                                          "region.manifest.dataRoot",
                                          "Region data root")) {
      manifest.data_root_relative_path = *value;
      (void)validate_local_input_path(manifest.data_root_relative_path,
                                      report,
                                      "region.manifest.dataRoot.defaultRelativePath",
                                      "Region data-root default path");
    }
  }

  if (const JsonValue::Object* runtime = require_object(object,
                                                       "runtimeCompatibility",
                                                       report,
                                                       "region.manifest",
                                                       "Region manifest")) {
    if (const auto value = require_string(*runtime,
                                          "minAppVersion",
                                          report,
                                          "region.manifest.runtimeCompatibility",
                                          "Region runtime compatibility")) {
      manifest.min_app_version = *value;
    }
    const JsonValue* network = find_member(*runtime, "runtimeNetworkRequired");
    if (network == nullptr || network->type != JsonValue::Type::bool_value) {
      add_issue(report,
                "error",
                "region.manifest.runtimeCompatibility.runtimeNetworkRequired.missing",
                "Region runtime compatibility must declare runtimeNetworkRequired.");
    } else {
      manifest.runtime_network_required = network->boolean;
      if (manifest.runtime_network_required) {
        add_issue(report,
                  "error",
                  "region.manifest.runtimeCompatibility.runtimeNetworkRequired.invalid",
                  "Installed region packages must support offline runtime launch.");
      }
    }
  }

  return manifest;
}

bool bounds_within(const Bounds& candidate, const Bounds& allowed) {
  return candidate.min_east_m >= allowed.min_east_m &&
         candidate.max_east_m <= allowed.max_east_m &&
         candidate.min_north_m >= allowed.min_north_m &&
         candidate.max_north_m <= allowed.max_north_m;
}

Bounds expand_bounds(const Bounds& bounds, double margin_m) {
  return {
    bounds.min_east_m - margin_m,
    bounds.max_east_m + margin_m,
    bounds.min_north_m - margin_m,
    bounds.max_north_m + margin_m,
  };
}

void validate_source_scope(const PilotPackageConfig& config,
                           const RegionManifest& region,
                           ValidationReport& report) {
  if (config.coverage_scope != region.coverage_scope) {
    add_issue(report,
              "error",
              "pilot.region.coverageScope.mismatch",
              "Package config coverageScope must match the selected region manifest.",
              region.region_id);
  }
  if (config.pilot_region.id != region.region_id) {
    add_issue(report,
              "error",
              "pilot.region.id.mismatch",
              "Package config pilotRegion.id must match the selected region manifest regionId.",
              region.region_id);
  }

  const Bounds config_bounds{
    config.pilot_region.min_east_m,
    config.pilot_region.max_east_m(),
    config.pilot_region.min_north_m,
    config.pilot_region.max_north_m(),
  };
  if (!bounds_within(config_bounds, region.project_bounds) ||
      !bounds_within(region.project_bounds, config_bounds)) {
    add_issue(report,
              "error",
              "pilot.region.projectBounds.mismatch",
              "Package config local bounds must match the selected region manifest projectBounds.",
              region.region_id);
  }

  const Bounds allowed = expand_bounds(region.project_bounds, region.source_margin_m);
  for (const OrthoSourceConfig& source : config.ortho.sources) {
    if (!bounds_within(source.bounds, allowed)) {
      add_issue(report,
                "error",
                "pilot.sourceScope.orthoImagery.out_of_region",
                "Ortofoto source bounds exceed the selected region bounds plus explicit source margin.",
                source.id);
    }
  }
  for (const VectorLayerConfig& layer : config.vector_layers) {
    if (!layer.bounds.has_value()) {
      add_issue(report,
                "error",
                "pilot.sourceScope.vectorLayers.bounds_missing",
                "Region-aware package validation requires vector source bounds for source-scope enforcement.",
                layer.category);
      continue;
    }
    if (!bounds_within(*layer.bounds, allowed)) {
      add_issue(report,
                "error",
                "pilot.sourceScope.vectorLayers.out_of_region",
                "Vector source bounds exceed the selected region bounds plus explicit source margin.",
                layer.category);
    }
  }
  if (!config.geonames.bounds.has_value()) {
    add_issue(report,
              "error",
              "pilot.sourceScope.geonamesLabels.bounds_missing",
              "Region-aware package validation requires Geonames source bounds for source-scope enforcement.");
  } else if (!bounds_within(*config.geonames.bounds, allowed)) {
    add_issue(report,
              "error",
              "pilot.sourceScope.geonamesLabels.out_of_region",
              "Geonames source bounds exceed the selected region bounds plus explicit source margin.");
  }
  for (const PackageInputConfig& input : config.package_inputs) {
    if (!input.bounds.has_value()) {
      add_issue(report,
                "error",
                "pilot.sourceScope.packageInputs.bounds_missing",
                "Region-aware package validation requires package input bounds for terrain, airport, runway, navigation, mask and world-object source-scope enforcement.",
                input.role);
      continue;
    }
    if (!bounds_within(*input.bounds, allowed)) {
      add_issue(report,
                "error",
                "pilot.sourceScope.packageInputs.out_of_region",
                "Package input bounds exceed the selected region bounds plus explicit source margin.",
                input.role);
    }
  }
}

using SourceByPath = std::map<std::string, const SourceDataset*>;

SourceByPath index_sources_by_path(const SourceManifest& manifest, ValidationReport& report) {
  SourceByPath sources_by_path;
  for (const SourceDataset& source : manifest.sources) {
    const std::string path = normalized_relative_path_string(source.checksum.path);
    if (!sources_by_path.emplace(path, &source).second) {
      add_issue(report,
                "error",
                "pilot.source_manifest.path.duplicate",
                "Validated source manifest contains duplicate checksum paths.",
                source.id);
    }
  }
  return sources_by_path;
}

void bind_source_path(const SourceByPath& sources_by_path,
                      const std::filesystem::path& source_path,
                      std::string& source_manifest_id,
                      ValidationReport& report,
                      std::string_view code_prefix,
                      std::string_view subject) {
  const std::string normalized = normalized_relative_path_string(source_path);
  const auto source = sources_by_path.find(normalized);
  if (source == sources_by_path.end()) {
    add_issue(report,
              "error",
              std::string{code_prefix} + ".source_manifest.path_missing",
              std::string{subject} + " source path is not declared in the validated source manifest.",
              normalized);
    return;
  }
  source_manifest_id = source->second->id;
}

void bind_config_sources(PilotPackageConfig& config,
                         const SourceManifest& manifest,
                         ValidationReport& report) {
  const SourceByPath sources_by_path = index_sources_by_path(manifest, report);
  for (OrthoSourceConfig& source : config.ortho.sources) {
    bind_source_path(sources_by_path,
                     source.path,
                     source.source_manifest_id,
                     report,
                     "pilot.ortho",
                     "Ortofoto");
  }
  for (VectorLayerConfig& layer : config.vector_layers) {
    bind_source_path(sources_by_path,
                     layer.path,
                     layer.source_manifest_id,
                     report,
                     "pilot.vector",
                     "ZABAGED vector");
  }
  bind_source_path(sources_by_path,
                   config.geonames.path,
                   config.geonames.source_manifest_id,
                   report,
                   "pilot.geonames",
                   "Geonames labels");
  for (PackageInputConfig& input : config.package_inputs) {
    bind_source_path(sources_by_path,
                     input.path,
                     input.source_manifest_id,
                     report,
                     "pilot.packageInput",
                     "Package input");
  }
}

std::vector<const SourceDataset*> lineage_sources(const SourceManifest& manifest,
                                                  const std::set<std::string>& source_ids) {
  std::vector<const SourceDataset*> sources;
  for (const SourceDataset& source : manifest.sources) {
    if (source_ids.find(source.id) != source_ids.end()) {
      sources.push_back(&source);
    }
  }
  std::sort(sources.begin(), sources.end(), [](const SourceDataset* lhs, const SourceDataset* rhs) {
    return lhs->id < rhs->id;
  });
  return sources;
}

std::string render_checksum_json(const ChecksumDeclaration& checksum) {
  std::ostringstream output;
  output << "{\"algorithm\":" << json_quote(checksum.algorithm)
         << ",\"path\":" << json_quote(checksum.path)
         << ",\"value\":" << json_quote(checksum.value) << "}";
  return output.str();
}

std::string render_source_lineage(const SourceManifest& manifest,
                                  const std::set<std::string>& source_ids,
                                  std::string_view indent) {
  const std::vector<const SourceDataset*> sources = lineage_sources(manifest, source_ids);
  std::ostringstream output;
  output << "[\n";
  for (std::size_t i = 0; i < sources.size(); ++i) {
    const SourceDataset& source = *sources[i];
    output << indent << "  {\n";
    output << indent << "    \"sourceId\": " << json_quote(source.id) << ",\n";
    output << indent << "    \"datasetName\": " << json_quote(source.dataset_name) << ",\n";
    output << indent << "    \"version\": " << json_quote(source.version) << ",\n";
    output << indent << "    \"license\": " << source.license_json << ",\n";
    output << indent << "    \"attribution\": " << source.attribution_json << ",\n";
    output << indent << "    \"checksum\": " << render_checksum_json(source.checksum) << ",\n";
    output << indent << "    \"coordinateReferenceSystem\": "
           << source.coordinate_reference_system_json << ",\n";
    output << indent << "    \"permittedUse\": " << source.permitted_use_json << ",\n";
    output << indent << "    \"provenance\": " << source.provenance_json << "\n";
    output << indent << "  }";
    if (i + 1U != sources.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "]";
  return output.str();
}

struct Pixel {
  int red = 0;
  int green = 0;
  int blue = 0;
};

struct Image {
  int width = 0;
  int height = 0;
  std::vector<Pixel> pixels;

  [[nodiscard]] Pixel at(int x, int y) const {
    return pixels[static_cast<std::size_t>(y * width + x)];
  }
};

class PpmTokenReader {
 public:
  explicit PpmTokenReader(std::string_view text) : text_(text) {}

  std::string next() {
    skip_whitespace_and_comments();
    if (index_ >= text_.size()) {
      throw std::runtime_error("unexpected end of PPM image");
    }
    const std::size_t begin = index_;
    while (index_ < text_.size()) {
      const unsigned char ch = static_cast<unsigned char>(text_[index_]);
      if (std::isspace(ch) != 0 || ch == '#') {
        break;
      }
      ++index_;
    }
    return std::string{text_.substr(begin, index_ - begin)};
  }

 private:
  void skip_whitespace_and_comments() {
    while (index_ < text_.size()) {
      const unsigned char ch = static_cast<unsigned char>(text_[index_]);
      if (std::isspace(ch) != 0) {
        ++index_;
        continue;
      }
      if (text_[index_] == '#') {
        while (index_ < text_.size() && text_[index_] != '\n') {
          ++index_;
        }
        continue;
      }
      return;
    }
  }

  std::string_view text_;
  std::size_t index_ = 0U;
};

int parse_int_token(PpmTokenReader& reader, std::string_view subject) {
  const std::string token = reader.next();
  std::size_t parsed = 0U;
  const int value = std::stoi(token, &parsed);
  if (parsed != token.size()) {
    throw std::runtime_error(std::string{subject} + " must be an integer");
  }
  return value;
}

Image read_ppm_p3(const std::filesystem::path& path) {
  const std::string image_text = read_text_file(path);
  PpmTokenReader reader{image_text};
  if (reader.next() != "P3") {
    throw std::runtime_error("pilot Ortofoto source " + path.string() +
                             " must use PPM P3 encoding");
  }

  Image image;
  image.width = parse_int_token(reader, "PPM width");
  image.height = parse_int_token(reader, "PPM height");
  const int max_value = parse_int_token(reader, "PPM max value");
  if (image.width <= 0 || image.height <= 0 || max_value <= 0) {
    throw std::runtime_error("PPM image dimensions and max value must be positive");
  }

  image.pixels.reserve(static_cast<std::size_t>(image.width * image.height));
  for (int i = 0; i < image.width * image.height; ++i) {
    Pixel pixel;
    pixel.red = std::clamp((parse_int_token(reader, "PPM red channel") * 255) / max_value,
                           0,
                           255);
    pixel.green =
      std::clamp((parse_int_token(reader, "PPM green channel") * 255) / max_value, 0, 255);
    pixel.blue =
      std::clamp((parse_int_token(reader, "PPM blue channel") * 255) / max_value, 0, 255);
    image.pixels.push_back(pixel);
  }
  return image;
}

Image downsample_image(const Image& source, int scale) {
  if (scale <= 1) {
    return source;
  }
  Image output;
  output.width = std::max(1, (source.width + scale - 1) / scale);
  output.height = std::max(1, (source.height + scale - 1) / scale);
  output.pixels.resize(static_cast<std::size_t>(output.width * output.height));

  for (int y = 0; y < output.height; ++y) {
    for (int x = 0; x < output.width; ++x) {
      int red_sum = 0;
      int green_sum = 0;
      int blue_sum = 0;
      int count = 0;
      for (int source_y = y * scale; source_y < std::min(source.height, (y + 1) * scale);
           ++source_y) {
        for (int source_x = x * scale; source_x < std::min(source.width, (x + 1) * scale);
             ++source_x) {
          const Pixel pixel = source.at(source_x, source_y);
          red_sum += pixel.red;
          green_sum += pixel.green;
          blue_sum += pixel.blue;
          ++count;
        }
      }
      output.pixels[static_cast<std::size_t>(y * output.width + x)] = {
        red_sum / count,
        green_sum / count,
        blue_sum / count,
      };
    }
  }
  return output;
}

Image make_tile_image(const Image& source,
                      int tile_size_px,
                      int tile_col,
                      int tile_row,
                      int& used_width,
                      int& used_height) {
  const int source_x0 = tile_col * tile_size_px;
  const int source_y0 = tile_row * tile_size_px;
  used_width = std::min(tile_size_px, source.width - source_x0);
  used_height = std::min(tile_size_px, source.height - source_y0);
  if (used_width <= 0 || used_height <= 0) {
    throw std::runtime_error("invalid tile coordinates for Ortofoto source");
  }

  Image tile;
  tile.width = used_width;
  tile.height = used_height;
  tile.pixels.reserve(static_cast<std::size_t>(used_width * used_height));
  for (int y = 0; y < used_height; ++y) {
    for (int x = 0; x < used_width; ++x) {
      tile.pixels.push_back(source.at(source_x0 + x, source_y0 + y));
    }
  }
  return tile;
}

void write_ppm_p3(const std::filesystem::path& path, const Image& image) {
  std::ostringstream output;
  output << "P3\n" << image.width << " " << image.height << "\n255\n";
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const Pixel pixel = image.at(x, y);
      output << pixel.red << " " << pixel.green << " " << pixel.blue;
      if (x + 1 != image.width) {
        output << "  ";
      }
    }
    output << "\n";
  }
  write_text_file(path, output.str());
}

struct FileMetadata {
  std::string id;
  std::string path;
  std::string checksum;
  std::size_t size_bytes = 0U;
};

FileMetadata metadata_for_written_file(const std::filesystem::path& output_root,
                                       const std::filesystem::path& path,
                                       std::string id) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error) {
    throw std::runtime_error("failed to stat generated file " + path.string());
  }
  return {
    std::move(id),
    std::filesystem::relative(path, output_root).generic_string(),
    sha256_file(path),
    static_cast<std::size_t>(size),
  };
}

struct ImageryTileMetadata {
  FileMetadata file;
  int level = 0;
  int row = 0;
  int col = 0;
  int width_px = 0;
  int height_px = 0;
  Bounds bounds;
  std::string source_manifest_id;
};

struct ImageryLodMetadata {
  int level = 0;
  int source_pixel_scale = 1;
  std::vector<ImageryTileMetadata> tiles;
};

struct ImageryPackageMetadata {
  int tile_size_px = 0;
  bool mipmapped = false;
  std::vector<ImageryLodMetadata> lods;
};

Bounds tile_bounds(const Bounds& source_bounds,
                   int source_width,
                   int source_height,
                   int scale,
                   int tile_col,
                   int tile_row,
                   int width_px,
                   int height_px,
                   int tile_size_px) {
  const double image_width_m = source_bounds.max_east_m - source_bounds.min_east_m;
  const double image_height_m = source_bounds.max_north_m - source_bounds.min_north_m;
  const double source_x0 = static_cast<double>(tile_col * tile_size_px * scale);
  const double source_y0 = static_cast<double>(tile_row * tile_size_px * scale);
  const double source_x1 = std::min(static_cast<double>(source_width),
                                    source_x0 + static_cast<double>(width_px * scale));
  const double source_y1 = std::min(static_cast<double>(source_height),
                                    source_y0 + static_cast<double>(height_px * scale));
  return {
    source_bounds.min_east_m + (source_x0 / static_cast<double>(source_width)) * image_width_m,
    source_bounds.min_east_m + (source_x1 / static_cast<double>(source_width)) * image_width_m,
    source_bounds.min_north_m +
      (source_y0 / static_cast<double>(source_height)) * image_height_m,
    source_bounds.min_north_m +
      (source_y1 / static_cast<double>(source_height)) * image_height_m,
  };
}

ImageryPackageMetadata process_ortho_imagery(const PilotRegionPackageOptions& options,
                                             const PilotPackageConfig& config) {
  ImageryPackageMetadata metadata;
  metadata.tile_size_px = config.ortho.tile_size_px;
  metadata.mipmapped = config.ortho.mip_levels > 1;

  for (int level = 0; level < config.ortho.mip_levels; ++level) {
    ImageryLodMetadata lod;
    lod.level = level;
    lod.source_pixel_scale = 1 << level;

    for (const OrthoSourceConfig& source : config.ortho.sources) {
      const Image source_image = read_ppm_p3(options.source_root / source.path);
      const Image lod_image = downsample_image(source_image, lod.source_pixel_scale);
      const int tile_cols =
        (lod_image.width + config.ortho.tile_size_px - 1) / config.ortho.tile_size_px;
      const int tile_rows =
        (lod_image.height + config.ortho.tile_size_px - 1) / config.ortho.tile_size_px;

      for (int row = 0; row < tile_rows; ++row) {
        for (int col = 0; col < tile_cols; ++col) {
          int width_px = 0;
          int height_px = 0;
          const Image tile =
            make_tile_image(lod_image, config.ortho.tile_size_px, col, row, width_px, height_px);
          const std::string tile_id = source.id + "_z" + std::to_string(level) + "_r" +
                                      std::to_string(row) + "_c" + std::to_string(col);
          const std::filesystem::path path = options.output_directory / "imagery" /
                                             ("lod" + std::to_string(level)) /
                                             (tile_id + ".ppm");
          write_ppm_p3(path, tile);
          ImageryTileMetadata tile_metadata;
          tile_metadata.file = metadata_for_written_file(options.output_directory, path, tile_id);
          tile_metadata.level = level;
          tile_metadata.row = row;
          tile_metadata.col = col;
          tile_metadata.width_px = width_px;
          tile_metadata.height_px = height_px;
          tile_metadata.bounds = tile_bounds(source.bounds,
                                             source_image.width,
                                             source_image.height,
                                             lod.source_pixel_scale,
                                             col,
                                             row,
                                             width_px,
                                             height_px,
                                             config.ortho.tile_size_px);
          tile_metadata.source_manifest_id = source.source_manifest_id;
          lod.tiles.push_back(std::move(tile_metadata));
        }
      }
    }
    metadata.lods.push_back(std::move(lod));
  }

  return metadata;
}

struct LocalGeometry {
  std::string type;
  std::vector<std::vector<Point2>> parts;
};

struct VectorFeature {
  std::string id;
  std::string name;
  std::string category;
  std::string properties_json = "{}";
  LocalGeometry geometry;
  double line_width_m = 0.0;
  std::string source_manifest_id;
};

struct VectorLayerOutput {
  std::string category;
  FileMetadata file;
  std::string source_manifest_id;
  std::vector<VectorFeature> features;
};

struct LabelFeature {
  std::string id;
  std::string name;
  std::string feature_class;
  Point2 position;
  std::string source_manifest_id;
};

struct LabelOutput {
  FileMetadata file;
  std::vector<LabelFeature> labels;
  std::string source_manifest_id;
};

struct WorldObjectInstance {
  std::string id;
  std::string kind;
  std::string source_category;
  std::string source_feature_id;
  std::string source_manifest_id;
  Point2 position;
  LocalGeometry geometry;
  double height_m = 0.0;
  bool density_scalable = false;
  bool flight_critical = false;
  std::string collision_policy;
  std::string audio_hook;
  double visible_distance_m = 0.0;
};

struct WorldObjectsOutput {
  FileMetadata file;
  std::vector<WorldObjectInstance> objects;
};

std::string render_bounds(const Bounds& bounds);
std::set<std::string> collect_used_source_ids(const PilotPackageConfig& config);

std::optional<double> numeric_array_value(const JsonValue::Array& array,
                                          std::size_t index,
                                          std::string_view subject) {
  if (index >= array.size() || array[index].type != JsonValue::Type::number_value) {
    throw std::runtime_error(std::string{subject} + " coordinate must be numeric");
  }
  return array[index].number;
}

Point2 parse_projected_coordinate(const JsonValue& value,
                                  const SourceToProjectTransform& transform) {
  if (value.type != JsonValue::Type::array_value || value.array.size() < 2U) {
    throw std::runtime_error("GeoJSON coordinate must be an array with x and y");
  }
  return transform.apply(*numeric_array_value(value.array, 0U, "GeoJSON"),
                         *numeric_array_value(value.array, 1U, "GeoJSON"));
}

std::vector<Point2> parse_coordinate_line(const JsonValue& value,
                                          const SourceToProjectTransform& transform) {
  if (value.type != JsonValue::Type::array_value) {
    throw std::runtime_error("GeoJSON coordinate line must be an array");
  }
  std::vector<Point2> line;
  line.reserve(value.array.size());
  for (const JsonValue& coordinate : value.array) {
    line.push_back(parse_projected_coordinate(coordinate, transform));
  }
  return line;
}

LocalGeometry parse_local_geometry(const JsonValue::Object& geometry,
                                   const SourceToProjectTransform& transform) {
  const JsonValue* type_value = find_member(geometry, "type");
  const JsonValue* coordinates_value = find_member(geometry, "coordinates");
  if (type_value == nullptr || type_value->type != JsonValue::Type::string_value ||
      coordinates_value == nullptr) {
    throw std::runtime_error("GeoJSON geometry requires type and coordinates");
  }

  LocalGeometry output;
  output.type = type_value->string;
  if (output.type == "Point") {
    output.parts.push_back({parse_projected_coordinate(*coordinates_value, transform)});
  } else if (output.type == "MultiPoint" || output.type == "LineString") {
    output.parts.push_back(parse_coordinate_line(*coordinates_value, transform));
  } else if (output.type == "MultiLineString" || output.type == "Polygon") {
    if (coordinates_value->type != JsonValue::Type::array_value) {
      throw std::runtime_error("GeoJSON nested coordinates must be an array");
    }
    for (const JsonValue& part : coordinates_value->array) {
      output.parts.push_back(parse_coordinate_line(part, transform));
    }
  } else if (output.type == "MultiPolygon") {
    if (coordinates_value->type != JsonValue::Type::array_value) {
      throw std::runtime_error("GeoJSON multipolygon coordinates must be an array");
    }
    for (const JsonValue& polygon : coordinates_value->array) {
      if (polygon.type != JsonValue::Type::array_value) {
        throw std::runtime_error("GeoJSON multipolygon polygon must be an array");
      }
      for (const JsonValue& ring : polygon.array) {
        output.parts.push_back(parse_coordinate_line(ring, transform));
      }
    }
  } else {
    throw std::runtime_error("unsupported GeoJSON geometry type: " + output.type);
  }
  return output;
}

bool geometry_intersects_region(const LocalGeometry& geometry, const PilotRegion& region) {
  bool has_point = false;
  Bounds bounds;
  bounds.min_east_m = std::numeric_limits<double>::infinity();
  bounds.min_north_m = std::numeric_limits<double>::infinity();
  bounds.max_east_m = -std::numeric_limits<double>::infinity();
  bounds.max_north_m = -std::numeric_limits<double>::infinity();
  for (const std::vector<Point2>& part : geometry.parts) {
    for (const Point2 point : part) {
      has_point = true;
      if (region.contains(point)) {
        return true;
      }
      bounds.min_east_m = std::min(bounds.min_east_m, point.east_m);
      bounds.max_east_m = std::max(bounds.max_east_m, point.east_m);
      bounds.min_north_m = std::min(bounds.min_north_m, point.north_m);
      bounds.max_north_m = std::max(bounds.max_north_m, point.north_m);
    }
  }
  if (!has_point) {
    return false;
  }
  return !(bounds.max_east_m < region.min_east_m || bounds.min_east_m > region.max_east_m() ||
           bounds.max_north_m < region.min_north_m ||
           bounds.min_north_m > region.max_north_m());
}

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

std::string optional_property_string(const JsonValue::Object& properties,
                                     std::string_view key) {
  const JsonValue* value = find_member(properties, key);
  if (value == nullptr || value->type != JsonValue::Type::string_value) {
    return {};
  }
  return value->string;
}

std::optional<double> optional_property_number(const std::string& properties_json,
                                               std::string_view key) {
  JsonValue properties = JsonParser{properties_json}.parse();
  if (properties.type != JsonValue::Type::object_value) {
    return std::nullopt;
  }
  const JsonValue* value = find_member(properties.object, key);
  if (value == nullptr || value->type != JsonValue::Type::number_value) {
    return std::nullopt;
  }
  return value->number;
}

std::string optional_property_string_from_json(const std::string& properties_json,
                                               std::string_view key) {
  JsonValue properties = JsonParser{properties_json}.parse();
  if (properties.type != JsonValue::Type::object_value) {
    return {};
  }
  return optional_property_string(properties.object, key);
}

std::optional<JsonValue> sanitize_runtime_json_value(const JsonValue& value) {
  if (value.type == JsonValue::Type::string_value) {
    if (looks_like_remote_tile_reference(value.string) ||
        lowercase_ascii(value.string).find("mapbox") != std::string::npos) {
      return std::nullopt;
    }
    return value;
  }
  if (value.type == JsonValue::Type::array_value) {
    JsonValue sanitized;
    sanitized.type = JsonValue::Type::array_value;
    sanitized.array.reserve(value.array.size());
    for (const JsonValue& child : value.array) {
      if (std::optional<JsonValue> safe_child = sanitize_runtime_json_value(child)) {
        sanitized.array.push_back(std::move(*safe_child));
      }
    }
    return sanitized;
  }
  if (value.type == JsonValue::Type::object_value) {
    JsonValue sanitized;
    sanitized.type = JsonValue::Type::object_value;
    for (const auto& [key, child] : value.object) {
      if (is_disallowed_runtime_reference_key(key)) {
        continue;
      }
      if (std::optional<JsonValue> safe_child = sanitize_runtime_json_value(child)) {
        sanitized.object.emplace(key, std::move(*safe_child));
      }
    }
    return sanitized;
  }
  return value;
}

JsonValue sanitize_runtime_properties(const JsonValue& properties) {
  if (std::optional<JsonValue> sanitized = sanitize_runtime_json_value(properties);
      sanitized.has_value() && sanitized->type == JsonValue::Type::object_value) {
    return *sanitized;
  }
  JsonValue empty_object;
  empty_object.type = JsonValue::Type::object_value;
  return empty_object;
}

std::string render_point(Point2 point) {
  std::ostringstream output;
  output << "{\"eastM\":" << render_number(point.east_m)
         << ",\"northM\":" << render_number(point.north_m) << "}";
  return output.str();
}

std::string render_geometry(const LocalGeometry& geometry, std::string_view indent) {
  std::ostringstream output;
  output << "{\n";
  output << indent << "  \"type\": " << json_quote(geometry.type) << ",\n";
  output << indent << "  \"parts\": [\n";
  for (std::size_t i = 0; i < geometry.parts.size(); ++i) {
    output << indent << "    [";
    for (std::size_t j = 0; j < geometry.parts[i].size(); ++j) {
      if (j != 0U) {
        output << ", ";
      }
      output << render_point(geometry.parts[i][j]);
    }
    output << "]";
    if (i + 1U != geometry.parts.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "  ]\n";
  output << indent << "}";
  return output.str();
}

std::string render_vector_package_json(const SourceManifest& source_manifest,
                                       const VectorLayerOutput& layer) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(kVectorSchemaVersion) << ",\n";
  output << "  \"category\": " << json_quote(layer.category) << ",\n";
  output << "  \"coordinateFrame\": \"project-local-ENU\",\n";
  output << "  \"sourceLineage\": "
         << render_source_lineage(source_manifest, {layer.source_manifest_id}, "  ") << ",\n";
  output << "  \"features\": [\n";
  for (std::size_t i = 0; i < layer.features.size(); ++i) {
    const VectorFeature& feature = layer.features[i];
    output << "    {\n";
    output << "      \"id\": " << json_quote(feature.id) << ",\n";
    output << "      \"name\": " << json_quote(feature.name) << ",\n";
    output << "      \"category\": " << json_quote(feature.category) << ",\n";
    output << "      \"properties\": " << feature.properties_json << ",\n";
    output << "      \"geometry\": " << render_geometry(feature.geometry, "      ") << "\n";
    output << "    }";
    if (i + 1U != layer.features.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ]\n";
  output << "}\n";
  return output.str();
}

VectorLayerOutput process_vector_layer(const PilotRegionPackageOptions& options,
                                       const PilotPackageConfig& config,
                                       const SourceManifest& source_manifest,
                                       const VectorLayerConfig& layer_config) {
  const JsonValue root = JsonParser{read_text_file(options.source_root / layer_config.path)}.parse();
  if (root.type != JsonValue::Type::object_value) {
    throw std::runtime_error("GeoJSON vector layer root must be an object");
  }
  const JsonValue* type = find_member(root.object, "type");
  const JsonValue* features = find_member(root.object, "features");
  if (type == nullptr || type->type != JsonValue::Type::string_value ||
      type->string != "FeatureCollection" || features == nullptr ||
      features->type != JsonValue::Type::array_value) {
    throw std::runtime_error("GeoJSON vector layer must be a FeatureCollection");
  }

  VectorLayerOutput output;
  output.category = layer_config.category;
  output.source_manifest_id = layer_config.source_manifest_id;
  std::size_t generated_id = 0U;
  for (const JsonValue& feature_value : features->array) {
    if (feature_value.type != JsonValue::Type::object_value) {
      throw std::runtime_error("GeoJSON feature must be an object");
    }
    const JsonValue* geometry_value = find_member(feature_value.object, "geometry");
    if (geometry_value == nullptr || geometry_value->type != JsonValue::Type::object_value) {
      throw std::runtime_error("GeoJSON feature geometry must be an object");
    }
    VectorFeature feature;
    feature.category = layer_config.category;
    feature.line_width_m = layer_config.line_width_m;
    feature.source_manifest_id = layer_config.source_manifest_id;
    feature.geometry = parse_local_geometry(geometry_value->object, config.transform);
    if (!geometry_intersects_region(feature.geometry, config.pilot_region)) {
      continue;
    }
    if (const JsonValue* id = find_member(feature_value.object, "id");
        id != nullptr && id->type == JsonValue::Type::string_value) {
      feature.id = id->string;
    } else {
      feature.id = layer_config.category + "-" + std::to_string(generated_id++);
    }
    if (const JsonValue* properties = find_member(feature_value.object, "properties");
        properties != nullptr && properties->type == JsonValue::Type::object_value) {
      feature.properties_json = render_canonical_json(sanitize_runtime_properties(*properties));
      feature.name = optional_property_string(properties->object, "name");
    }
    output.features.push_back(std::move(feature));
  }

  const std::filesystem::path path =
    options.output_directory / "vectors" / (layer_config.category + ".json");
  write_text_file(path, render_vector_package_json(source_manifest, output));
  output.file = metadata_for_written_file(options.output_directory, path, layer_config.category);
  return output;
}

Bounds geometry_bounds(const LocalGeometry& geometry) {
  Bounds bounds;
  bounds.min_east_m = std::numeric_limits<double>::infinity();
  bounds.min_north_m = std::numeric_limits<double>::infinity();
  bounds.max_east_m = -std::numeric_limits<double>::infinity();
  bounds.max_north_m = -std::numeric_limits<double>::infinity();
  bool has_point = false;
  for (const std::vector<Point2>& part : geometry.parts) {
    for (const Point2 point : part) {
      has_point = true;
      bounds.min_east_m = std::min(bounds.min_east_m, point.east_m);
      bounds.max_east_m = std::max(bounds.max_east_m, point.east_m);
      bounds.min_north_m = std::min(bounds.min_north_m, point.north_m);
      bounds.max_north_m = std::max(bounds.max_north_m, point.north_m);
    }
  }
  if (!has_point) {
    return {};
  }
  return bounds;
}

Point2 geometry_anchor(const LocalGeometry& geometry) {
  double east_sum = 0.0;
  double north_sum = 0.0;
  std::size_t point_count = 0U;
  for (const std::vector<Point2>& part : geometry.parts) {
    for (const Point2 point : part) {
      east_sum += point.east_m;
      north_sum += point.north_m;
      ++point_count;
    }
  }
  if (point_count == 0U) {
    return {};
  }
  return {east_sum / static_cast<double>(point_count),
          north_sum / static_cast<double>(point_count)};
}

std::string lower_properties_kind(const VectorFeature& feature) {
  const std::string kind = optional_property_string_from_json(feature.properties_json, "kind");
  if (!kind.empty()) {
    return lowercase_ascii(kind);
  }
  return lowercase_ascii(optional_property_string_from_json(feature.properties_json, "type"));
}

std::string lower_feature_classification(const VectorFeature& feature) {
  std::string classification = lower_properties_kind(feature);
  if (!feature.name.empty()) {
    if (!classification.empty()) {
      classification.push_back(' ');
    }
    classification += lowercase_ascii(feature.name);
  }
  return classification;
}

double dmp_height_for_feature(const VectorFeature& feature,
                              const PilotPackageConfig& config,
                              double fallback_height_m) {
  if (const std::optional<double> height = optional_property_number(feature.properties_json, "heightM")) {
    return std::max(0.5, *height);
  }
  if (const std::optional<double> height = optional_property_number(feature.properties_json, "dmpHeightM")) {
    return std::max(0.5, *height);
  }
  (void)config;
  return fallback_height_m;
}

void append_world_object(WorldObjectsOutput& output,
                         const VectorFeature& feature,
                         std::string kind,
                         double height_m,
                         bool density_scalable,
                         bool flight_critical,
                         std::string collision_policy,
                         std::string audio_hook,
                         double visible_distance_m) {
  WorldObjectInstance object;
  object.id = feature.id + "-" + kind;
  object.kind = std::move(kind);
  object.source_category = feature.category;
  object.source_feature_id = feature.id;
  object.source_manifest_id = feature.source_manifest_id;
  object.position = geometry_anchor(feature.geometry);
  object.geometry = feature.geometry;
  object.height_m = height_m;
  object.density_scalable = density_scalable;
  object.flight_critical = flight_critical;
  object.collision_policy = std::move(collision_policy);
  object.audio_hook = std::move(audio_hook);
  object.visible_distance_m = visible_distance_m;
  output.objects.push_back(std::move(object));
}

std::string render_world_object_instance(const WorldObjectInstance& object,
                                         std::string_view indent) {
  std::ostringstream output;
  output << indent << "{\n";
  output << indent << "  \"id\": " << json_quote(object.id) << ",\n";
  output << indent << "  \"kind\": " << json_quote(object.kind) << ",\n";
  output << indent << "  \"sourceCategory\": " << json_quote(object.source_category) << ",\n";
  output << indent << "  \"sourceFeatureId\": " << json_quote(object.source_feature_id) << ",\n";
  output << indent << "  \"sourceId\": " << json_quote(object.source_manifest_id) << ",\n";
  output << indent << "  \"placementSource\": \"approved_vector_with_dmp_height_estimate\",\n";
  output << indent << "  \"position\": " << render_point(object.position) << ",\n";
  output << indent << "  \"heightM\": " << render_number(object.height_m) << ",\n";
  output << indent << "  \"bounds\": " << render_bounds(geometry_bounds(object.geometry)) << ",\n";
  output << indent << "  \"geometry\": " << render_geometry(object.geometry, std::string{indent} + "  ") << ",\n";
  output << indent << "  \"densityScalable\": " << (object.density_scalable ? "true" : "false") << ",\n";
  output << indent << "  \"flightCritical\": " << (object.flight_critical ? "true" : "false") << ",\n";
  output << indent << "  \"collisionPolicy\": " << json_quote(object.collision_policy) << ",\n";
  output << indent << "  \"visibleDistanceM\": " << render_number(object.visible_distance_m) << ",\n";
  output << indent << "  \"audioHook\": " << json_quote(object.audio_hook) << "\n";
  output << indent << "}";
  return output.str();
}

std::string render_world_objects_json(const SourceManifest& source_manifest,
                                      const PilotPackageConfig& config,
                                      const WorldObjectsOutput& world_objects) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": \"flying.world-objects.v1\",\n";
  output << "  \"coordinateFrame\": \"project-local-ENU\",\n";
  output << "  \"placementPolicy\": {\n";
  output << "    \"approvedVectorDataOnly\": true,\n";
  output << "    \"orthoColorInferenceAllowed\": false,\n";
  output << "    \"heightSource\": \"dmp-derived-estimate-with-vector-property-override\"\n";
  output << "  },\n";
  output << "  \"streaming\": {\n";
  output << "    \"addressing\": \"project-local-ENU-tile-bounds\",\n";
  output << "    \"streamInDistanceM\": " << render_number(config.world_objects.stream_in_distance_m) << ",\n";
  output << "    \"streamOutDistanceM\": " << render_number(config.world_objects.stream_out_distance_m) << ",\n";
  output << "    \"hysteresisPreventsHorizonPopping\": true\n";
  output << "  },\n";
  output << "  \"collision\": {\n";
  output << "    \"activeSafetyZoneRadiusM\": " << render_number(config.world_objects.active_collision_radius_m) << ",\n";
  output << "    \"distantObjectsCollision\": false,\n";
  output << "    \"flightCriticalObjectsIgnoreDensityScale\": true\n";
  output << "  },\n";
  output << "  \"graphicsProfiles\": [\n";
  for (std::size_t i = 0; i < config.world_objects.graphics_profiles.size(); ++i) {
    const GraphicsDensityProfileConfig& profile = config.world_objects.graphics_profiles[i];
    output << "    {\"id\":" << json_quote(profile.id)
           << ",\"vegetationDensityScale\":" << render_number(profile.vegetation_density_scale)
           << ",\"objectDensityScale\":" << render_number(profile.object_density_scale)
           << ",\"flightCriticalDensityScale\":1}";
    if (i + 1U != config.world_objects.graphics_profiles.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"environmentAudioHooks\": [\"water_ambience\", \"vegetation_wind\", \"airport_windsock_wind\"],\n";
  output << "  \"airportObjectSources\": [\"detailed-airport-manifest:windsocks\", \"runway-surfaces:runwayObjects\"],\n";
  output << "  \"sourceLineage\": "
         << render_source_lineage(source_manifest, collect_used_source_ids(config), "  ") << ",\n";
  output << "  \"objects\": [\n";
  for (std::size_t i = 0; i < world_objects.objects.size(); ++i) {
    output << render_world_object_instance(world_objects.objects[i], "    ");
    if (i + 1U != world_objects.objects.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ]\n";
  output << "}\n";
  return output.str();
}

WorldObjectsOutput process_world_objects(const PilotRegionPackageOptions& options,
                                         const SourceManifest& source_manifest,
                                         const PilotPackageConfig& config,
                                         const std::vector<VectorLayerOutput>& vector_layers) {
  WorldObjectsOutput output;
  if (!config.world_objects.enabled) {
    const std::filesystem::path path = options.output_directory / "world" / "world-objects.json";
    write_text_file(path, render_world_objects_json(source_manifest, config, output));
    output.file = metadata_for_written_file(options.output_directory, path, "world-objects");
    return output;
  }

  for (const VectorLayerOutput& layer : vector_layers) {
    for (const VectorFeature& feature : layer.features) {
      if (layer.category == "settlements") {
        append_world_object(output,
                            feature,
                            "building",
                            dmp_height_for_feature(
                              feature, config, config.world_objects.default_building_height_m),
                            true,
                            false,
                            "active_safety_zone",
                            "settlement_ambience",
                            config.world_objects.stream_in_distance_m);
      } else if (layer.category == "vegetationAreas") {
        append_world_object(output,
                            feature,
                            "vegetation",
                            dmp_height_for_feature(
                              feature, config, config.world_objects.default_vegetation_height_m),
                            true,
                            false,
                            "visual_only",
                            "vegetation_wind",
                            config.world_objects.stream_in_distance_m * 0.75);
      } else if (layer.category == "water") {
        append_world_object(output,
                            feature,
                            "water_surface",
                            0.0,
                            false,
                            true,
                            "water_surface_active_zone",
                            "water_ambience",
                            config.world_objects.stream_out_distance_m);
      } else if (layer.category == "notableObjects") {
        std::string kind = lower_feature_classification(feature);
        if (kind.find("power") != std::string::npos || kind.find("line") != std::string::npos) {
          kind = "power_line";
        } else if (kind.find("windsock") != std::string::npos ||
                   kind.find("wind sock") != std::string::npos) {
          kind = "windsock";
        } else if (kind.find("mast") != std::string::npos || kind.find("tower") != std::string::npos) {
          kind = "mast";
        } else {
          kind = "obstacle";
        }
        const double fallback_height =
          kind == "power_line" ? config.world_objects.default_power_line_height_m
                               : config.world_objects.default_obstacle_height_m;
        append_world_object(output,
                            feature,
                            kind,
                            dmp_height_for_feature(feature, config, fallback_height),
                            false,
                            true,
                            "active_safety_zone",
                            kind == "windsock" ? "airport_windsock_wind"
                                               : "obstacle_warning_context",
                            config.world_objects.stream_out_distance_m);
      } else if (layer.category == "significantObstacle" || layer.category == "obstacle") {
        std::string kind = lower_feature_classification(feature);
        if (kind.find("power") != std::string::npos || kind.find("line") != std::string::npos) {
          kind = "power_line";
        } else if (kind.find("mast") != std::string::npos || kind.find("tower") != std::string::npos) {
          kind = "mast";
        } else {
          kind = "obstacle";
        }
        const double fallback_height =
          kind == "power_line" ? config.world_objects.default_power_line_height_m
                               : config.world_objects.default_obstacle_height_m;
        append_world_object(output,
                            feature,
                            kind,
                            dmp_height_for_feature(feature, config, fallback_height),
                            false,
                            true,
                            "active_safety_zone",
                            "obstacle_warning_context",
                            config.world_objects.stream_out_distance_m);
      } else if (layer.category == "airport") {
        const std::string classification = lower_feature_classification(feature);
        const bool is_windsock = classification.find("windsock") != std::string::npos ||
                                 classification.find("wind sock") != std::string::npos;
        append_world_object(output,
                            feature,
                            is_windsock ? "windsock" : "runway_object",
                            dmp_height_for_feature(
                              feature, config, config.world_objects.default_obstacle_height_m),
                            false,
                            true,
                            "active_safety_zone",
                            is_windsock ? "airport_windsock_wind" : "airport_surface_context",
                            config.world_objects.stream_out_distance_m);
      } else if (layer.category == "runway") {
        append_world_object(output,
                            feature,
                            "runway_object",
                            dmp_height_for_feature(feature, config, 0.5),
                            false,
                            true,
                            "active_safety_zone",
                            "airport_surface_context",
                            config.world_objects.stream_out_distance_m);
      }
    }
  }

  const std::filesystem::path path = options.output_directory / "world" / "world-objects.json";
  write_text_file(path, render_world_objects_json(source_manifest, config, output));
  output.file = metadata_for_written_file(options.output_directory, path, "world-objects");
  return output;
}

std::vector<std::string> parse_csv_row(std::string_view line) {
  std::vector<std::string> fields;
  std::string current;
  bool in_quotes = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (ch == '"') {
      if (in_quotes && i + 1U < line.size() && line[i + 1U] == '"') {
        current.push_back('"');
        ++i;
      } else {
        in_quotes = !in_quotes;
      }
    } else if (ch == ',' && !in_quotes) {
      fields.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  fields.push_back(current);
  return fields;
}

std::map<std::string, std::size_t> csv_header_index(const std::vector<std::string>& header) {
  std::map<std::string, std::size_t> index;
  for (std::size_t i = 0; i < header.size(); ++i) {
    index.emplace(header[i], i);
  }
  return index;
}

std::string csv_field(const std::vector<std::string>& row,
                      const std::map<std::string, std::size_t>& header,
                      std::string_view key) {
  const auto it = header.find(std::string{key});
  if (it == header.end() || it->second >= row.size()) {
    return {};
  }
  return row[it->second];
}

double parse_csv_number(const std::vector<std::string>& row,
                        const std::map<std::string, std::size_t>& header,
                        std::string_view key) {
  const std::string value = csv_field(row, header, key);
  if (value.empty()) {
    throw std::runtime_error("Geonames CSV missing coordinate field '" + std::string{key} + "'");
  }
  return std::stod(value);
}

std::string render_labels_package_json(const SourceManifest& source_manifest,
                                       const LabelOutput& labels) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(kVectorSchemaVersion) << ",\n";
  output << "  \"category\": \"labels\",\n";
  output << "  \"coordinateFrame\": \"project-local-ENU\",\n";
  output << "  \"sourceLineage\": "
         << render_source_lineage(source_manifest, {labels.source_manifest_id}, "  ") << ",\n";
  output << "  \"labels\": [\n";
  for (std::size_t i = 0; i < labels.labels.size(); ++i) {
    const LabelFeature& label = labels.labels[i];
    output << "    {\n";
    output << "      \"id\": " << json_quote(label.id) << ",\n";
    output << "      \"name\": " << json_quote(label.name) << ",\n";
    output << "      \"featureClass\": " << json_quote(label.feature_class) << ",\n";
    output << "      \"position\": " << render_point(label.position) << "\n";
    output << "    }";
    if (i + 1U != labels.labels.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ]\n";
  output << "}\n";
  return output.str();
}

LabelOutput process_geonames_labels(const PilotRegionPackageOptions& options,
                                    const PilotPackageConfig& config,
                                    const SourceManifest& source_manifest) {
  const std::string csv = read_text_file(options.source_root / config.geonames.path);
  std::istringstream input{csv};
  std::string line;
  if (!std::getline(input, line)) {
    throw std::runtime_error("Geonames CSV must include a header row");
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  const std::map<std::string, std::size_t> header = csv_header_index(parse_csv_row(line));

  LabelOutput output;
  output.source_manifest_id = config.geonames.source_manifest_id;
  std::size_t generated_id = 0U;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    const std::vector<std::string> row = parse_csv_row(line);
    LabelFeature label;
    label.id = csv_field(row, header, "id");
    if (label.id.empty()) {
      label.id = "geonames-" + std::to_string(generated_id++);
    }
    label.name = csv_field(row, header, "name");
    label.feature_class = csv_field(row, header, "featureClass");
    label.position = config.transform.apply(parse_csv_number(row, header, config.geonames.x_field),
                                            parse_csv_number(row, header, config.geonames.y_field));
    label.source_manifest_id = config.geonames.source_manifest_id;
    if (config.pilot_region.contains(label.position)) {
      output.labels.push_back(std::move(label));
    }
  }

  const std::filesystem::path path = options.output_directory / "vectors" / "labels.json";
  write_text_file(path, render_labels_package_json(source_manifest, output));
  output.file = metadata_for_written_file(options.output_directory, path, "labels");
  return output;
}

bool point_in_ring(Point2 point, const std::vector<Point2>& ring) {
  if (ring.size() < 3U) {
    return false;
  }
  bool inside = false;
  for (std::size_t i = 0U, j = ring.size() - 1U; i < ring.size(); j = i++) {
    const Point2 pi = ring[i];
    const Point2 pj = ring[j];
    const bool crosses = ((pi.north_m > point.north_m) != (pj.north_m > point.north_m)) &&
                         (point.east_m <
                          ((pj.east_m - pi.east_m) * (point.north_m - pi.north_m) /
                           (pj.north_m - pi.north_m)) +
                            pi.east_m);
    if (crosses) {
      inside = !inside;
    }
  }
  return inside;
}

bool point_in_feature_polygon(Point2 point, const VectorFeature& feature) {
  if (feature.geometry.type != "Polygon" && feature.geometry.type != "MultiPolygon") {
    return false;
  }
  return std::any_of(feature.geometry.parts.begin(),
                     feature.geometry.parts.end(),
                     [point](const std::vector<Point2>& ring) {
                       return point_in_ring(point, ring);
                     });
}

double squared_distance(Point2 lhs, Point2 rhs) noexcept {
  const double de = lhs.east_m - rhs.east_m;
  const double dn = lhs.north_m - rhs.north_m;
  return (de * de) + (dn * dn);
}

double distance_to_segment(Point2 point, Point2 a, Point2 b) {
  const double segment_length_sq = squared_distance(a, b);
  if (segment_length_sq <= 0.0) {
    return std::sqrt(squared_distance(point, a));
  }
  const double t = std::clamp(((point.east_m - a.east_m) * (b.east_m - a.east_m) +
                               (point.north_m - a.north_m) * (b.north_m - a.north_m)) /
                                segment_length_sq,
                              0.0,
                              1.0);
  const Point2 projected{
    a.east_m + (t * (b.east_m - a.east_m)),
    a.north_m + (t * (b.north_m - a.north_m)),
  };
  return std::sqrt(squared_distance(point, projected));
}

bool point_near_feature_line(Point2 point, const VectorFeature& feature, double tolerance_m) {
  if (feature.geometry.type != "LineString" && feature.geometry.type != "MultiLineString") {
    return false;
  }
  for (const std::vector<Point2>& part : feature.geometry.parts) {
    if (part.size() < 2U) {
      continue;
    }
    for (std::size_t i = 1U; i < part.size(); ++i) {
      if (distance_to_segment(point, part[i - 1U], part[i]) <= tolerance_m) {
        return true;
      }
    }
  }
  return false;
}

bool point_in_water_feature(Point2 point, const VectorFeature& feature, double cell_size_m) {
  const double line_tolerance_m = std::max(cell_size_m * 0.5, feature.line_width_m * 0.5);
  return point_in_feature_polygon(point, feature) ||
         point_near_feature_line(point, feature, line_tolerance_m);
}

const VectorLayerOutput* find_layer(const std::vector<VectorLayerOutput>& layers,
                                    std::string_view category) {
  const auto it = std::find_if(layers.begin(), layers.end(), [category](const auto& layer) {
    return layer.category == category;
  });
  if (it == layers.end()) {
    return nullptr;
  }
  return &*it;
}

struct MaskOutputs {
  FileMetadata water_mask;
  FileMetadata material_mask;
  int rows = 0;
  int cols = 0;
};

struct NavigationMapOutput {
  FileMetadata tile_archive;
  FileMetadata style_manifest;
};

std::set<std::string> collect_used_source_ids(const PilotPackageConfig& config);

MaskOutputs process_masks(const PilotRegionPackageOptions& options,
                          const PilotPackageConfig& config,
                          const std::vector<VectorLayerOutput>& vector_layers) {
  const int cols = static_cast<int>(
    std::ceil(config.pilot_region.width_m / config.masks.cell_size_m));
  const int rows = static_cast<int>(
    std::ceil(config.pilot_region.height_m / config.masks.cell_size_m));
  if (cols <= 0 || rows <= 0) {
    throw std::runtime_error("mask grid must contain at least one cell");
  }

  const VectorLayerOutput* water_layer = find_layer(vector_layers, "water");
  std::ostringstream water;
  std::ostringstream material;
  water << "row,col,center_east_m,center_north_m,is_water\n";
  material << "row,col,center_east_m,center_north_m,material\n";
  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      const Point2 center{
        config.pilot_region.min_east_m +
          ((static_cast<double>(col) + 0.5) * config.masks.cell_size_m),
        config.pilot_region.min_north_m +
          ((static_cast<double>(row) + 0.5) * config.masks.cell_size_m),
      };

      bool is_water = false;
      if (water_layer != nullptr) {
        is_water = std::any_of(water_layer->features.begin(),
                               water_layer->features.end(),
                               [center, &config](const VectorFeature& feature) {
                                 return point_in_water_feature(
                                   center, feature, config.masks.cell_size_m);
                               });
      }

      std::string material_value = config.masks.default_material;
      for (const MaterialLayerConfig& material_layer : config.masks.material_layers) {
        const VectorLayerOutput* layer = find_layer(vector_layers, material_layer.category);
        if (layer == nullptr) {
          continue;
        }
        for (const VectorFeature& feature : layer->features) {
          const double tolerance = std::max(config.masks.cell_size_m * 0.5,
                                            feature.line_width_m * 0.5);
          if (point_in_feature_polygon(center, feature) ||
              point_near_feature_line(center, feature, tolerance)) {
            material_value = material_layer.material;
          }
        }
      }
      if (is_water) {
        material_value = "water";
      }

      water << row << "," << col << "," << render_number(center.east_m) << ","
            << render_number(center.north_m) << "," << (is_water ? "1" : "0") << "\n";
      material << row << "," << col << "," << render_number(center.east_m) << ","
               << render_number(center.north_m) << "," << material_value << "\n";
    }
  }

  const std::filesystem::path water_path = options.output_directory / "masks" / "water-mask.csv";
  const std::filesystem::path material_path =
    options.output_directory / "masks" / "material-mask.csv";
  write_text_file(water_path, water.str());
  write_text_file(material_path, material.str());

  MaskOutputs output;
  output.rows = rows;
  output.cols = cols;
  output.water_mask =
    metadata_for_written_file(options.output_directory, water_path, "water-mask");
  output.material_mask =
    metadata_for_written_file(options.output_directory, material_path, "material-mask");
  return output;
}

std::string render_file_metadata(const FileMetadata& metadata) {
  std::ostringstream output;
  output << "{\"id\":" << json_quote(metadata.id) << ",\"path\":"
         << json_quote(metadata.path) << ",\"checksum\":{\"algorithm\":\"sha256\",\"value\":"
         << json_quote(metadata.checksum) << "},\"sizeBytes\":" << metadata.size_bytes
         << "}";
  return output.str();
}

std::string render_bounds(const Bounds& bounds) {
  std::ostringstream output;
  output << "{\"minEastM\":" << render_number(bounds.min_east_m)
         << ",\"maxEastM\":" << render_number(bounds.max_east_m)
         << ",\"minNorthM\":" << render_number(bounds.min_north_m)
         << ",\"maxNorthM\":" << render_number(bounds.max_north_m) << "}";
  return output.str();
}

std::string render_imagery_metadata(const ImageryPackageMetadata& imagery,
                                    std::string_view indent) {
  std::ostringstream output;
  output << "{\n";
  output << indent << "  \"encoding\": \"ppm-p3\",\n";
  output << indent << "  \"tileSizePx\": " << imagery.tile_size_px << ",\n";
  output << indent << "  \"mipmapped\": " << (imagery.mipmapped ? "true" : "false") << ",\n";
  output << indent << "  \"lods\": [\n";
  for (std::size_t i = 0; i < imagery.lods.size(); ++i) {
    const ImageryLodMetadata& lod = imagery.lods[i];
    output << indent << "    {\n";
    output << indent << "      \"level\": " << lod.level << ",\n";
    output << indent << "      \"sourcePixelScale\": " << lod.source_pixel_scale << ",\n";
    output << indent << "      \"tiles\": [\n";
    for (std::size_t j = 0; j < lod.tiles.size(); ++j) {
      const ImageryTileMetadata& tile = lod.tiles[j];
      output << indent << "        {\n";
      output << indent << "          \"tileId\": " << json_quote(tile.file.id) << ",\n";
      output << indent << "          \"row\": " << tile.row << ",\n";
      output << indent << "          \"col\": " << tile.col << ",\n";
      output << indent << "          \"widthPx\": " << tile.width_px << ",\n";
      output << indent << "          \"heightPx\": " << tile.height_px << ",\n";
      output << indent << "          \"sourceId\": " << json_quote(tile.source_manifest_id)
             << ",\n";
      output << indent << "          \"bounds\": " << render_bounds(tile.bounds) << ",\n";
      output << indent << "          \"file\": " << render_file_metadata(tile.file) << "\n";
      output << indent << "        }";
      if (j + 1U != lod.tiles.size()) {
        output << ",";
      }
      output << "\n";
    }
    output << indent << "      ]\n";
    output << indent << "    }";
    if (i + 1U != imagery.lods.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "  ]\n";
  output << indent << "}";
  return output.str();
}

std::string render_vector_package_metadata(const std::vector<VectorLayerOutput>& layers,
                                           const LabelOutput& labels,
                                           std::string_view indent) {
  std::ostringstream output;
  output << "{\n";
  output << indent << "  \"coordinateFrame\": \"project-local-ENU\",\n";
  output << indent << "  \"layers\": [\n";
  for (std::size_t i = 0; i < layers.size(); ++i) {
    const VectorLayerOutput& layer = layers[i];
    output << indent << "    {\"category\":" << json_quote(layer.category)
           << ",\"featureCount\":" << layer.features.size()
           << ",\"sourceId\":" << json_quote(layer.source_manifest_id)
           << ",\"file\":" << render_file_metadata(layer.file) << "}";
    if (i + 1U != layers.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << indent << "  ],\n";
  output << indent << "  \"labels\": {\"labelCount\":" << labels.labels.size()
         << ",\"sourceId\":" << json_quote(labels.source_manifest_id)
         << ",\"file\":" << render_file_metadata(labels.file) << "}\n";
  output << indent << "}";
  return output.str();
}

std::string render_masks_metadata(const MaskOutputs& masks, std::string_view indent) {
  std::ostringstream output;
  output << "{\n";
  output << indent << "  \"grid\": {\"rows\":" << masks.rows << ",\"cols\":" << masks.cols
         << "},\n";
  output << indent << "  \"waterMask\": " << render_file_metadata(masks.water_mask) << ",\n";
  output << indent << "  \"materialMask\": " << render_file_metadata(masks.material_mask) << "\n";
  output << indent << "}";
  return output.str();
}

void append_uint64_le(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

void append_uint32_le(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

void append_varint(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  while (value >= 0x80U) {
    bytes.push_back(static_cast<std::uint8_t>((value & 0x7fU) | 0x80U));
    value >>= 7U;
  }
  bytes.push_back(static_cast<std::uint8_t>(value));
}

bool is_aviation_navigation_category(std::string_view category) {
  return category == "airport" || category == "runway" ||
         category == "significantObstacle" || category == "obstacle" ||
         category == "airspace";
}

void render_navigation_feature_array(std::ostringstream& output,
                                     const std::vector<VectorFeature>& features) {
  output << "[";
  bool first_feature = true;
  for (const VectorFeature& feature : features) {
    if (!first_feature) {
      output << ",";
    }
    first_feature = false;
    output << "{\"id\":" << json_quote(feature.id)
           << ",\"name\":" << json_quote(feature.name)
           << ",\"category\":" << json_quote(feature.category)
           << ",\"geometry\":" << render_geometry(feature.geometry, "") << "}";
  }
  output << "]";
}

std::string render_navigation_map_tile_payload(
  const std::vector<VectorLayerOutput>& vector_layers,
  const LabelOutput& labels) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": \"flying.navigation-vector-tile.v1\",\n";
  output << "  \"coordinateFrame\": \"project-local-ENU\",\n";
  output << "  \"layers\": {\n";

  const auto render_layer = [&output](std::string_view layer_id,
                                      const std::vector<VectorFeature>& features,
                                      bool trailing_comma) {
    output << "    " << json_quote(layer_id) << ": {\"features\": ";
    render_navigation_feature_array(output, features);
    output << "}";
    if (trailing_comma) {
      output << ",";
    }
    output << "\n";
  };

  std::vector<VectorFeature> base_features;
  std::vector<VectorFeature> airport_features;
  std::vector<VectorFeature> runway_features;
  std::vector<VectorFeature> obstacle_features;
  std::vector<VectorFeature> airspace_features;
  for (const VectorLayerOutput& layer : vector_layers) {
    if (!is_aviation_navigation_category(layer.category)) {
      base_features.insert(base_features.end(), layer.features.begin(), layer.features.end());
    } else if (layer.category == "airport") {
      airport_features.insert(
        airport_features.end(), layer.features.begin(), layer.features.end());
    } else if (layer.category == "runway") {
      runway_features.insert(runway_features.end(), layer.features.begin(), layer.features.end());
    } else if (layer.category == "significantObstacle" || layer.category == "obstacle") {
      obstacle_features.insert(
        obstacle_features.end(), layer.features.begin(), layer.features.end());
    } else if (layer.category == "airspace") {
      airspace_features.insert(
        airspace_features.end(), layer.features.begin(), layer.features.end());
    }
  }

  render_layer("zabaged-base", base_features, true);
  output << "    \"geonames-labels\": {\"labels\": [";
  for (std::size_t i = 0; i < labels.labels.size(); ++i) {
    const LabelFeature& label = labels.labels[i];
    output << "{\"id\":" << json_quote(label.id)
           << ",\"name\":" << json_quote(label.name)
           << ",\"featureClass\":" << json_quote(label.feature_class)
           << ",\"position\":" << render_point(label.position) << "}";
    if (i + 1U != labels.labels.size()) {
      output << ",";
    }
  }
  output << "]},\n";
  render_layer("airports", airport_features, true);
  render_layer("runways", runway_features, true);
  render_layer("obstacles", obstacle_features, true);
  render_layer("airspaces", airspace_features, false);
  output << "  }\n";
  output << "}\n";
  return output.str();
}

std::string render_navigation_map_tile_metadata(
  const SourceManifest& source_manifest,
  const PilotPackageConfig& config,
  const std::vector<VectorLayerOutput>& vector_layers,
  const LabelOutput& labels,
  std::string_view package_version) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": \"flying.navigation-vector-tiles.v1\",\n";
  output << "  \"format\": \"pmtiles\",\n";
  output << "  \"tilePayloadFormat\": \"flying.navigation-vector-tile+json\",\n";
  output << "  \"packageVersion\": " << json_quote(package_version) << ",\n";
  output << "  \"coordinateFrame\": \"project-local-ENU\",\n";
  output << "  \"tileAddressing\": \"project-local-ENU-zxy\",\n";
  output << "  \"coverageScope\": " << json_quote(config.coverage_scope) << ",\n";
  output << "  \"runtimeNetworkRequired\": false,\n";
  output << "  \"externalMapApis\": [],\n";
  output << "  \"remoteTileServerUrls\": [],\n";
  output << "  \"sourceLineage\": "
         << render_source_lineage(source_manifest, collect_used_source_ids(config), "  ")
         << ",\n";
  output << "  \"layers\": [\n";
  output << "    {\"id\":\"zabaged-base\",\"source\":\"vectorPackages\","
            "\"categories\":[";
  for (std::size_t i = 0; i < vector_layers.size(); ++i) {
    if (i != 0U) {
      output << ",";
    }
    output << json_quote(vector_layers[i].category);
  }
  output << "]},\n";
  output << "    {\"id\":\"geonames-labels\",\"source\":\"vectorPackages\","
            "\"categories\":[\"labels\"],\"labelCount\":"
         << labels.labels.size() << "},\n";
  output << "    {\"id\":\"airports\",\"source\":\"airport-database\","
            "\"categories\":[\"airport\"]},\n";
  output << "    {\"id\":\"runways\",\"source\":\"runway-surfaces\","
            "\"categories\":[\"runway\"]},\n";
  output << "    {\"id\":\"obstacles\",\"source\":\"detailed-airport-set\","
            "\"categories\":[\"significantObstacle\"]},\n";
  output << "    {\"id\":\"airspaces\",\"source\":\"permitted-airspace\","
            "\"categories\":[\"airspace\"]}\n";
  output << "  ]\n";
  output << "}\n";
  return output.str();
}

std::vector<std::uint8_t> render_navigation_map_tile_archive(
  const SourceManifest& source_manifest,
  const PilotPackageConfig& config,
  const std::vector<VectorLayerOutput>& vector_layers,
  const LabelOutput& labels,
  std::string_view package_version) {
  const std::string metadata = render_navigation_map_tile_metadata(
    source_manifest, config, vector_layers, labels, package_version);
  const std::string tile_payload = render_navigation_map_tile_payload(vector_layers, labels);

  std::vector<std::uint8_t> root_directory;
  append_varint(root_directory, 1U);                     // entry count
  append_varint(root_directory, 0U);                     // tile id delta
  append_varint(root_directory, 1U);                     // run length
  append_varint(root_directory, tile_payload.size());    // tile data length
  append_varint(root_directory, 0U);                     // offset from tile data section

  std::vector<std::uint8_t> archive;
  archive.reserve(127U + root_directory.size() + metadata.size() + tile_payload.size());
  const std::array<std::uint8_t, 8U> magic = {
    'P', 'M', 'T', 'i', 'l', 'e', 's', static_cast<std::uint8_t>(3)};
  archive.insert(archive.end(), magic.begin(), magic.end());

  constexpr std::uint64_t kHeaderLength = 127U;
  constexpr std::uint64_t kNoSection = 0U;
  const std::uint64_t root_directory_offset = kHeaderLength;
  const std::uint64_t metadata_offset = root_directory_offset + root_directory.size();
  const std::uint64_t tile_data_offset = metadata_offset + metadata.size();
  append_uint64_le(archive, root_directory_offset);      // root directory offset
  append_uint64_le(archive, root_directory.size());      // root directory length
  append_uint64_le(archive, metadata_offset);            // JSON metadata offset
  append_uint64_le(archive, metadata.size());            // JSON metadata length
  append_uint64_le(archive, kNoSection);                 // leaf directories offset
  append_uint64_le(archive, kNoSection);                 // leaf directories length
  append_uint64_le(archive, tile_data_offset);           // tile data offset
  append_uint64_le(archive, tile_payload.size());        // tile data length
  append_uint64_le(archive, 1U);                         // addressed tile count
  append_uint64_le(archive, 1U);                         // tile entry count
  append_uint64_le(archive, 1U);                         // tile content count
  archive.push_back(1U);                                 // clustered
  archive.push_back(0U);                                 // internal compression: none
  archive.push_back(0U);                                 // tile compression: none
  archive.push_back(0U);                                 // tile type: custom JSON vector payload
  archive.push_back(0U);                                 // min zoom
  archive.push_back(14U);                                // max zoom
  for (int i = 0; i < 6; ++i) {
    append_uint32_le(archive, 0U);                       // bounds and center placeholders
  }
  while (archive.size() < kHeaderLength) {
    archive.push_back(0U);
  }
  archive.insert(archive.end(), root_directory.begin(), root_directory.end());
  archive.insert(archive.end(), metadata.begin(), metadata.end());
  archive.insert(archive.end(), tile_payload.begin(), tile_payload.end());
  return archive;
}

std::string render_navigation_map_style_manifest(const FileMetadata& tile_archive) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": \"flying.offline-navigation-map.v1\",\n";
  output << "  \"runtimeDependencies\": {\n";
  output << "    \"runtimeNetworkRequired\": false,\n";
  output << "    \"externalMapApis\": [],\n";
  output << "    \"remoteTileServerUrls\": []\n";
  output << "  },\n";
  output << "  \"tilePackages\": [\n";
  const std::array<std::string_view, 6U> layer_ids = {
    "zabaged-base",
    "geonames-labels",
    "airports",
    "runways",
    "obstacles",
    "airspaces",
  };
  const std::array<std::string_view, 6U> attributions = {
    "Contains CUZK ZABAGED source data.",
    "Contains Geonames source data.",
    "Contains project airport database source data.",
    "Contains project runway surface source data.",
    "Contains project-derived obstacle source data.",
    "Contains permitted airspace source data.",
  };
  for (std::size_t i = 0; i < layer_ids.size(); ++i) {
    output << "    {\"layerId\":" << json_quote(layer_ids[i])
           << ",\"format\":\"pmtiles\",\"path\":" << json_quote(tile_archive.path)
           << ",\"file\":" << render_file_metadata(tile_archive)
           << ",\"attribution\":" << json_quote(attributions[i]) << "}";
    if (i + 1U != layer_ids.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"attribution\": {\n";
  output << "    \"visible\": true,\n";
  output << "    \"text\": \"Contains CUZK ZABAGED, Geonames, project airport, runway, obstacle and permitted airspace data.\"\n";
  output << "  },\n";
  output << "  \"style\": \"Config/FlyingOfflineNavigationMapStyle.json\"\n";
  output << "}\n";
  return output.str();
}

NavigationMapOutput process_navigation_map_package(
  const PilotRegionPackageOptions& options,
  const SourceManifest& source_manifest,
  const PilotPackageConfig& config,
  const std::vector<VectorLayerOutput>& vector_layers,
  const LabelOutput& labels) {
  const std::filesystem::path tile_path =
    options.output_directory / "Navigation" / "offline-navigation-map.pmtiles";
  write_binary_file(tile_path,
                    render_navigation_map_tile_archive(
                      source_manifest, config, vector_layers, labels, options.package_version));
  NavigationMapOutput output;
  output.tile_archive =
    metadata_for_written_file(options.output_directory, tile_path, "offline-navigation-map");

  const std::filesystem::path manifest_path =
    options.output_directory / "Navigation" / "navigation-map.json";
  write_text_file(manifest_path, render_navigation_map_style_manifest(output.tile_archive));
  output.style_manifest =
    metadata_for_written_file(options.output_directory, manifest_path, "navigation-map");
  return output;
}

std::set<std::string> collect_used_source_ids(const PilotPackageConfig& config) {
  std::set<std::string> source_ids;
  for (const OrthoSourceConfig& source : config.ortho.sources) {
    source_ids.insert(source.source_manifest_id);
  }
  for (const VectorLayerConfig& layer : config.vector_layers) {
    source_ids.insert(layer.source_manifest_id);
  }
  source_ids.insert(config.geonames.source_manifest_id);
  for (const PackageInputConfig& input : config.package_inputs) {
    source_ids.insert(input.source_manifest_id);
  }
  source_ids.erase("");
  return source_ids;
}

bool has_disallowed_runtime_reference(std::string_view manifest_json) {
  const std::string lowered = lowercase_ascii(manifest_json);
  const std::array<std::string_view, 14U> disallowed = {
    "\"apikey\"",
    "\"api_key\"",
    "\"accesskey\"",
    "\"access_token\"",
    "\"accesstoken\"",
    "\"mapboxtoken\"",
    "\"urltemplate\"",
    "\"templateurl\"",
    "\"wmtsurl\"",
    "\"wmsurl\"",
    "\"tileurl\"",
    "\"tileserverurl\"",
    "\"remotetileserverurl\"",
    "mapbox",
  };
  if (std::any_of(disallowed.begin(), disallowed.end(), [&lowered](std::string_view token) {
        return lowered.find(token) != std::string::npos;
      })) {
    return true;
  }
  return looks_like_remote_tile_reference(lowered);
}

std::optional<std::string> find_disallowed_generated_runtime_reference(
  const PilotRegionPackageOptions& options,
  std::string_view package_manifest_json,
  const std::vector<VectorLayerOutput>& vector_layers,
  const LabelOutput& labels,
  const WorldObjectsOutput& world_objects) {
  if (has_disallowed_runtime_reference(package_manifest_json)) {
    return "pilot-region-package.json";
  }
  for (const VectorLayerOutput& layer : vector_layers) {
    const std::filesystem::path path = options.output_directory / layer.file.path;
    if (has_disallowed_runtime_reference(read_text_file(path))) {
      return layer.file.path;
    }
  }
  const std::filesystem::path labels_path = options.output_directory / labels.file.path;
  if (has_disallowed_runtime_reference(read_text_file(labels_path))) {
    return labels.file.path;
  }
  const std::filesystem::path world_objects_path = options.output_directory / world_objects.file.path;
  if (has_disallowed_runtime_reference(read_text_file(world_objects_path))) {
    return world_objects.file.path;
  }
  return std::nullopt;
}

std::string package_identity_input(const PilotRegionPackageOptions& options,
                                   const SourceManifest& source_manifest,
                                   const PilotPackageConfig& config,
                                   const RegionManifest& region,
                                   const ImageryPackageMetadata& imagery,
                                   const std::vector<VectorLayerOutput>& vector_layers,
                                   const LabelOutput& labels,
                                   const MaskOutputs& masks,
                                   const NavigationMapOutput& navigation_map,
                                   const WorldObjectsOutput& world_objects) {
  std::ostringstream input;
  input << "{\"packageName\":" << json_quote(options.package_name)
        << ",\"packageVersion\":" << json_quote(options.package_version)
        << ",\"sourceManifestVersion\":" << json_quote(source_manifest.manifest_version)
        << ",\"pilotRegion\":" << json_quote(config.pilot_region.id)
        << ",\"regionManifest\":{\"regionId\":" << json_quote(region.region_id)
        << ",\"coverageScope\":" << json_quote(region.coverage_scope)
        << ",\"packageMode\":" << json_quote(region.package_mode)
        << ",\"sourceMarginM\":" << render_number(region.source_margin_m)
        << ",\"dataRoot\":" << json_quote(region.data_root_relative_path.generic_string())
        << ",\"minAppVersion\":" << json_quote(region.min_app_version) << "}";
  input << ",\"packageInputs\":[";
  for (std::size_t i = 0; i < config.package_inputs.size(); ++i) {
    const PackageInputConfig& package_input = config.package_inputs[i];
    if (i != 0U) {
      input << ",";
    }
    input << "{\"role\":" << json_quote(package_input.role)
          << ",\"sourceId\":" << json_quote(package_input.source_manifest_id)
          << ",\"path\":" << json_quote(package_input.path.generic_string());
    if (package_input.bounds.has_value()) {
      input << ",\"bounds\":" << render_bounds(*package_input.bounds);
    }
    input << "}";
  }
  input << "],\"imagery\":[";
  bool first = true;
  for (const ImageryLodMetadata& lod : imagery.lods) {
    for (const ImageryTileMetadata& tile : lod.tiles) {
      if (!first) {
        input << ",";
      }
      first = false;
      input << render_file_metadata(tile.file);
    }
  }
  input << "],\"vectors\":[";
  for (std::size_t i = 0; i < vector_layers.size(); ++i) {
    if (i != 0U) {
      input << ",";
    }
    input << render_file_metadata(vector_layers[i].file);
  }
  input << "],\"labels\":" << render_file_metadata(labels.file)
        << ",\"waterMask\":" << render_file_metadata(masks.water_mask)
        << ",\"materialMask\":" << render_file_metadata(masks.material_mask)
        << ",\"navigationMap\":" << render_file_metadata(navigation_map.tile_archive)
        << ",\"navigationManifest\":"
        << render_file_metadata(navigation_map.style_manifest)
        << ",\"worldObjects\":" << render_file_metadata(world_objects.file) << "}";
  return input.str();
}

std::string make_package_id(const PilotRegionPackageOptions& options,
                            const PilotPackageConfig& config,
                            std::string_view content_hash) {
  if (!config.package_id_hint.empty()) {
    return sanitized_id(config.package_id_hint, "pilot-region-offline-gis");
  }
  return sanitized_id(options.package_name, "pilot-region-offline-gis") + "-" +
         std::string{content_hash.substr(0U, 16U)};
}

std::size_t imagery_tile_count(const ImageryPackageMetadata& imagery);

std::size_t imagery_byte_count(const ImageryPackageMetadata& imagery) {
  std::size_t total = 0U;
  for (const ImageryLodMetadata& lod : imagery.lods) {
    for (const ImageryTileMetadata& tile : lod.tiles) {
      total += tile.file.size_bytes;
    }
  }
  return total;
}

std::size_t vector_byte_count(const std::vector<VectorLayerOutput>& vector_layers,
                              const LabelOutput& labels) {
  std::size_t total = labels.file.size_bytes;
  for (const VectorLayerOutput& layer : vector_layers) {
    total += layer.file.size_bytes;
  }
  return total;
}

std::size_t mask_byte_count(const MaskOutputs& masks) {
  return masks.water_mask.size_bytes + masks.material_mask.size_bytes;
}

std::size_t navigation_map_byte_count(const NavigationMapOutput& navigation_map) {
  return navigation_map.tile_archive.size_bytes + navigation_map.style_manifest.size_bytes;
}

std::size_t world_object_byte_count(const WorldObjectsOutput& world_objects) {
  return world_objects.file.size_bytes;
}

std::string render_package_manifest(const PilotRegionPackageOptions& options,
                                    const SourceManifest& source_manifest,
                                    const PilotPackageConfig& config,
                                    const RegionManifest& region,
                                    const ImageryPackageMetadata& imagery,
                                    const std::vector<VectorLayerOutput>& vector_layers,
                                    const LabelOutput& labels,
                                    const MaskOutputs& masks,
                                    const NavigationMapOutput& navigation_map,
                                    const WorldObjectsOutput& world_objects,
                                    std::string_view package_id,
                                    std::string_view content_hash) {
  const std::set<std::string> used_source_ids = collect_used_source_ids(config);
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(kPackageSchemaVersion) << ",\n";
  output << "  \"packageName\": " << json_quote(options.package_name) << ",\n";
  output << "  \"packageVersion\": " << json_quote(options.package_version) << ",\n";
  output << "  \"packageId\": " << json_quote(package_id) << ",\n";
  output << "  \"contentHash\": " << json_quote(content_hash) << ",\n";
  output << "  \"sourceManifestVersion\": "
         << json_quote(source_manifest.manifest_version) << ",\n";
  output << "  \"regionManifest\": {\n";
  output << "    \"schemaVersion\": \"flying.region-manifest.v1\",\n";
  output << "    \"regionId\": " << json_quote(region.region_id) << ",\n";
  output << "    \"displayName\": " << json_quote(region.display_name) << ",\n";
  output << "    \"coverageScope\": " << json_quote(region.coverage_scope) << ",\n";
  output << "    \"packageMode\": " << json_quote(region.package_mode) << ",\n";
  output << "    \"sourceMarginM\": " << render_number(region.source_margin_m) << ",\n";
  output << "    \"dataRoot\": {\n";
  output << "      \"installVariable\": " << json_quote(region.data_root_variable) << ",\n";
  output << "      \"defaultRelativePath\": "
         << json_quote(region.data_root_relative_path.generic_string()) << "\n";
  output << "    },\n";
  output << "    \"runtimeCompatibility\": {\n";
  output << "      \"minAppVersion\": " << json_quote(region.min_app_version) << ",\n";
  output << "      \"runtimeNetworkRequired\": false\n";
  output << "    }\n";
  output << "  },\n";
  output << "  \"coverage\": {\n";
  output << "    \"scope\": " << json_quote(config.coverage_scope) << ",\n";
  output << "    \"countryCode\": \"CZ\",\n";
  output << "    \"completeWithinDeclaredBounds\": "
         << (config.coverage_scope == "czech-republic" ? "true" : "false") << "\n";
  output << "  },\n";
  output << "  \"sourceLineage\": "
         << render_source_lineage(source_manifest, used_source_ids, "  ") << ",\n";
  output << "  \"packageInputs\": [\n";
  for (std::size_t i = 0; i < config.package_inputs.size(); ++i) {
    const PackageInputConfig& input = config.package_inputs[i];
    output << "    {\"role\":" << json_quote(input.role)
           << ",\"path\":" << json_quote(input.path.generic_string())
           << ",\"sourceId\":" << json_quote(input.source_manifest_id);
    if (input.bounds.has_value()) {
      output << ",\"bounds\":" << render_bounds(*input.bounds);
    }
    output << "}";
    if (i + 1U != config.package_inputs.size()) {
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
  output << "  \"transform\": " << config.transform_json << ",\n";
  output << "  \"runtimeDependencies\": {\n";
  output << "    \"runtimeNetworkRequired\": false,\n";
  output << "    \"externalMapApis\": [],\n";
  output << "    \"remoteTileServerUrls\": []\n";
  output << "  },\n";
  output << "  \"imagery\": " << render_imagery_metadata(imagery, "  ") << ",\n";
  output << "  \"vectorPackages\": "
         << render_vector_package_metadata(vector_layers, labels, "  ") << ",\n";
  output << "  \"navigationMap\": {\n";
  output << "    \"renderer\": \"FlyingOfflineNavigationMapWidget\",\n";
  output << "    \"style\": \"Config/FlyingOfflineNavigationMapStyle.json\",\n";
  output << "    \"archiveFormat\": \"pmtiles\",\n";
  output << "    \"tileArchive\": " << render_file_metadata(navigation_map.tile_archive)
         << ",\n";
  output << "    \"manifest\": " << render_file_metadata(navigation_map.style_manifest)
         << ",\n";
  output << "    \"requiredLayers\": [\"zabaged-base\", \"geonames-labels\", \"airports\", \"runways\", \"obstacles\", \"airspaces\"],\n";
  output << "    \"overlayLayers\": [\"aircraft-position\", \"flight-path\", \"replay-track\"],\n";
  output << "    \"attributionVisible\": true\n";
  output << "  },\n";
  output << "  \"masks\": " << render_masks_metadata(masks, "  ") << ",\n";
  output << "  \"worldObjects\": {\n";
  output << "    \"renderer\": \"FlyingOfflinePilotTerrainActor\",\n";
  output << "    \"schemaVersion\": \"flying.world-objects.v1\",\n";
  output << "    \"objectCount\": " << world_objects.objects.size() << ",\n";
  output << "    \"file\": " << render_file_metadata(world_objects.file) << ",\n";
  output << "    \"placementPolicy\": {\n";
  output << "      \"approvedVectorDataOnly\": true,\n";
  output << "      \"orthoColorInferenceAllowed\": false,\n";
  output << "      \"heightSource\": \"dmp-derived-estimate-with-vector-property-override\"\n";
  output << "    },\n";
  output << "    \"criticalObjectTypes\": [\"mast\", \"power_line\", \"obstacle\", \"water_surface\", \"windsock\", \"runway_object\"],\n";
  output << "    \"environmentAudioHooks\": [\"water_ambience\", \"vegetation_wind\", \"airport_windsock_wind\"],\n";
  output << "    \"activeZoneCollisionOnly\": true,\n";
  output << "    \"densityScalesPreserveFlightCriticalObjects\": true\n";
  output << "  },\n";
  output << "  \"streaming\": {\n";
  output << "    \"runtimeNetworkRequired\": false,\n";
  output << "    \"externalMapApis\": [],\n";
  output << "    \"remoteTileServerUrls\": [],\n";
  output << "    \"addressing\": \"project-local-ENU-tile-bounds\",\n";
  output << "    \"coverageScope\": " << json_quote(config.coverage_scope) << ",\n";
  output << "    \"interruptionFreeWithinDeclaredBounds\": true\n";
  output << "  },\n";
  output << "  \"packagingLayout\": {\n";
  output << "    \"imageryRoot\": \"imagery\",\n";
  output << "    \"vectorRoot\": \"vectors\",\n";
  output << "    \"maskRoot\": \"masks\",\n";
  output << "    \"orthoTileCount\": " << imagery_tile_count(imagery) << ",\n";
  output << "    \"vectorLayerCount\": " << vector_layers.size() << ",\n";
  output << "    \"waterMaskAvailable\": true,\n";
  output << "    \"materialMaskAvailable\": true,\n";
  output << "    \"imageryBytes\": " << imagery_byte_count(imagery) << ",\n";
  output << "    \"vectorBytes\": " << vector_byte_count(vector_layers, labels) << ",\n";
  output << "    \"navigationMapBytes\": "
         << navigation_map_byte_count(navigation_map) << ",\n";
  output << "    \"worldObjectBytes\": " << world_object_byte_count(world_objects) << ",\n";
  output << "    \"maskBytes\": " << mask_byte_count(masks) << ",\n";
  output << "    \"totalBytes\": "
         << (imagery_byte_count(imagery) + vector_byte_count(vector_layers, labels) +
             navigation_map_byte_count(navigation_map) + world_object_byte_count(world_objects) +
             mask_byte_count(masks)) << "\n";
  output << "  },\n";
  output << "  \"validation\": {\n";
  output << "    \"offlineRuntime\": {\n";
  output << "      \"manifestsChecked\": " << (vector_layers.size() + 2U) << ",\n";
  output << "      \"externalMapApiKeys\": 0,\n";
  output << "      \"remoteTileServerUrls\": 0,\n";
  output << "      \"passed\": true\n";
  output << "    }\n";
  output << "  },\n";
  output << "  \"determinism\": {\n";
  output << "    \"algorithm\": \"sha256\",\n";
  output << "    \"identityInputHash\": " << json_quote(content_hash) << "\n";
  output << "  }\n";
  output << "}\n";
  return output.str();
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

std::string render_validation_report(const ValidationReport& report,
                                     std::size_t ortho_tile_count,
                                     std::size_t vector_layer_count,
                                     std::size_t label_count,
                                     const MaskOutputs& masks) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(report.schema_version) << ",\n";
  output << "  \"passed\": " << (report.passed ? "true" : "false") << ",\n";
  output << "  \"sourceManifestPath\": " << json_quote(report.source_manifest_path) << ",\n";
  output << "  \"packageManifestPath\": " << json_quote(report.package_manifest_path) << ",\n";
  output << "  \"packageId\": " << json_quote(report.package_id) << ",\n";
  output << "  \"issues\": " << render_issues(report, "  ") << ",\n";
  output << "  \"sources\": " << render_sources(report, "  ") << ",\n";
  output << "  \"pilotRegionPackageValidation\": {\n";
  output << "    \"orthoTileCount\": " << ortho_tile_count << ",\n";
  output << "    \"vectorLayerCount\": " << vector_layer_count << ",\n";
  output << "    \"labelCount\": " << label_count << ",\n";
  output << "    \"maskRows\": " << masks.rows << ",\n";
  output << "    \"maskCols\": " << masks.cols << ",\n";
  output << "    \"offlineRuntime\": {\n";
  output << "      \"externalMapApiKeys\": 0,\n";
  output << "      \"remoteTileServerUrls\": 0,\n";
  output << "      \"passed\": " << (report.passed ? "true" : "false") << "\n";
  output << "    }\n";
  output << "  }\n";
  output << "}\n";
  return output.str();
}

void write_report_if_requested(const PilotRegionPackageOptions& options,
                               const ValidationReport& report,
                               std::size_t ortho_tile_count = 0U,
                               std::size_t vector_layer_count = 0U,
                               std::size_t label_count = 0U,
                               const MaskOutputs& masks = {}) {
  if (!options.report_path.empty()) {
    write_text_file(options.report_path,
                    render_validation_report(
                      report, ortho_tile_count, vector_layer_count, label_count, masks));
  }
}

std::size_t imagery_tile_count(const ImageryPackageMetadata& imagery) {
  std::size_t count = 0U;
  for (const ImageryLodMetadata& lod : imagery.lods) {
    count += lod.tiles.size();
  }
  return count;
}

void validate_czech_republic_package_coverage(const PilotPackageConfig& config,
                                              ValidationReport& report) {
  if (config.coverage_scope != "czech-republic") {
    return;
  }

  const Bounds required_bounds{
    config.pilot_region.min_east_m,
    config.pilot_region.max_east_m(),
    config.pilot_region.min_north_m,
    config.pilot_region.max_north_m(),
  };

  std::vector<Bounds> ortho_bounds;
  ortho_bounds.reserve(config.ortho.sources.size());
  for (const OrthoSourceConfig& source : config.ortho.sources) {
    ortho_bounds.push_back(source.bounds);
  }
  if (!continuous_rect_coverage(ortho_bounds, required_bounds)) {
    add_issue(report,
              "error",
              "pilot.config.czechRepublic.orthoImagery.coverage_incomplete",
              "Full Czech Republic package processing requires continuous ortho imagery source coverage across the declared package bounds.");
  }

  std::map<std::string, std::vector<Bounds>> vector_bounds_by_category;
  for (const VectorLayerConfig& layer : config.vector_layers) {
    if (!layer.bounds.has_value()) {
      add_issue(report,
                "error",
                "pilot.config.czechRepublic.vectorLayers.bounds_missing",
                "Full Czech Republic package processing requires each vector layer to declare source coverage bounds.",
                layer.category);
      continue;
    }
    vector_bounds_by_category[layer.category].push_back(*layer.bounds);
  }

  for (const auto& [category, layer_bounds] : vector_bounds_by_category) {
    if (!continuous_rect_coverage(layer_bounds, required_bounds)) {
      add_issue(report,
                "error",
                "pilot.config.czechRepublic.vectorLayers.coverage_incomplete",
                "Full Czech Republic package processing requires every declared vector layer category to cover the declared package bounds.",
                category);
    }
  }

  const std::set<std::string> material_categories = [&config] {
    std::set<std::string> categories;
    for (const MaterialLayerConfig& material_layer : config.masks.material_layers) {
      categories.insert(material_layer.category);
    }
    return categories;
  }();

  const auto water_bounds = vector_bounds_by_category.find("water");
  if (water_bounds == vector_bounds_by_category.end() ||
      !continuous_rect_coverage(water_bounds->second, required_bounds)) {
    add_issue(report,
              "error",
              "pilot.config.czechRepublic.waterMask.coverage_incomplete",
              "Full Czech Republic package processing requires water-mask source coverage across the declared package bounds.");
  }

  for (const std::string& category : material_categories) {
    const auto layer_bounds = vector_bounds_by_category.find(category);
    if (layer_bounds == vector_bounds_by_category.end() ||
        !continuous_rect_coverage(layer_bounds->second, required_bounds)) {
      add_issue(report,
                "error",
                "pilot.config.czechRepublic.materialMasks.coverage_incomplete",
                "Full Czech Republic package processing requires material-mask source layer coverage across the declared package bounds.",
                category);
    }
  }
}

} // namespace

PilotRegionPackageResult process_pilot_region_packages(
  const PilotRegionPackageOptions& options) {
  PilotRegionPackageResult result;
  result.report.schema_version = std::string{kValidationReportSchemaVersion};
  result.report.source_manifest_path = options.source_manifest_path.generic_string();
  result.report.package_manifest_path =
    (options.output_directory / "pilot-region-package.json").generic_string();

  try {
    if (options.source_manifest_path.empty()) {
      add_issue(result.report,
                "error",
                "pilot.options.source_manifest_path.missing",
                "Pilot region package processing requires a source manifest path.");
    }
    if (options.region_manifest_path.empty()) {
      add_issue(result.report,
                "error",
                "pilot.options.region_manifest_path.missing",
                "Region-aware package processing requires a region manifest path.");
    }
    if (options.package_config_path.empty()) {
      add_issue(result.report,
                "error",
                "pilot.options.package_config_path.missing",
                "Pilot region package processing requires a package config path.");
    }
    if (options.output_directory.empty()) {
      add_issue(result.report,
                "error",
                "pilot.options.output_directory.missing",
                "Pilot region package processing requires an output directory.");
    }
    if (options.package_version.empty()) {
      add_issue(result.report,
                "error",
                "pilot.options.package_version.missing",
                "Pilot region package processing requires a package version.");
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
      (options.output_directory / "pilot-region-package.json").generic_string();

    if (!source_validation.accepted()) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report);
      return result;
    }

    const JsonValue config_root = JsonParser{read_text_file(options.package_config_path)}.parse();
    std::optional<PilotPackageConfig> config =
      parse_package_config(config_root, result.report);
    const JsonValue region_root = JsonParser{read_text_file(options.region_manifest_path)}.parse();
    std::optional<RegionManifest> region =
      parse_region_manifest(region_root, result.report);
    if (!config.has_value() || !region.has_value()) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report);
      return result;
    }
    if (options.require_czech_republic_scope &&
        (config->schema_version != kCzechConfigSchemaVersion ||
         config->coverage_scope != "czech-republic")) {
      add_issue(result.report,
                "error",
                "pilot.options.czech_republic_config.required",
                "The Czech Republic package command requires the Czech Republic schema and coverageScope 'czech-republic'.");
    }
    validate_czech_republic_package_coverage(*config, result.report);
    validate_source_scope(*config, *region, result.report);
    bind_config_sources(*config, *source_validation.manifest, result.report);
    if (has_errors(result.report)) {
      finalize_report(result.report);
      write_report_if_requested(options, result.report);
      return result;
    }

    const ImageryPackageMetadata imagery = process_ortho_imagery(options, *config);

    std::vector<VectorLayerOutput> vector_layers;
    vector_layers.reserve(config->vector_layers.size());
    for (const VectorLayerConfig& layer_config : config->vector_layers) {
      vector_layers.push_back(
        process_vector_layer(options, *config, *source_validation.manifest, layer_config));
    }
    std::sort(vector_layers.begin(), vector_layers.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.category < rhs.category;
    });

    const LabelOutput labels =
      process_geonames_labels(options, *config, *source_validation.manifest);
    const MaskOutputs masks = process_masks(options, *config, vector_layers);
    const NavigationMapOutput navigation_map = process_navigation_map_package(
      options, *source_validation.manifest, *config, vector_layers, labels);
    const WorldObjectsOutput world_objects =
      process_world_objects(options, *source_validation.manifest, *config, vector_layers);

    const std::string identity_input = package_identity_input(
      options,
      *source_validation.manifest,
      *config,
      *region,
      imagery,
      vector_layers,
      labels,
      masks,
      navigation_map,
      world_objects);
    const std::string content_hash = sha256_text(identity_input);
    const std::string package_id = make_package_id(options, *config, content_hash);
    result.report.package_id = package_id;

    const std::string manifest_json = render_package_manifest(options,
                                                              *source_validation.manifest,
                                                              *config,
                                                              *region,
                                                              imagery,
                                                              vector_layers,
                                                              labels,
                                                              masks,
                                                              navigation_map,
                                                              world_objects,
                                                              package_id,
                                                              content_hash);
    if (const std::optional<std::string> unsafe_runtime_package =
          find_disallowed_generated_runtime_reference(
            options, manifest_json, vector_layers, labels, world_objects)) {
      add_issue(result.report,
                "error",
                "pilot.offline_manifest.disallowed_runtime_reference",
                "Generated pilot-region runtime package contains an external map API key or remote tile-server reference.",
                *unsafe_runtime_package);
      finalize_report(result.report);
      write_report_if_requested(options,
                                result.report,
                                imagery_tile_count(imagery),
                                vector_layers.size(),
                                labels.labels.size(),
                                masks);
      return result;
    }

    const std::filesystem::path package_manifest_path =
      options.output_directory / "pilot-region-package.json";
    write_text_file(package_manifest_path, manifest_json);
    result.package_manifest_path = package_manifest_path;

    finalize_report(result.report);
    write_report_if_requested(options,
                              result.report,
                              imagery_tile_count(imagery),
                              vector_layers.size(),
                              labels.labels.size(),
                              masks);
    return result;
  } catch (const std::exception& error) {
    add_issue(result.report, "error", "pilot.processing.failed", error.what());
    finalize_report(result.report);
    write_report_if_requested(options, result.report);
    return result;
  }
}

PilotRegionPackageResult process_czech_republic_packages(
  const CzechRepublicPackageOptions& options) {
  CzechRepublicPackageOptions scoped_options = options;
  scoped_options.require_czech_republic_scope = true;
  return process_pilot_region_packages(scoped_options);
}

} // namespace flying::data_pipeline
