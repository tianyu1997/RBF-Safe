#include <rbfsafe/identity.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/identity.h"
#include "internal/json.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kSchema = 1;
constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumStringBytes = 4096;
constexpr std::size_t kMaximumExactJsonInteger = sizeof(std::size_t) < sizeof(std::uint64_t)
                                                     ? std::numeric_limits<std::size_t>::max()
                                                     : static_cast<std::size_t>(9'007'199'254'740'991ULL);

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json key_json(const ServicePublicKey& key) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(key.algorithm)},
        {"allow_fetch", key.allow_fetch},
        {"allow_publish", key.allow_publish},
        {"id", key.id},
        {"public_key", internal::encode_hex(key.public_key)},
        {"service_id", key.service_id},
        {"state", static_cast<int>(key.state)},
        {"valid_from_sequence", std::to_string(key.valid_from_sequence)},
        {"valid_through_sequence", std::to_string(key.valid_through_sequence)},
    };
}

internal::Json storage_json(const ServiceTrustBundle& bundle) {
    internal::Json::Array keys;
    keys.reserve(bundle.keys().size());
    for (const auto& key : bundle.keys())
        keys.emplace_back(key_json(key));
    return internal::Json::Object{
        {"format", "rbfsafe-service-trust-bundle"},
        {"id", bundle.id()},
        {"keys", std::move(keys)},
        {"library_version", kVersion},
        {"parent_id", bundle.parent_id()},
        {"schema", static_cast<double>(kSchema)},
        {"sequence", std::to_string(bundle.sequence())},
    };
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 std::size_t maximum_bytes, bool allow_empty = false) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "expected JSON object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > maximum_bytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData, "missing or invalid string field",
                                            std::string(key));
    }
    return value->as_string();
}

Result<std::size_t> integer_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object()) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "expected JSON object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || std::floor(value->as_number()) != value->as_number() ||
        value->as_number() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "invalid integer field",
                                            std::string(key));
    }
    return static_cast<std::size_t>(value->as_number());
}

Result<bool> bool_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<bool>::failure(StatusCode::CorruptData, "expected JSON object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_bool()) {
        return Result<bool>::failure(StatusCode::CorruptData, "invalid Boolean field", std::string(key));
    }
    return value->as_bool();
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto value = string_field(object, key, 32, true);
    if (!value)
        return value.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(value.value().data(), value.value().data() + value.value().size(), result);
    if (value.value().empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.value().data() + value.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "invalid decimal field",
                                              std::string(key));
    }
    return result;
}

Result<internal::Json> read_bounded_json(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<internal::Json>::failure(StatusCode::IoError, "failed to inspect service trust bundle",
                                               path.string());
    }
    if (bytes > maximum_bytes ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return Result<internal::Json>::failure(
            StatusCode::ResourceLimit, "service trust bundle exceeds configured byte limit", path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<internal::Json>::failure(StatusCode::IoError, "failed to open service trust bundle",
                                               path.string());
    }
    std::string text(static_cast<std::size_t>(bytes), '\0');
    if (!text.empty()) {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (input.gcount() != static_cast<std::streamsize>(text.size())) {
            return Result<internal::Json>::failure(
                StatusCode::CorruptData, "service trust bundle changed while reading", path.string());
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        return Result<internal::Json>::failure(StatusCode::CorruptData,
                                               "service trust bundle changed while reading", path.string());
    }
    return internal::Json::parse(text);
}

Result<ServicePublicKey> decode_key(const internal::Json& object) {
    auto id = string_field(object, "id", 64);
    auto service_id = string_field(object, "service_id", kMaximumIdentifierBytes);
    auto algorithm = integer_field(object, "algorithm",
                                   static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto public_key_text = string_field(object, "public_key", 64);
    auto valid_from = decimal_field(object, "valid_from_sequence");
    auto valid_through = decimal_field(object, "valid_through_sequence");
    auto state = integer_field(object, "state", static_cast<std::size_t>(ServiceKeyState::Revoked));
    auto allow_fetch = bool_field(object, "allow_fetch");
    auto allow_publish = bool_field(object, "allow_publish");
    if (!id || !service_id || !algorithm || !public_key_text || !valid_from || !valid_through || !state ||
        !allow_fetch || !allow_publish) {
        return Result<ServicePublicKey>::failure(StatusCode::CorruptData,
                                                 "service trust-bundle key is incomplete");
    }
    auto public_key = internal::decode_hex(public_key_text.value(), kEd25519PublicKeyBytes);
    if (!public_key)
        return public_key.error();
    ServicePublicKey result;
    result.id = std::move(id).value();
    result.service_id = std::move(service_id).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    std::memcpy(result.public_key.data(), public_key.value().data(), result.public_key.size());
    result.valid_from_sequence = valid_from.value();
    result.valid_through_sequence = valid_through.value();
    result.state = static_cast<ServiceKeyState>(state.value());
    result.allow_fetch = allow_fetch.value();
    result.allow_publish = allow_publish.value();
    if (!valid_service_public_key(result)) {
        return Result<ServicePublicKey>::failure(StatusCode::CorruptData,
                                                 "service trust-bundle key identity is invalid", result.id);
    }
    return result;
}

Result<void> publish_file(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                          bool destination_exists) {
    std::error_code error;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(destination, ".backup-");
        std::filesystem::rename(destination, backup, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to stage existing service trust bundle");
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish service trust bundle");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
    }
    return Result<void>::success();
}

} // namespace

Result<void> save_service_trust_bundle(const ServiceTrustBundle& bundle, const std::filesystem::path& path,
                                       const SaveOptions& options) {
    if (!bundle.valid() || path.empty() || path == path.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "service trust bundle or destination is invalid");
    }
    if (bundle.keys().size() > kMaximumExactJsonInteger) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "service trust-bundle key count exceeds storage format");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to inspect service trust-bundle destination");
    }
    if (destination_exists && !options.overwrite) {
        return Result<void>::failure(StatusCode::IoError, "service trust-bundle destination already exists");
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError, "failed to create service trust-bundle parent");
        }
    }
    const auto temporary = unique_sibling(path, ".tmp-");
    auto written = internal::write_text_file(temporary, storage_json(bundle).dump(true) + "\n");
    if (!written)
        return written;
    auto published = publish_file(temporary, path, destination_exists);
    if (!published) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return published;
}

