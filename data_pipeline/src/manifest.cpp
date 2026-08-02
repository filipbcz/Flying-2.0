#include "flying/data_pipeline/manifest.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <ios>
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

constexpr std::string_view kSourceManifestSchemaVersion = "flying.source-manifest.v1";
constexpr std::string_view kPackageManifestSchemaVersion = "flying.package-manifest.v1";
constexpr std::string_view kValidationReportSchemaVersion = "flying.validation-report.v1";

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
    if (try_consume('-')) {
      if (index_ >= text_.size()) {
        throw std::runtime_error("incomplete JSON number");
      }
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
      if (index_ + 6U > text_.size() || text_[index_] != '\\' || text_[index_ + 1U] != 'u') {
        throw std::runtime_error("invalid JSON surrogate pair");
      }
      index_ += 2U;
      const unsigned int low = parse_hex_quad();
      if (low < 0xDC00U || low > 0xDFFFU) {
        throw std::runtime_error("invalid JSON surrogate pair");
      }
      code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
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

std::string render_canonical_json(const JsonValue& value) {
  switch (value.type) {
    case JsonValue::Type::null_value:
      return "null";
    case JsonValue::Type::bool_value:
      return value.boolean ? "true" : "false";
    case JsonValue::Type::number_value: {
      std::ostringstream output;
      output << std::setprecision(17) << value.number;
      return output.str();
    }
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
    throw std::runtime_error("failed to open source file " + path.string());
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
    throw std::runtime_error("failed to read source file " + path.string());
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

const JsonValue* find_member(const JsonValue::Object& object, std::string_view key) {
  const auto it = object.find(std::string{key});
  if (it == object.end()) {
    return nullptr;
  }
  return &it->second;
}

std::string source_label(std::size_t index) {
  std::ostringstream label;
  label << "<source[" << index << "]>";
  return label.str();
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

std::string require_canonical_object_with_strings(
  const JsonValue::Object& object,
  std::string_view key,
  std::initializer_list<std::string_view> required_fields,
  ValidationReport& report,
  std::string_view code_prefix,
  std::string_view subject,
  std::string_view source_id) {
  const JsonValue* value = find_member(object, key);
  const JsonValue::Object* child =
    require_object(object, key, report, code_prefix, subject, source_id);
  if (child == nullptr || value == nullptr) {
    return {};
  }
  for (const std::string_view field : required_fields) {
    (void)require_non_empty_string(
      *child, field, report, std::string{code_prefix} + "." + std::string{key}, subject, source_id);
  }
  return render_canonical_json(*value);
}

bool is_stable_identifier(std::string_view id) {
  return std::all_of(id.begin(), id.end(), [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '_' || ch == '.';
  });
}

std::string lowercase_ascii(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const char ch : text) {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return lowered;
}

bool is_sha256_hex(std::string_view text) {
  return text.size() == 64U && std::all_of(text.begin(), text.end(), [](char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
  });
}

std::filesystem::path effective_source_root(const ValidateOptions& options) {
  if (!options.source_root.empty()) {
    return options.source_root;
  }
  const std::filesystem::path parent = options.source_manifest_path.parent_path();
  if (!parent.empty()) {
    return parent;
  }
  return std::filesystem::current_path();
}

void validate_transform_config(const JsonValue::Object& root,
                               SourceManifest& manifest,
                               ValidationReport& report) {
  const JsonValue* transform_value = find_member(root, "transform");
  const JsonValue::Object* transform =
    require_object(root, "transform", report, "manifest", "Source manifest", {});
  if (transform == nullptr || transform_value == nullptr) {
    return;
  }

  const JsonValue::Object* target_crs = require_object(
    *transform, "targetCrs", report, "manifest.transform", "Transform configuration", {});
  if (target_crs != nullptr) {
    (void)require_non_empty_string(
      *target_crs, "authority", report, "manifest.transform.targetCrs", "Transform target CRS", {});
    (void)require_non_empty_string(
      *target_crs, "code", report, "manifest.transform.targetCrs", "Transform target CRS", {});
  }

  const JsonValue* steps = find_member(*transform, "steps");
  if (steps == nullptr) {
    add_issue(report,
              "error",
              "manifest.transform.steps.missing",
              "Transform configuration is missing required field 'steps'.");
  } else if (steps->type != JsonValue::Type::array_value) {
    add_issue(report,
              "error",
              "manifest.transform.steps.invalid_type",
              "Transform configuration field 'steps' must be an array.");
  }

  manifest.transform_config_json = render_canonical_json(*transform_value);
}

ChecksumDeclaration validate_checksum(const JsonValue::Object& source,
                                      ValidationReport& report,
                                      const std::string& source_id) {
  ChecksumDeclaration checksum;
  const JsonValue::Object* checksum_object =
    require_object(source, "checksum", report, "source", "Source dataset", source_id);
  if (checksum_object == nullptr) {
    return checksum;
  }

  checksum.path = require_non_empty_string(
    *checksum_object, "path", report, "source.checksum", "Checksum declaration", source_id);
  checksum.algorithm = require_non_empty_string(
    *checksum_object, "algorithm", report, "source.checksum", "Checksum declaration", source_id);
  checksum.value = require_non_empty_string(
    *checksum_object, "value", report, "source.checksum", "Checksum declaration", source_id);

  if (!checksum.algorithm.empty() && lowercase_ascii(checksum.algorithm) != "sha256") {
    add_issue(report,
              "error",
              "source.checksum.algorithm.unsupported",
              "Source dataset checksum algorithm must be 'sha256'.",
              source_id);
  }
  if (!checksum.value.empty() && !is_sha256_hex(checksum.value)) {
    add_issue(report,
              "error",
              "source.checksum.value.invalid",
              "Source dataset checksum value must be a 64-character SHA-256 hex digest.",
              source_id);
  }
  if (!checksum.path.empty() && std::filesystem::path{checksum.path}.is_absolute()) {
    add_issue(report,
              "error",
              "source.checksum.path.absolute",
              "Source dataset checksum path must be relative to the source root.",
              source_id);
  }

  checksum.algorithm = lowercase_ascii(checksum.algorithm);
  checksum.value = lowercase_ascii(checksum.value);
  return checksum;
}

void verify_source_checksum(const SourceDataset& dataset,
                            const std::filesystem::path& source_root,
                            ValidationReport& report) {
  SourceValidationRecord record;
  record.source_id = dataset.id;
  record.path = dataset.checksum.path;
  record.checksum_algorithm = dataset.checksum.algorithm;
  record.declared_checksum = dataset.checksum.value;

  if (dataset.checksum.path.empty() || dataset.checksum.algorithm.empty() ||
      dataset.checksum.value.empty() || dataset.checksum.algorithm != "sha256" ||
      !is_sha256_hex(dataset.checksum.value) ||
      std::filesystem::path{dataset.checksum.path}.is_absolute()) {
    report.sources.push_back(std::move(record));
    return;
  }

  const std::filesystem::path source_path = source_root / dataset.checksum.path;
  std::error_code error;
  if (!std::filesystem::is_regular_file(source_path, error)) {
    add_issue(report,
              "error",
              "source.checksum.file_missing",
              "Declared source payload does not exist or is not a regular file: " +
                source_path.string(),
              dataset.id);
    report.sources.push_back(std::move(record));
    return;
  }

  try {
    record.computed_checksum = sha256_file(source_path);
  } catch (const std::exception& error_message) {
    add_issue(report,
              "error",
              "source.checksum.read_failed",
              error_message.what(),
              dataset.id);
    report.sources.push_back(std::move(record));
    return;
  }

  record.checksum_verified = record.computed_checksum == dataset.checksum.value;
  if (!record.checksum_verified) {
    add_issue(report,
              "error",
              "source.checksum.mismatch",
              "Declared source payload checksum does not match the computed SHA-256 digest.",
              dataset.id);
  }
  report.sources.push_back(std::move(record));
}

std::optional<SourceDataset> validate_source_dataset(const JsonValue& value,
                                                     std::size_t index,
                                                     ValidationReport& report) {
  const std::string fallback_id = source_label(index);
  if (value.type != JsonValue::Type::object_value) {
    add_issue(report,
              "error",
              "source.invalid_type",
              "Source manifest entries must be objects.",
              fallback_id);
    return std::nullopt;
  }

  const JsonValue::Object& source = value.object;
  SourceDataset dataset;
  dataset.id =
    require_non_empty_string(source, "id", report, "source", "Source dataset", fallback_id);
  const std::string issue_source_id = dataset.id.empty() ? fallback_id : dataset.id;
  dataset.dataset_name = require_non_empty_string(
    source, "datasetName", report, "source", "Source dataset", issue_source_id);
  dataset.version = require_non_empty_string(
    source, "version", report, "source", "Source dataset", issue_source_id);

  if (!dataset.id.empty() && !is_stable_identifier(dataset.id)) {
    add_issue(report,
              "error",
              "source.id.invalid",
              "Source dataset id may contain only letters, numbers, '.', '_' and '-'.",
              issue_source_id);
  }

  dataset.license_json = require_canonical_object_with_strings(
    source, "license", {"name"}, report, "source", "Source dataset", issue_source_id);
  dataset.attribution_json = require_canonical_object_with_strings(
    source, "attribution", {"text"}, report, "source", "Source dataset", issue_source_id);
  dataset.coordinate_reference_system_json = require_canonical_object_with_strings(
    source,
    "coordinateReferenceSystem",
    {"authority", "code"},
    report,
    "source",
    "Source dataset",
    issue_source_id);
  dataset.provenance_json = require_canonical_object_with_strings(
    source,
    "provenance",
    {"publisher", "sourceUrl", "retrievedAtUtc"},
    report,
    "source",
    "Source dataset",
    issue_source_id);

  const JsonValue* permitted_use_value = find_member(source, "permittedUse");
  const JsonValue::Object* permitted_use =
    require_object(source, "permittedUse", report, "source", "Source dataset", issue_source_id);
  if (permitted_use != nullptr && permitted_use_value != nullptr) {
    (void)require_bool(*permitted_use,
                       "terrainDerivatives",
                       report,
                       "source.permittedUse",
                       "Permitted-use metadata",
                       issue_source_id);
    (void)require_bool(*permitted_use,
                       "runtimeRedistribution",
                       report,
                       "source.permittedUse",
                       "Permitted-use metadata",
                       issue_source_id);
    (void)require_bool(*permitted_use,
                       "attributionRequired",
                       report,
                       "source.permittedUse",
                       "Permitted-use metadata",
                       issue_source_id);
    dataset.permitted_use_json = render_canonical_json(*permitted_use_value);
  }

  dataset.checksum = validate_checksum(source, report, issue_source_id);
  return dataset;
}

std::optional<SourceManifest> parse_source_manifest(const JsonValue& root,
                                                    ValidationReport& report) {
  if (root.type != JsonValue::Type::object_value) {
    add_issue(report, "error", "manifest.invalid_type", "Source manifest root must be an object.");
    return std::nullopt;
  }

  SourceManifest manifest;
  const JsonValue::Object& root_object = root.object;
  manifest.schema_version = require_non_empty_string(
    root_object, "schemaVersion", report, "manifest", "Source manifest", {});
  manifest.manifest_version = require_non_empty_string(
    root_object, "manifestVersion", report, "manifest", "Source manifest", {});
  if (!manifest.schema_version.empty() &&
      manifest.schema_version != kSourceManifestSchemaVersion) {
    add_issue(report,
              "error",
              "manifest.schemaVersion.unsupported",
              "Source manifest schemaVersion must be '" +
                std::string{kSourceManifestSchemaVersion} + "'.");
  }

  validate_transform_config(root_object, manifest, report);

  const JsonValue* sources_value = find_member(root_object, "sources");
  if (sources_value == nullptr) {
    add_issue(report,
              "error",
              "manifest.sources.missing",
              "Source manifest is missing required field 'sources'.");
    return std::nullopt;
  }
  if (sources_value->type != JsonValue::Type::array_value) {
    add_issue(report,
              "error",
              "manifest.sources.invalid_type",
              "Source manifest field 'sources' must be an array.");
    return std::nullopt;
  }
  if (sources_value->array.empty()) {
    add_issue(report,
              "error",
              "manifest.sources.empty",
              "Source manifest must declare at least one source dataset.");
  }

  std::set<std::string> source_ids;
  for (std::size_t i = 0; i < sources_value->array.size(); ++i) {
    std::optional<SourceDataset> dataset =
      validate_source_dataset(sources_value->array[i], i, report);
    if (!dataset.has_value()) {
      continue;
    }
    if (!dataset->id.empty() && !source_ids.insert(dataset->id).second) {
      add_issue(report,
                "error",
                "source.id.duplicate",
                "Source dataset ids must be unique.",
                dataset->id);
    }
    manifest.sources.push_back(std::move(*dataset));
  }

  if (has_errors(report)) {
    return std::nullopt;
  }
  return manifest;
}

std::string render_validation_report(const ValidationReport& report) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(report.schema_version) << ",\n";
  output << "  \"passed\": " << (report.passed ? "true" : "false") << ",\n";
  output << "  \"sourceManifestPath\": " << json_quote(report.source_manifest_path) << ",\n";
  output << "  \"packageManifestPath\": " << json_quote(report.package_manifest_path) << ",\n";
  output << "  \"packageId\": " << json_quote(report.package_id) << ",\n";
  output << "  \"issues\": [\n";
  for (std::size_t i = 0; i < report.issues.size(); ++i) {
    const ValidationIssue& issue = report.issues[i];
    output << "    {\n";
    output << "      \"severity\": " << json_quote(issue.severity) << ",\n";
    output << "      \"code\": " << json_quote(issue.code) << ",\n";
    output << "      \"message\": " << json_quote(issue.message) << ",\n";
    output << "      \"sourceId\": " << json_quote(issue.source_id) << "\n";
    output << "    }";
    if (i + 1U != report.issues.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"sources\": [\n";
  for (std::size_t i = 0; i < report.sources.size(); ++i) {
    const SourceValidationRecord& source = report.sources[i];
    output << "    {\n";
    output << "      \"sourceId\": " << json_quote(source.source_id) << ",\n";
    output << "      \"path\": " << json_quote(source.path) << ",\n";
    output << "      \"checksumAlgorithm\": " << json_quote(source.checksum_algorithm) << ",\n";
    output << "      \"declaredChecksum\": " << json_quote(source.declared_checksum) << ",\n";
    output << "      \"computedChecksum\": " << json_quote(source.computed_checksum) << ",\n";
    output << "      \"checksumVerified\": " << (source.checksum_verified ? "true" : "false")
           << "\n";
    output << "    }";
    if (i + 1U != report.sources.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ]\n";
  output << "}\n";
  return output.str();
}

std::string sanitized_package_name(std::string_view name) {
  std::string sanitized;
  bool previous_dash = false;
  for (const char ch : name) {
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
    return "flying-gis-package";
  }
  return sanitized;
}

std::string render_checksum_json(const ChecksumDeclaration& checksum) {
  std::ostringstream output;
  output << "{\"algorithm\":" << json_quote(checksum.algorithm)
         << ",\"path\":" << json_quote(checksum.path)
         << ",\"value\":" << json_quote(checksum.value) << "}";
  return output.str();
}

std::string package_identity_input(const SourceManifest& manifest,
                                   std::string_view package_name,
                                   std::string_view package_version) {
  std::vector<SourceDataset> sorted_sources = manifest.sources;
  std::sort(sorted_sources.begin(), sorted_sources.end(), [](const SourceDataset& lhs,
                                                             const SourceDataset& rhs) {
    return lhs.id < rhs.id;
  });

  std::ostringstream input;
  input << "{\"packageName\":" << json_quote(package_name)
        << ",\"packageVersion\":" << json_quote(package_version)
        << ",\"sourceManifestVersion\":" << json_quote(manifest.manifest_version)
        << ",\"transform\":" << manifest.transform_config_json << ",\"sources\":[";
  for (std::size_t i = 0; i < sorted_sources.size(); ++i) {
    const SourceDataset& source = sorted_sources[i];
    if (i != 0U) {
      input << ",";
    }
    input << "{\"attribution\":" << source.attribution_json
          << ",\"checksum\":" << render_checksum_json(source.checksum)
          << ",\"coordinateReferenceSystem\":" << source.coordinate_reference_system_json
          << ",\"datasetName\":" << json_quote(source.dataset_name)
          << ",\"id\":" << json_quote(source.id) << ",\"license\":" << source.license_json
          << ",\"permittedUse\":" << source.permitted_use_json
          << ",\"provenance\":" << source.provenance_json
          << ",\"version\":" << json_quote(source.version) << "}";
  }
  input << "]}";
  return input.str();
}

PackageManifest build_package_manifest(const SourceManifest& source_manifest,
                                       std::string_view package_name,
                                       std::string_view package_version) {
  const std::string identity_input =
    package_identity_input(source_manifest, package_name, package_version);
  const std::string content_hash = sha256_text(identity_input);
  const std::string package_id =
    sanitized_package_name(package_name) + "-" + content_hash.substr(0U, 16U);

  std::vector<SourceDataset> sorted_sources = source_manifest.sources;
  std::sort(sorted_sources.begin(), sorted_sources.end(), [](const SourceDataset& lhs,
                                                             const SourceDataset& rhs) {
    return lhs.id < rhs.id;
  });

  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": " << json_quote(kPackageManifestSchemaVersion) << ",\n";
  output << "  \"packageName\": " << json_quote(package_name) << ",\n";
  output << "  \"packageVersion\": " << json_quote(package_version) << ",\n";
  output << "  \"packageId\": " << json_quote(package_id) << ",\n";
  output << "  \"contentHash\": " << json_quote(content_hash) << ",\n";
  output << "  \"sourceManifestVersion\": "
         << json_quote(source_manifest.manifest_version) << ",\n";
  output << "  \"transform\": " << source_manifest.transform_config_json << ",\n";
  output << "  \"sourceLineage\": [\n";
  for (std::size_t i = 0; i < sorted_sources.size(); ++i) {
    const SourceDataset& source = sorted_sources[i];
    output << "    {\n";
    output << "      \"sourceId\": " << json_quote(source.id) << ",\n";
    output << "      \"datasetName\": " << json_quote(source.dataset_name) << ",\n";
    output << "      \"version\": " << json_quote(source.version) << ",\n";
    output << "      \"license\": " << source.license_json << ",\n";
    output << "      \"attribution\": " << source.attribution_json << ",\n";
    output << "      \"checksum\": " << render_checksum_json(source.checksum) << ",\n";
    output << "      \"coordinateReferenceSystem\": "
           << source.coordinate_reference_system_json << ",\n";
    output << "      \"permittedUse\": " << source.permitted_use_json << ",\n";
    output << "      \"provenance\": " << source.provenance_json << "\n";
    output << "    }";
    if (i + 1U != sorted_sources.size()) {
      output << ",";
    }
    output << "\n";
  }
  output << "  ],\n";
  output << "  \"determinism\": {\n";
  output << "    \"algorithm\": \"sha256\",\n";
  output << "    \"identityInputHash\": " << json_quote(content_hash) << "\n";
  output << "  }\n";
  output << "}\n";

  PackageManifest manifest;
  manifest.schema_version = std::string{kPackageManifestSchemaVersion};
  manifest.package_name = std::string{package_name};
  manifest.package_version = std::string{package_version};
  manifest.package_id = package_id;
  manifest.content_hash = content_hash;
  manifest.source_manifest_version = source_manifest.manifest_version;
  manifest.json = output.str();
  return manifest;
}

} // namespace

ValidationResult validate_source_manifest(const ValidateOptions& options) {
  ValidationResult result;
  result.report.schema_version = std::string{kValidationReportSchemaVersion};
  result.report.source_manifest_path = options.source_manifest_path.generic_string();

  try {
    const std::string manifest_text = read_text_file(options.source_manifest_path);
    const JsonValue root = JsonParser{manifest_text}.parse();
    result.manifest = parse_source_manifest(root, result.report);
  } catch (const std::exception& error) {
    add_issue(result.report, "error", "manifest.load_failed", error.what());
  }

  if (result.manifest.has_value() && options.verify_checksums) {
    const std::filesystem::path root = effective_source_root(options);
    for (const SourceDataset& source : result.manifest->sources) {
      verify_source_checksum(source, root, result.report);
    }
  }

  if (has_errors(result.report)) {
    result.manifest.reset();
  }
  finalize_report(result.report);
  if (!options.report_path.empty()) {
    write_text_file(options.report_path, render_validation_report(result.report));
  }
  return result;
}

PackageResult create_package_manifest(const PackageOptions& options) {
  PackageResult result;
  ValidateOptions validate_options;
  validate_options.source_manifest_path = options.source_manifest_path;
  validate_options.source_root = options.source_root;
  validate_options.verify_checksums = options.verify_checksums;

  ValidationResult validation = validate_source_manifest(validate_options);
  result.report = std::move(validation.report);
  result.report.package_manifest_path = options.package_manifest_path.generic_string();

  if (!validation.accepted()) {
    finalize_report(result.report);
    if (!options.report_path.empty()) {
      write_text_file(options.report_path, render_validation_report(result.report));
    }
    return result;
  }

  if (options.package_version.empty()) {
    add_issue(result.report,
              "error",
              "package.version.missing",
              "Package manifest generation requires a non-empty package version.");
    finalize_report(result.report);
    if (!options.report_path.empty()) {
      write_text_file(options.report_path, render_validation_report(result.report));
    }
    return result;
  }

  PackageManifest package =
    build_package_manifest(*validation.manifest, options.package_name, options.package_version);
  result.report.package_id = package.package_id;
  result.package_manifest = package;
  write_text_file(options.package_manifest_path, package.json);
  finalize_report(result.report);
  if (!options.report_path.empty()) {
    write_text_file(options.report_path, render_validation_report(result.report));
  }
  return result;
}

} // namespace flying::data_pipeline
