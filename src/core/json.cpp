#include "internal/json.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace rbfsafe::internal {
namespace {

class Parser {
  public:
    explicit Parser(std::string_view input) : input_(input) {}

    Result<Json> parse_document() {
        skip_space();
        auto value = parse_value(0);
        if (!value)
            return value;
        skip_space();
        if (position_ != input_.size())
            return fail("unexpected trailing JSON data");
        return value;
    }

  private:
    Result<Json> fail(std::string message) const {
        return Result<Json>::failure(StatusCode::CorruptData, std::move(message),
                                     "byte " + std::to_string(position_));
    }

    void skip_space() {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\n' && value != '\r' && value != '\t')
                break;
            ++position_;
        }
    }

    Result<Json> parse_value(std::size_t depth) {
        if (depth > 64)
            return fail("JSON nesting is too deep");
        skip_space();
        if (position_ >= input_.size())
            return fail("unexpected end of JSON");
        const char value = input_[position_];
        if (value == '{')
            return parse_object(depth + 1);
        if (value == '[')
            return parse_array(depth + 1);
        if (value == '"') {
            auto string = parse_string();
            if (!string)
                return string.error();
            return Json(std::move(string).value());
        }
        if (value == 't' && take_literal("true"))
            return Json(true);
        if (value == 'f' && take_literal("false"))
            return Json(false);
        if (value == 'n' && take_literal("null"))
            return Json(nullptr);
        if (value == '-' || (value >= '0' && value <= '9'))
            return parse_number();
        return fail("unexpected JSON token");
    }

    bool take_literal(std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal)
            return false;
        position_ += literal.size();
        return true;
    }

    Result<std::string> parse_string() {
        if (position_ >= input_.size() || input_[position_] != '"') {
            return Result<std::string>::failure(StatusCode::CorruptData, "expected JSON string");
        }
        ++position_;
        std::string output;
        while (position_ < input_.size()) {
            const unsigned char value = static_cast<unsigned char>(input_[position_++]);
            if (value == '"')
                return output;
            if (value < 0x20u) {
                return Result<std::string>::failure(StatusCode::CorruptData,
                                                    "control character in JSON string");
            }
            if (value != '\\') {
                output.push_back(static_cast<char>(value));
                continue;
            }
            if (position_ >= input_.size()) {
                return Result<std::string>::failure(StatusCode::CorruptData, "truncated JSON escape");
            }
            const char escape = input_[position_++];
            switch (escape) {
            case '"':
                output.push_back('"');
                break;
            case '\\':
                output.push_back('\\');
                break;
            case '/':
                output.push_back('/');
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
            case 'u': {
                if (position_ + 4 > input_.size()) {
                    return Result<std::string>::failure(StatusCode::CorruptData, "truncated Unicode escape");
                }
                unsigned code = 0;
                for (int index = 0; index < 4; ++index) {
                    const char digit = input_[position_++];
                    code <<= 4u;
                    if (digit >= '0' && digit <= '9')
                        code += static_cast<unsigned>(digit - '0');
                    else if (digit >= 'a' && digit <= 'f')
                        code += static_cast<unsigned>(digit - 'a' + 10);
                    else if (digit >= 'A' && digit <= 'F')
                        code += static_cast<unsigned>(digit - 'A' + 10);
                    else
                        return Result<std::string>::failure(StatusCode::CorruptData,
                                                            "invalid Unicode escape");
                }
                if (code <= 0x7fu)
                    output.push_back(static_cast<char>(code));
                else if (code <= 0x7ffu) {
                    output.push_back(static_cast<char>(0xc0u | (code >> 6u)));
                    output.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
                } else {
                    output.push_back(static_cast<char>(0xe0u | (code >> 12u)));
                    output.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3fu)));
                    output.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
                }
                break;
            }
            default:
                return Result<std::string>::failure(StatusCode::CorruptData, "invalid JSON escape");
            }
        }
        return Result<std::string>::failure(StatusCode::CorruptData, "unterminated JSON string");
    }

    Result<Json> parse_number() {
        const std::size_t start = position_;
        if (input_[position_] == '-')
            ++position_;
        if (position_ >= input_.size())
            return fail("truncated JSON number");
        if (input_[position_] == '0')
            ++position_;
        else {
            if (input_[position_] < '1' || input_[position_] > '9')
                return fail("invalid JSON number");
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const auto fraction_start = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
            if (fraction_start == position_)
                return fail("invalid JSON fraction");
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-'))
                ++position_;
            const auto exponent_start = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
            if (exponent_start == position_)
                return fail("invalid JSON exponent");
        }
        const std::string token(input_.substr(start, position_ - start));
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || !std::isfinite(number))
            return fail("invalid finite JSON number");
        return Json(number);
    }

    Result<Json> parse_array(std::size_t depth) {
        ++position_;
        Json::Array array;
        skip_space();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return Json(std::move(array));
        }
        while (true) {
            if (array.size() >= 10'000'000)
                return fail("JSON array exceeds resource limit");
            auto item = parse_value(depth);
            if (!item)
                return item;
            array.push_back(std::move(item).value());
            skip_space();
            if (position_ >= input_.size())
                return fail("unterminated JSON array");
            const char delimiter = input_[position_++];
            if (delimiter == ']')
                break;
            if (delimiter != ',')
                return fail("expected comma in JSON array");
        }
        return Json(std::move(array));
    }

    Result<Json> parse_object(std::size_t depth) {
        ++position_;
        Json::Object object;
        skip_space();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return Json(std::move(object));
        }
        while (true) {
            skip_space();
            auto key = parse_string();
            if (!key)
                return key.error();
            skip_space();
            if (position_ >= input_.size() || input_[position_++] != ':')
                return fail("expected colon in JSON object");
            auto value = parse_value(depth);
            if (!value)
                return value;
            if (!object.emplace(std::move(key).value(), std::move(value).value()).second) {
                return fail("duplicate JSON object key");
            }
            skip_space();
            if (position_ >= input_.size())
                return fail("unterminated JSON object");
            const char delimiter = input_[position_++];
            if (delimiter == '}')
                break;
            if (delimiter != ',')
                return fail("expected comma in JSON object");
        }
        return Json(std::move(object));
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

