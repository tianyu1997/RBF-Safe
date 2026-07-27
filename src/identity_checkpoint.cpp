#include <rbfsafe/identity.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kCheckpointSchema = 1;
constexpr std::size_t kMaximumIdentifierBytes = 256;

bool valid_text(std::string_view value, std::size_t maximum_bytes) {
    return !value.empty() && value.size() <= maximum_bytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

internal::Json signature_json(const ServiceTrustCheckpointSignature& signature) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(signature.algorithm)},
        {"authentication_tag", signature.authentication_tag},
        {"signer_key_id", signature.signer_key_id},
        {"signer_service_id", signature.signer_service_id},
    };
}

internal::Json checkpoint_payload(const ServiceTrustCheckpoint& checkpoint) {
    internal::Json::Array signatures;
    signatures.reserve(checkpoint.signatures.size());
    for (const auto& signature : checkpoint.signatures)
        signatures.emplace_back(signature_json(signature));
    return internal::Json::Object{
        {"format", "rbfsafe-service-trust-checkpoint"},
        {"head_bundle_id", checkpoint.head_bundle_id},
        {"head_record_id", checkpoint.head_record_id},
        {"head_sequence", std::to_string(checkpoint.head_sequence)},
        {"root_bundle_id", checkpoint.root_bundle_id},
        {"schema", static_cast<double>(checkpoint.storage_schema)},
        {"signatures", std::move(signatures)},
    };
}

internal::Json checkpoint_json(const ServiceTrustCheckpoint& checkpoint) {
    auto object = checkpoint_payload(checkpoint).as_object();
    object.emplace("id", checkpoint.id);
    object.emplace("library_version", kVersion);
    return object;
}

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 std::size_t maximum_bytes) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "trust checkpoint is not a JSON object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().empty() ||
        value->as_string().size() > maximum_bytes) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "trust-checkpoint string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<std::size_t> integer_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object()) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "trust checkpoint is not a JSON object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || std::floor(value->as_number()) != value->as_number() ||
        value->as_number() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData,
                                            "trust-checkpoint integer field is invalid", std::string(key));
    }
    return static_cast<std::size_t>(value->as_number());
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key, 32);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData,
                                              "trust-checkpoint decimal field is invalid", std::string(key));
    }
    return result;
}