Result<ServiceTrustBundle> load_service_trust_bundle(const std::filesystem::path& path,
                                                     const ServiceTrustBundleLoadOptions& options) {
    if (path.empty() || options.maximum_keys == 0 || options.maximum_payload_bytes == 0) {
        return Result<ServiceTrustBundle>::failure(StatusCode::InvalidArgument,
                                                   "service trust-bundle load options are invalid");
    }
    auto document = read_bounded_json(path, options.maximum_payload_bytes);
    if (!document)
        return document.error();
    auto format = string_field(document.value(), "format", 128);
    auto schema = integer_field(document.value(), "schema", 1000);
    auto library_version = string_field(document.value(), "library_version", 128);
    auto id = string_field(document.value(), "id", 64);
    auto sequence = decimal_field(document.value(), "sequence");
    auto parent_id = string_field(document.value(), "parent_id", 64, true);
    const auto* keys_json = document.value().is_object() ? document.value().find("keys") : nullptr;
    if (!format || !schema || !library_version || !id || !sequence || !parent_id || keys_json == nullptr ||
        !keys_json->is_array()) {
        return Result<ServiceTrustBundle>::failure(StatusCode::CorruptData,
                                                   "service trust-bundle document is incomplete");
    }
    if (format.value() != "rbfsafe-service-trust-bundle" || schema.value() != kSchema) {
        return Result<ServiceTrustBundle>::failure(StatusCode::IncompatibleFormat,
                                                   "unsupported service trust-bundle schema");
    }
    if (keys_json->as_array().size() > options.maximum_keys) {
        return Result<ServiceTrustBundle>::failure(StatusCode::ResourceLimit,
                                                   "service trust-bundle key count exceeds configured limit");
    }
    std::vector<ServicePublicKey> keys;
    keys.reserve(keys_json->as_array().size());
    for (const auto& key_json_value : keys_json->as_array()) {
        auto key = decode_key(key_json_value);
        if (!key)
            return key.error();
        keys.push_back(std::move(key).value());
    }
    auto result = ServiceTrustBundle::create(sequence.value(), std::move(parent_id).value(), std::move(keys));
    if (!result)
        return Result<ServiceTrustBundle>::failure(StatusCode::CorruptData,
                                                   "service trust-bundle structure is invalid");
    if (result.value().id() != id.value()) {
        return Result<ServiceTrustBundle>::failure(StatusCode::CorruptData,
                                                   "service trust-bundle identity is invalid", id.value());
    }
    return result;
}

} // namespace rbfsafe