std::string escape_json(std::string_view input) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char value : input) {
        switch (value) {
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
            if (value < 0x20u) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned>(value) << std::dec;
            } else
                output << static_cast<char>(value);
        }
    }
    output << '"';
    return output.str();
}

} // namespace

const Json* Json::find(std::string_view key) const {
    if (!is_object())
        return nullptr;
    const auto iterator = as_object().find(std::string(key));
    return iterator == as_object().end() ? nullptr : &iterator->second;
}

std::string Json::dump(bool pretty, int depth) const {
    if (is_null())
        return "null";
    if (is_bool())
        return as_bool() ? "true" : "false";
    if (is_number()) {
        std::ostringstream output;
        output << std::setprecision(std::numeric_limits<double>::max_digits10) << as_number();
        return output.str();
    }
    if (is_string())
        return escape_json(as_string());

    const auto indent = [&](int level) {
        return pretty ? std::string(static_cast<std::size_t>(level) * 2u, ' ') : "";
    };
    const std::string newline = pretty ? "\n" : "";
    const std::string separator = pretty ? ": " : ":";
    std::ostringstream output;
    if (is_array()) {
        output << '[';
        if (!as_array().empty())
            output << newline;
        for (std::size_t index = 0; index < as_array().size(); ++index) {
            output << indent(depth + 1) << as_array()[index].dump(pretty, depth + 1);
            if (index + 1 != as_array().size())
                output << ',';
            output << newline;
        }
        if (!as_array().empty())
            output << indent(depth);
        output << ']';
        return output.str();
    }
    output << '{';
    if (!as_object().empty())
        output << newline;
    std::size_t index = 0;
    for (const auto& [key, value] : as_object()) {
        output << indent(depth + 1) << escape_json(key) << separator << value.dump(pretty, depth + 1);
        if (++index != as_object().size())
            output << ',';
        output << newline;
    }
    if (!as_object().empty())
        output << indent(depth);
    output << '}';
    return output.str();
}

Result<Json> Json::parse(std::string_view text) { return Parser(text).parse_document(); }

Result<Json> read_json_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Result<Json>::failure(StatusCode::IoError, "failed to open JSON file", path.string());
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(64 * 1024 * 1024)) {
        return Result<Json>::failure(StatusCode::ResourceLimit, "JSON file exceeds 64 MiB limit",
                                     path.string());
    }
    input.seekg(0, std::ios::beg);
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), size);
    if (!input && size != 0)
        return Result<Json>::failure(StatusCode::IoError, "failed to read JSON file", path.string());
    auto parsed = Json::parse(contents);
    if (!parsed)
        return Result<Json>(Error{parsed.error().code, parsed.error().message,
                                  path.string() + ": " + parsed.error().context});
    return parsed;
}

Result<void> write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return Result<void>::failure(StatusCode::IoError, "failed to open output file", path.string());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output)
        return Result<void>::failure(StatusCode::IoError, "failed to write output file", path.string());
    return Result<void>::success();
}

} // namespace rbfsafe::internal
