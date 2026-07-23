#include <rbfsafe/trust.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/memory.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rbfsafe {
namespace {

constexpr std::size_t kSchema = 1;
constexpr std::size_t kMinimumHmacKeyBytes = 32;
constexpr std::size_t kMaximumHmacKeyBytes = 4096;
constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumMediaTypeBytes = 256;

bool valid_text(std::string_view value, std::size_t maximum_bytes) {
    return !value.empty() && value.size() <= maximum_bytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_key(std::span<const std::byte> key) {
    return key.size() >= kMinimumHmacKeyBytes && key.size() <= kMaximumHmacKeyBytes;
}

internal::Json unsigned_json(const ArtifactAttestation& attestation) {
    return internal::Json::Object{
        {"algorithm", static_cast<double>(attestation.algorithm)},
        {"artifact_content_digest", attestation.artifact_content_digest},
        {"artifact_generation", std::to_string(attestation.artifact_generation)},
        {"artifact_id", attestation.artifact_id},
        {"artifact_state", static_cast<double>(attestation.artifact_state)},
        {"key_id", attestation.key_id},
        {"media_type", attestation.media_type},
        {"payload_bytes", std::to_string(attestation.payload_bytes)},
        {"payload_digest", attestation.payload_digest},
        {"sequence", std::to_string(attestation.sequence)},
        {"service_id", attestation.service_id},
    };
}

std::string unsigned_message(const ArtifactAttestation& attestation) {
    return std::string("rbfsafe-artifact-attestation-v1\n") + unsigned_json(attestation).dump(false);
}

std::string authentication_message(const ArtifactAttestation& attestation) {
    return std::string("rbfsafe-artifact-attestation-hmac-v1\n") + unsigned_json(attestation).dump(false);
}

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

Result<std::vector<std::byte>> read_payload(const std::filesystem::path& path,
                                            const ArtifactVerificationOptions& options) {
    if (path.empty() || options.maximum_payload_bytes == 0) {
        return Result<std::vector<std::byte>>::failure(StatusCode::InvalidArgument,
                                                       "artifact verification options are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<std::vector<std::byte>>::failure(StatusCode::Cancelled,
                                                       "artifact payload read was cancelled");
    }
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<std::vector<std::byte>>::failure(StatusCode::IoError,
                                                       "failed to inspect artifact payload", path.string());
    }
    if (bytes > options.maximum_payload_bytes ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return Result<std::vector<std::byte>>::failure(
            StatusCode::ResourceLimit, "artifact payload exceeds configured limit", path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::vector<std::byte>>::failure(StatusCode::IoError, "failed to open artifact payload",
                                                       path.string());
    }
    std::vector<std::byte> result(static_cast<std::size_t>(bytes));
    std::size_t offset = 0;
    constexpr std::size_t chunk = 64 * 1024;
    while (offset < result.size()) {
        if (options.cancellation.cancelled()) {
            return Result<std::vector<std::byte>>::failure(StatusCode::Cancelled,
                                                           "artifact payload read was cancelled");
        }
        const auto count = std::min(chunk, result.size() - offset);
        input.read(reinterpret_cast<char*>(result.data() + offset), static_cast<std::streamsize>(count));
        if (input.gcount() != static_cast<std::streamsize>(count)) {
            return Result<std::vector<std::byte>>::failure(
                StatusCode::CorruptData, "artifact payload changed while reading", path.string());
        }
        offset += count;
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        return Result<std::vector<std::byte>>::failure(
            StatusCode::CorruptData, "artifact payload changed while reading", path.string());
    }
    return result;
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 std::size_t maximum_bytes) {
    if (!object.is_object())
        return Result<std::string>::failure(StatusCode::CorruptData, "attestation is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || !valid_text(value->as_string(), maximum_bytes)) {
        return Result<std::string>::failure(StatusCode::CorruptData, "attestation string field is invalid",
                                            std::string(key));
    }
    return value->as_string();
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key, 32);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "attestation decimal field is invalid",
                                              std::string(key));
    }
    return result;
}

Result<std::size_t> enum_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object())
        return Result<std::size_t>::failure(StatusCode::CorruptData, "attestation is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || std::floor(value->as_number()) != value->as_number() ||
        value->as_number() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "attestation enum field is invalid",
                                            std::string(key));
    }
    return static_cast<std::size_t>(value->as_number());
}

internal::Json storage_json(const ArtifactAttestation& attestation) {
    auto fields = unsigned_json(attestation).as_object();
    fields.emplace("authentication_tag", attestation.authentication_tag);
    fields.emplace("format", "rbfsafe-artifact-attestation");
    fields.emplace("id", attestation.id);
    fields.emplace("library_version", kVersion);
    fields.emplace("schema", static_cast<double>(kSchema));
    return fields;
}