Result<internal::Json> read_bounded_json(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        return Result<internal::Json>::failure(StatusCode::CorruptData,
                                               "trust checkpoint is missing or is not a direct regular file",
                                               path.string());
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<internal::Json>::failure(StatusCode::IoError, "failed to inspect trust checkpoint",
                                               path.string());
    }
    if (bytes > maximum_bytes ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return Result<internal::Json>::failure(
            StatusCode::ResourceLimit, "trust checkpoint exceeds configured byte limit", path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<internal::Json>::failure(StatusCode::IoError, "failed to open trust checkpoint",
                                               path.string());
    }
    std::string text(static_cast<std::size_t>(bytes), '\0');
    if (!text.empty()) {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (input.gcount() != static_cast<std::streamsize>(text.size())) {
            return Result<internal::Json>::failure(StatusCode::CorruptData,
                                                   "trust checkpoint changed while reading", path.string());
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        return Result<internal::Json>::failure(StatusCode::CorruptData,
                                               "trust checkpoint changed while reading", path.string());
    }
    return internal::Json::parse(text);
}

Result<ServiceTrustCheckpointSignature> decode_signature(const internal::Json& object) {
    auto signer_service_id = string_field(object, "signer_service_id", kMaximumIdentifierBytes);
    auto signer_key_id = string_field(object, "signer_key_id", 64);
    auto algorithm = integer_field(object, "algorithm",
                                   static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto authentication_tag = string_field(object, "authentication_tag", kEd25519SignatureBytes * 2);
    if (!signer_service_id || !signer_key_id || !algorithm || !authentication_tag) {
        return Result<ServiceTrustCheckpointSignature>::failure(StatusCode::CorruptData,
                                                                "trust-checkpoint signature is incomplete");
    }
    ServiceTrustCheckpointSignature result;
    result.signer_service_id = std::move(signer_service_id).value();
    result.signer_key_id = std::move(signer_key_id).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(authentication_tag).value();
    if (!valid_service_trust_checkpoint_signature(result)) {
        return Result<ServiceTrustCheckpointSignature>::failure(
            StatusCode::CorruptData, "trust-checkpoint signature is invalid", result.signer_key_id);
    }
    return result;
}

Result<void> verify_checkpoint_signature(const ServiceTrustCheckpoint& checkpoint,
                                         const ServiceTrustCheckpointSignature& signature,
                                         const ServiceTrustBundle& head_bundle) {
    if (!valid_service_trust_checkpoint_signature(signature)) {
        return Result<void>::failure(StatusCode::CorruptData,
                                     "trust-checkpoint signature is structurally invalid");
    }
    auto found = head_bundle.key(signature.signer_service_id, signature.signer_key_id);
    if (!found)
        return found.error();
    const bool sequence_authorized = found.value() &&
                                     checkpoint.head_sequence >= found.value()->valid_from_sequence &&
                                     (found.value()->valid_through_sequence == 0 ||
                                      checkpoint.head_sequence <= found.value()->valid_through_sequence);
    if (!found.value() || found.value()->state != ServiceKeyState::Active || !found.value()->allow_rotate ||
        !sequence_authorized) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "service key is not authorized to sign the trust checkpoint",
                                     signature.signer_key_id);
    }
    auto decoded = internal::decode_hex(signature.authentication_tag, kEd25519SignatureBytes);
    if (!decoded)
        return decoded.error();
    const auto message = internal::service_trust_checkpoint_signature_message(checkpoint, signature);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), decoded.value(),
                          found.value()->public_key);
}

void wipe_secret(std::array<std::byte, kEd25519SecretKeyBytes>& secret) {
    volatile std::byte* current = secret.data();
    for (std::size_t index = 0; index < secret.size(); ++index)
        current[index] = std::byte{0};
}

} // namespace

namespace internal {

std::string service_trust_checkpoint_signature_message(const ServiceTrustCheckpoint& checkpoint,
                                                       const ServiceTrustCheckpointSignature& signature) {
    return std::string("rbfsafe-service-trust-checkpoint-signature-v1\n") +
           Json(Json::Object{
                    {"algorithm", static_cast<int>(signature.algorithm)},
                    {"format", "rbfsafe-service-trust-checkpoint-signature"},
                    {"head_bundle_id", checkpoint.head_bundle_id},
                    {"head_record_id", checkpoint.head_record_id},
                    {"head_sequence", std::to_string(checkpoint.head_sequence)},
                    {"root_bundle_id", checkpoint.root_bundle_id},
                    {"schema", 1},
                    {"signer_key_id", signature.signer_key_id},
                    {"signer_service_id", signature.signer_service_id},
                })
               .dump(false);
}

std::string service_trust_checkpoint_identity(const ServiceTrustCheckpoint& checkpoint) {
    return sha256(std::string("rbfsafe-service-trust-checkpoint-identity-v1\n") +
                  checkpoint_payload(checkpoint).dump(false));
}

} // namespace internal

bool valid_service_trust_checkpoint_signature(const ServiceTrustCheckpointSignature& signature) {
    return valid_text(signature.signer_service_id, kMaximumIdentifierBytes) &&
           internal::valid_sha256(signature.signer_key_id) &&
           signature.algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           internal::decode_hex(signature.authentication_tag, kEd25519SignatureBytes);
}

