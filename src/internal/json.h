#pragma once

#include <rbfsafe/result.h>

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rbfsafe::internal {

class Json {
  public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool value) : value_(value) {}
    Json(double value) : value_(value) {}
    Json(int value) : value_(static_cast<double>(value)) {}
    Json(std::string value) : value_(std::move(value)) {}
    Json(const char* value) : value_(std::string(value)) {}
    Json(Array value) : value_(std::move(value)) {}
    Json(Object value) : value_(std::move(value)) {}

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_number() const { return std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_array() const { return std::holds_alternative<Array>(value_); }
    bool is_object() const { return std::holds_alternative<Object>(value_); }

    bool as_bool() const { return std::get<bool>(value_); }
    double as_number() const { return std::get<double>(value_); }
    const std::string& as_string() const { return std::get<std::string>(value_); }
    const Array& as_array() const { return std::get<Array>(value_); }
    Array& as_array() { return std::get<Array>(value_); }
    const Object& as_object() const { return std::get<Object>(value_); }
    Object& as_object() { return std::get<Object>(value_); }

    const Json* find(std::string_view key) const;
    std::string dump(bool pretty = false, int depth = 0) const;
    static Result<Json> parse(std::string_view text);

  private:
    Storage value_;
};

Result<Json> read_json_file(const std::filesystem::path& path);
Result<void> write_text_file(const std::filesystem::path& path, std::string_view text);

} // namespace rbfsafe::internal