Result<void> publish_file(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                          bool destination_exists) {
    std::error_code error;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(destination, ".backup-");
        std::filesystem::rename(destination, backup, error);
        if (error)
            return Result<void>::failure(StatusCode::IoError, "failed to stage existing attestation");
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish artifact attestation");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
    }
    return Result<void>::success();
}

} // namespace

bool valid_artifact_attestation(const ArtifactAttestation& attestation) {
    return valid_text(attestation.service_id, kMaximumIdentifierBytes) &&
           valid_text(attestation.key_id, kMaximumIdentifierBytes) &&
           attestation.algorithm == ArtifactAuthenticationAlgorithm::HmacSha256 &&
           internal::valid_sha256(attestation.artifact_id) && attestation.artifact_generation > 0 &&
           attestation.artifact_state >= MemoryArtifactState::Active &&
           attestation.artifact_state <= MemoryArtifactState::Retired &&
           internal::valid_sha256(attestation.artifact_content_digest) &&
           internal::valid_sha256(attestation.payload_digest) &&
           valid_text(attestation.media_type, kMaximumMediaTypeBytes) &&
           internal::valid_sha256(attestation.id) && internal::valid_sha256(attestation.authentication_tag) &&
           internal::sha256(unsigned_message(attestation)) == attestation.id;
}

Result<ArtifactAttestation> attest_artifact(const MemoryArtifact& artifact,
                                            std::span<const std::byte> payload, std::string service_id,
                                            std::string key_id, std::span<const std::byte> hmac_key,
                                            std::uint64_t sequence, std::string media_type) {
    if (!internal::validate_memory_artifact(artifact) || !valid_text(service_id, kMaximumIdentifierBytes) ||
        !valid_text(key_id, kMaximumIdentifierBytes) || !valid_text(media_type, kMaximumMediaTypeBytes) ||
        !valid_key(hmac_key)) {
        return Result<ArtifactAttestation>::failure(StatusCode::InvalidArgument,
                                                    "artifact attestation input is invalid");
    }
    ArtifactAttestation result;
    result.sequence = sequence;
    result.service_id = std::move(service_id);
    result.key_id = std::move(key_id);
    result.artifact_id = artifact.id;
    result.artifact_generation = artifact.generation;
    result.artifact_state = artifact.state;
    result.artifact_content_digest = artifact.content_digest;
    result.payload_digest = internal::sha256(payload);
    result.payload_bytes = static_cast<std::uint64_t>(payload.size());
    result.media_type = std::move(media_type);
    result.id = internal::sha256(unsigned_message(result));
    const auto message = authentication_message(result);
    result.authentication_tag =
        internal::hmac_sha256(hmac_key, std::as_bytes(std::span(message.data(), message.size())));
    return result;
}

Result<ArtifactAttestation> attest_artifact_file(const MemoryArtifact& artifact,
                                                 const std::filesystem::path& payload_path,
                                                 std::string service_id, std::string key_id,
                                                 std::span<const std::byte> hmac_key, std::uint64_t sequence,
                                                 std::string media_type,
                                                 const ArtifactVerificationOptions& options) {
    auto payload = read_payload(payload_path, options);
    if (!payload)
        return payload.error();
    return attest_artifact(artifact, payload.value(), std::move(service_id), std::move(key_id), hmac_key,
                           sequence, std::move(media_type));
}

Result<void> verify_artifact(const MemoryArtifact& artifact, std::span<const std::byte> payload,
                             const ArtifactAttestation& attestation, std::string_view expected_service_id,
                             std::string_view expected_key_id, std::span<const std::byte> hmac_key) {
    if (!internal::validate_memory_artifact(artifact) ||
        !valid_text(expected_service_id, kMaximumIdentifierBytes) ||
        !valid_text(expected_key_id, kMaximumIdentifierBytes) || !valid_key(hmac_key)) {
        return Result<void>::failure(StatusCode::InvalidArgument, "artifact verification input is invalid");
    }
    if (!valid_artifact_attestation(attestation)) {
        return Result<void>::failure(StatusCode::CorruptData, "artifact attestation is malformed");
    }
    if (attestation.service_id != expected_service_id || attestation.key_id != expected_key_id ||
        attestation.artifact_id != artifact.id || attestation.artifact_generation != artifact.generation ||
        attestation.artifact_state != artifact.state ||
        attestation.artifact_content_digest != artifact.content_digest ||
        attestation.payload_bytes != static_cast<std::uint64_t>(payload.size()) ||
        attestation.payload_digest != internal::sha256(payload)) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "artifact, payload, service, or key identity does not match attestation",
                                     attestation.id);
    }
    const auto message = authentication_message(attestation);
    const auto expected_tag =
        internal::hmac_sha256(hmac_key, std::as_bytes(std::span(message.data(), message.size())));
    if (!internal::constant_time_equal(expected_tag, attestation.authentication_tag)) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "artifact authentication tag verification failed", attestation.id);
    }
    return Result<void>::success();
}