bool ServiceTrustCheckpoint::valid() const {
    if (storage_schema != kCheckpointSchema || !internal::valid_sha256(id) ||
        !internal::valid_sha256(root_bundle_id) || !internal::valid_sha256(head_bundle_id) ||
        head_sequence == 0 || !internal::valid_sha256(head_record_id) || signatures.empty()) {
        return false;
    }
    for (std::size_t index = 0; index < signatures.size(); ++index) {
        if (!valid_service_trust_checkpoint_signature(signatures[index]))
            return false;
        if (index > 0) {
            const auto& previous = signatures[index - 1];
            const auto& current = signatures[index];
            if (previous.signer_service_id > current.signer_service_id ||
                (previous.signer_service_id == current.signer_service_id &&
                 previous.signer_key_id >= current.signer_key_id)) {
                return false;
            }
        }
    }
    return internal::service_trust_checkpoint_identity(*this) == id;
}

Result<void> ServiceTrustCheckpoint::save(const std::filesystem::path& path,
                                          const SaveOptions& options) const {
    if (!valid() || path.empty() || path == path.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "trust checkpoint or destination is invalid");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError, "failed to inspect trust-checkpoint destination");
    }
    if (destination_exists && !options.overwrite) {
        return Result<void>::failure(StatusCode::IoError, "trust-checkpoint destination already exists");
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to create trust-checkpoint parent directory");
        }
    }
    const auto temporary = unique_sibling(path, ".tmp-");
    auto written = internal::write_text_file(temporary, checkpoint_json(*this).dump(true) + "\n");
    if (!written)
        return written;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(path, ".backup-");
        std::filesystem::rename(path, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return Result<void>::failure(StatusCode::IoError, "failed to stage existing trust checkpoint");
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        if (destination_exists)
            std::filesystem::rename(backup, path, ignored);
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(StatusCode::IoError, "failed to publish trust checkpoint");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
    }
    return Result<void>::success();
}

Result<ServiceTrustCheckpoint>
ServiceTrustCheckpoint::load(const std::filesystem::path& path,
                             const ServiceTrustCheckpointLoadOptions& options) {
    if (path.empty() || options.maximum_signatures == 0 || options.maximum_payload_bytes == 0) {
        return Result<ServiceTrustCheckpoint>::failure(StatusCode::InvalidArgument,
                                                       "trust-checkpoint load options are invalid");
    }
    auto document = read_bounded_json(path, options.maximum_payload_bytes);
    if (!document)
        return document.error();
    auto format = string_field(document.value(), "format", 128);
    auto schema = integer_field(document.value(), "schema", 1000);
    auto library_version = string_field(document.value(), "library_version", 128);
    auto id = string_field(document.value(), "id", 64);
    auto root_bundle_id = string_field(document.value(), "root_bundle_id", 64);
    auto head_bundle_id = string_field(document.value(), "head_bundle_id", 64);
    auto head_sequence = decimal_field(document.value(), "head_sequence");
    auto head_record_id = string_field(document.value(), "head_record_id", 64);
    const auto* signatures = document.value().is_object() ? document.value().find("signatures") : nullptr;
    if (!format || !schema || !library_version || !id || !root_bundle_id || !head_bundle_id ||
        !head_sequence || !head_record_id || signatures == nullptr || !signatures->is_array()) {
        return Result<ServiceTrustCheckpoint>::failure(StatusCode::CorruptData,
                                                       "trust-checkpoint document is incomplete");
    }
    if (format.value() != "rbfsafe-service-trust-checkpoint" || schema.value() != kCheckpointSchema) {
        return Result<ServiceTrustCheckpoint>::failure(StatusCode::IncompatibleFormat,
                                                       "unsupported trust-checkpoint schema");
    }
    if (signatures->as_array().size() > options.maximum_signatures) {
        return Result<ServiceTrustCheckpoint>::failure(
            StatusCode::ResourceLimit, "trust-checkpoint signature count exceeds configured limit");
    }
    ServiceTrustCheckpoint result;
    result.storage_schema = static_cast<std::uint32_t>(schema.value());
    result.id = std::move(id).value();
    result.root_bundle_id = std::move(root_bundle_id).value();
    result.head_bundle_id = std::move(head_bundle_id).value();
    result.head_sequence = head_sequence.value();
    result.head_record_id = std::move(head_record_id).value();
    result.signatures.reserve(signatures->as_array().size());
    for (const auto& item : signatures->as_array()) {
        auto signature = decode_signature(item);
        if (!signature)
            return signature.error();
        result.signatures.push_back(std::move(signature).value());
    }
    if (!result.valid()) {
        return Result<ServiceTrustCheckpoint>::failure(StatusCode::CorruptData,
                                                       "trust-checkpoint identity is invalid", result.id);
    }
    return result;
}

