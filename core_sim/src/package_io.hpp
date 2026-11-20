#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace flying::core_sim::package_io {
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
      if (!value.object.emplace(std::move(key), parse_value()).second) throw std::runtime_error("Duplicate package JSON member");
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
    const auto consume_number_character = [this](char character) {
      if (index_ < text_.size() && text_[index_] == character) { ++index_; return true; }
      return false;
    };
    const std::size_t begin = index_;
    if (consume_number_character('-') && index_ >= text_.size()) {
      throw std::runtime_error("incomplete JSON number");
    }
    if (consume_number_character('0')) {
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
    if (consume_number_character('.')) {
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
      throw std::runtime_error("invalid JSON number: " + number_text);
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

  [[nodiscard]] inline bool try_consume(char expected) noexcept {
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

[[nodiscard]] inline std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open installed package data: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] inline const JsonValue* find_member(const JsonValue::Object& object,
                                           std::string_view key) noexcept {
  const auto found = object.find(std::string(key));
  return found == object.end() ? nullptr : &found->second;
}

[[nodiscard]] inline const JsonValue::Object& as_object(const JsonValue& value,
                                                 std::string_view context) {
  if (value.type != JsonValue::Type::object_value) {
    throw std::runtime_error(std::string(context) + " must be an object");
  }
  return value.object;
}

[[nodiscard]] inline const JsonValue::Array& as_array(const JsonValue& value,
                                               std::string_view context) {
  if (value.type != JsonValue::Type::array_value) {
    throw std::runtime_error(std::string(context) + " must be an array");
  }
  return value.array;
}

[[nodiscard]] inline const JsonValue::Object& require_object(const JsonValue::Object& object,
                                                      std::string_view key,
                                                      std::string_view context) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " is required");
  }
  return as_object(*value, std::string(context) + "." + std::string(key));
}

[[nodiscard]] inline const JsonValue::Array& require_array(const JsonValue::Object& object,
                                                    std::string_view key,
                                                    std::string_view context) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr) {
    throw std::runtime_error(std::string(context) + "." + std::string(key) + " is required");
  }
  return as_array(*value, std::string(context) + "." + std::string(key));
}

[[nodiscard]] inline std::string require_string(const JsonValue::Object& object,
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

[[nodiscard]] inline double require_number(const JsonValue::Object& object,
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

[[nodiscard]] inline bool require_bool(const JsonValue::Object& object,
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

inline std::string sha256_text(std::string_view text) {
  Sha256 sha;
  sha.update(text);
  return sha.final_hex();
}

inline std::string sha256_file(const std::filesystem::path& path) {
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


}