Result<void> verify_artifact_file(const MemoryArtifact& artifact, const std::filesystem::path& payload_path,
                                  const ArtifactAttestation& attestation,
                                  std::string_view expected_service_id, std::string_view expected_key_id,
                                  std::span<const std::byte> hmac_key,
                                  const ArtifactVerificationOptions& options) {
    auto payload = read_payload(payload_path, options);
    if (!payload)
        return payload.error();
    return verify_artifact(artifact, payload.value(), attestation, expected_service_id, expected_key_id,
                           hmac_key);
}

Result<void> save_artifact_attestation(const ArtifactAttestation& attestation,
                                       const std::filesystem::path& path, const SaveOptions& options) {
    if (!valid_artifact_attestation(attestation) || path.empty() || path == path.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "artifact attestation or destination is invalid");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to inspect attestation destination");
    if (destination_exists && !options.overwrite)
        return Result<void>::failure(StatusCode::IoError, "attestation destination already exists");
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return Result<void>::failure(StatusCode::IoError, "failed to create attestation parent");
    }
    const auto temporary = unique_sibling(path, ".tmp-");
    auto written = internal::write_text_file(temporary, storage_json(attestation).dump(true) + "\n");
    if (!written)
        return written;
    auto published = publish_file(temporary, path, destination_exists);
    if (!published) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return published;
}

Result<ArtifactAttestation> load_artifact_attestation(const std::filesystem::path& path,
                                                      std::uintmax_t maximum_bytes) {
    if (path.empty() || maximum_bytes == 0) {
        return Result<ArtifactAttestation>::failure(StatusCode::InvalidArgument,
                                                    "attestation load options are invalid");
    }
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error)
        return Result<ArtifactAttestation>::failure(StatusCode::IoError, "failed to inspect attestation");
    if (bytes > maximum_bytes) {
        return Result<ArtifactAttestation>::failure(StatusCode::ResourceLimit,
                                                    "attestation exceeds configured byte limit");
    }
    auto document = internal::read_json_file(path);
    if (!document)
        return document.error();
    auto format = string_field(document.value(), "format", 128);
    auto schema = enum_field(document.value(), "schema", 1000);
    auto library_version = string_field(document.value(), "library_version", 128);
    auto id = string_field(document.value(), "id", 64);
    auto service_id = string_field(document.value(), "service_id", kMaximumIdentifierBytes);
    auto key_id = string_field(document.value(), "key_id", kMaximumIdentifierBytes);
    auto algorithm = enum_field(document.value(), "algorithm",
                                static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::HmacSha256));
    auto artifact_id = string_field(document.value(), "artifact_id", 64);
    auto artifact_generation = decimal_field(document.value(), "artifact_generation");
    auto artifact_state = enum_field(document.value(), "artifact_state",
                                     static_cast<std::size_t>(MemoryArtifactState::Retired));
    auto content_digest = string_field(document.value(), "artifact_content_digest", 64);
    auto payload_digest = string_field(document.value(), "payload_digest", 64);
    auto payload_bytes = decimal_field(document.value(), "payload_bytes");
    auto sequence = decimal_field(document.value(), "sequence");
    auto media_type = string_field(document.value(), "media_type", kMaximumMediaTypeBytes);
    auto tag = string_field(document.value(), "authentication_tag", 64);
    if (!format || !schema || !library_version || !id || !service_id || !key_id || !algorithm ||
        !artifact_id || !artifact_generation || !artifact_state || !content_digest || !payload_digest ||
        !payload_bytes || !sequence || !media_type || !tag) {
        return Result<ArtifactAttestation>::failure(StatusCode::CorruptData,
                                                    "artifact attestation is incomplete");
    }
    if (format.value() != "rbfsafe-artifact-attestation" || schema.value() != kSchema) {
        return Result<ArtifactAttestation>::failure(StatusCode::IncompatibleFormat,
                                                    "unsupported artifact attestation schema");
    }
    ArtifactAttestation result;
    result.sequence = sequence.value();
    result.id = std::move(id).value();
    result.service_id = std::move(service_id).value();
    result.key_id = std::move(key_id).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.artifact_id = std::move(artifact_id).value();
    result.artifact_generation = artifact_generation.value();
    result.artifact_state = static_cast<MemoryArtifactState>(artifact_state.value());
    result.artifact_content_digest = std::move(content_digest).value();
    result.payload_digest = std::move(payload_digest).value();
    result.payload_bytes = payload_bytes.value();
    result.media_type = std::move(media_type).value();
    result.authentication_tag = std::move(tag).value();
    if (!valid_artifact_attestation(result)) {
        return Result<ArtifactAttestation>::failure(StatusCode::CorruptData,
                                                    "artifact attestation identity is invalid");
    }
    return result;
}

std::string artifact_authentication_algorithm_name(ArtifactAuthenticationAlgorithm algorithm) {
    switch (algorithm) {
    case ArtifactAuthenticationAlgorithm::HmacSha256:
        return "hmac_sha256";
    }
    return "unknown";
}

} // namespace rbfsafe