Result<ServiceTrustCheckpointSignature>
sign_service_trust_checkpoint(const ServiceTrustHistory& history, std::string signer_service_id,
                              std::string signer_key_id, std::span<const std::byte> ed25519_secret_key) {
    if (!history.valid() || !valid_text(signer_service_id, kMaximumIdentifierBytes) ||
        !internal::valid_sha256(signer_key_id) || ed25519_secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<ServiceTrustCheckpointSignature>::failure(StatusCode::InvalidArgument,
                                                                "trust-checkpoint signing input is invalid");
    }
    auto head = history.current_bundle();
    if (!head)
        return head.error();
    auto found = head.value().key(signer_service_id, signer_key_id);
    if (!found)
        return found.error();
    const bool sequence_authorized = found.value() &&
                                     head.value().sequence() >= found.value()->valid_from_sequence &&
                                     (found.value()->valid_through_sequence == 0 ||
                                      head.value().sequence() <= found.value()->valid_through_sequence);
    if (!found.value() || found.value()->state != ServiceKeyState::Active || !found.value()->allow_rotate ||
        !sequence_authorized) {
        return Result<ServiceTrustCheckpointSignature>::failure(
            StatusCode::IdentityMismatch, "service key is not authorized to sign the trust checkpoint",
            signer_key_id);
    }
    auto derived = ed25519_key_pair_from_seed(ed25519_secret_key.first(kEd25519SeedBytes));
    if (!derived)
        return derived.error();
    const bool matching_secret = std::equal(derived.value().secret_key.begin(),
                                            derived.value().secret_key.end(), ed25519_secret_key.begin());
    const bool matching_public = derived.value().public_key == found.value()->public_key;
    wipe_secret(derived.value().secret_key);
    if (!matching_secret || !matching_public) {
        return Result<ServiceTrustCheckpointSignature>::failure(
            StatusCode::IdentityMismatch, "Ed25519 secret key does not match the checkpoint signer",
            signer_key_id);
    }
    ServiceTrustCheckpoint checkpoint;
    checkpoint.root_bundle_id = history.root_bundle_id();
    checkpoint.head_bundle_id = history.current_bundle_id();
    checkpoint.head_sequence = head.value().sequence();
    checkpoint.head_record_id = history.records().back().id;
    ServiceTrustCheckpointSignature result;
    result.signer_service_id = std::move(signer_service_id);
    result.signer_key_id = std::move(signer_key_id);
    const auto message = internal::service_trust_checkpoint_signature_message(checkpoint, result);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    result.authentication_tag = internal::encode_hex(signature.value());
    if (!valid_service_trust_checkpoint_signature(result)) {
        return Result<ServiceTrustCheckpointSignature>::failure(
            StatusCode::InternalError, "generated trust-checkpoint signature is invalid");
    }
    return result;
}

Result<ServiceTrustCheckpoint>
assemble_service_trust_checkpoint(const ServiceTrustHistory& history,
                                  std::vector<ServiceTrustCheckpointSignature> signatures) {
    if (!history.valid() || signatures.empty()) {
        return Result<ServiceTrustCheckpoint>::failure(StatusCode::InvalidArgument,
                                                       "trust-checkpoint assembly input is invalid");
    }
    if (signatures.size() > 100'000) {
        return Result<ServiceTrustCheckpoint>::failure(
            StatusCode::ResourceLimit, "trust-checkpoint signature count exceeds the supported limit");
    }
    auto head = history.current_bundle();
    if (!head)
        return head.error();
    ServiceTrustCheckpoint result;
    result.root_bundle_id = history.root_bundle_id();
    result.head_bundle_id = history.current_bundle_id();
    result.head_sequence = head.value().sequence();
    result.head_record_id = history.records().back().id;
    std::sort(signatures.begin(), signatures.end(), [](const auto& left, const auto& right) {
        if (left.signer_service_id != right.signer_service_id)
            return left.signer_service_id < right.signer_service_id;
        return left.signer_key_id < right.signer_key_id;
    });
    std::set<std::string> services;
    for (std::size_t index = 0; index < signatures.size(); ++index) {
        if (index > 0 && signatures[index - 1].signer_service_id == signatures[index].signer_service_id &&
            signatures[index - 1].signer_key_id == signatures[index].signer_key_id) {
            return Result<ServiceTrustCheckpoint>::failure(StatusCode::InvalidArgument,
                                                           "trust checkpoint contains a duplicate signer",
                                                           signatures[index].signer_key_id);
        }
        auto verified = verify_checkpoint_signature(result, signatures[index], head.value());
        if (!verified)
            return verified.error();
        services.insert(signatures[index].signer_service_id);
    }
    const auto& policy = head.value().rotation_policy();
    if (signatures.size() < policy.minimum_signatures ||
        (policy.require_distinct_services && services.size() < policy.minimum_signatures)) {
        return Result<ServiceTrustCheckpoint>::failure(
            StatusCode::IdentityMismatch, "trust-checkpoint signatures do not satisfy the bundle policy",
            head.value().id());
    }
    result.signatures = std::move(signatures);
    result.id = internal::service_trust_checkpoint_identity(result);
    if (!result.valid()) {
        return Result<ServiceTrustCheckpoint>::failure(StatusCode::InternalError,
                                                       "generated trust checkpoint is invalid");
    }
    return result;
}

Result<void> verify_service_trust_checkpoint(const ServiceTrustHistory& history,
                                             const ServiceTrustCheckpoint& checkpoint,
                                             const std::string& expected_checkpoint_id) {
    if (!history.valid() || !checkpoint.valid() || !internal::valid_sha256(expected_checkpoint_id)) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "trust-checkpoint verification input is invalid");
    }
    if (checkpoint.id != expected_checkpoint_id || checkpoint.root_bundle_id != history.root_bundle_id() ||
        checkpoint.head_bundle_id != history.current_bundle_id() ||
        checkpoint.head_sequence != history.records().size() ||
        checkpoint.head_record_id != history.records().back().id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "trust checkpoint does not match caller pins or replayed history",
                                     checkpoint.id);
    }
    auto assembled = assemble_service_trust_checkpoint(history, checkpoint.signatures);
    if (!assembled)
        return assembled.error();
    if (assembled.value().id != checkpoint.id) {
        return Result<void>::failure(StatusCode::IdentityMismatch, "trust checkpoint is not canonical",
                                     checkpoint.id);
    }
    return Result<void>::success();
}

Result<ServiceTrustHistory> ServiceTrustHistory::open(const std::filesystem::path& directory,
                                                      const std::string& expected_root_bundle_id,
                                                      const ServiceTrustCheckpoint& checkpoint,
                                                      const std::string& expected_checkpoint_id,
                                                      const ServiceTrustHistoryLoadOptions& options) {
    if (!checkpoint.valid() || checkpoint.root_bundle_id != expected_root_bundle_id ||
        checkpoint.id != expected_checkpoint_id) {
        return Result<ServiceTrustHistory>::failure(
            StatusCode::IdentityMismatch, "trust checkpoint does not match the caller-provided anchors",
            checkpoint.id);
    }
    auto history = open(directory, expected_root_bundle_id, checkpoint.head_bundle_id, options);
    if (!history)
        return history.error();
    auto verified = verify_service_trust_checkpoint(history.value(), checkpoint, expected_checkpoint_id);
    if (!verified)
        return verified.error();
    return history;
}

} // namespace rbfsafe
