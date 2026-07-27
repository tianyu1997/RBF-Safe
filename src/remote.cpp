#include <rbfsafe/remote.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/memory.h"
#include "internal/remote.h"
#include "internal/sha256.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumLocatorBytes = 4096;
constexpr std::size_t kMaximumMediaTypeBytes = 256;
constexpr std::size_t kMinimumHmacKeyBytes = 32;
constexpr std::size_t kMaximumHmacKeyBytes = 4096;

bool valid_text(std::string_view value, std::size_t maximum_bytes) {
    return !value.empty() && value.size() <= maximum_bytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_state(MemoryArtifactState state) {
    return state >= MemoryArtifactState::Active && state <= MemoryArtifactState::Retired;
}

bool valid_operation(ArtifactTransferOperation operation) {
    return operation >= ArtifactTransferOperation::Fetch && operation <= ArtifactTransferOperation::Publish;
}

bool valid_authentication(ArtifactTransferAuthentication authentication) {
    return authentication >= ArtifactTransferAuthentication::None &&
           authentication <= ArtifactTransferAuthentication::HmacSha256;
}

bool valid_key(std::span<const std::byte> key) {
    return key.size() >= kMinimumHmacKeyBytes && key.size() <= kMaximumHmacKeyBytes;
}

internal::Json fetch_request_json(const ArtifactFetchRequest& request) {
    return internal::Json::Object{
        {"artifact_content_digest", request.artifact_content_digest},
        {"artifact_generation", std::to_string(request.artifact_generation)},
        {"artifact_id", request.artifact_id},
        {"artifact_state", static_cast<int>(request.artifact_state)},
        {"format", "rbfsafe-artifact-fetch-request"},
        {"locator", request.locator},
        {"maximum_payload_bytes", std::to_string(request.maximum_payload_bytes)},
        {"media_type", request.media_type},
        {"memory_id", request.memory_id},
        {"response_authentication", static_cast<int>(request.response_authentication)},
        {"schema", 1},
        {"sequence", std::to_string(request.sequence)},
        {"service_id", request.service_id},
    };
}

internal::Json fetch_response_json(const ArtifactFetchResponse& response) {
    return internal::Json::Object{
        {"artifact_content_digest", response.artifact_content_digest},
        {"artifact_generation", std::to_string(response.artifact_generation)},
        {"artifact_id", response.artifact_id},
        {"artifact_state", static_cast<int>(response.artifact_state)},
        {"format", "rbfsafe-artifact-fetch-response"},
        {"media_type", response.media_type},
        {"payload_bytes", std::to_string(response.payload_bytes)},
        {"payload_digest", response.payload_digest},
        {"request_id", response.request_id},
        {"schema", 1},
        {"service_id", response.service_id},
        {"service_sequence", std::to_string(response.service_sequence)},
    };
}

internal::Json publish_request_json(const ArtifactPublishRequest& request) {
    return internal::Json::Object{
        {"artifact_content_digest", request.artifact_content_digest},
        {"artifact_generation", std::to_string(request.artifact_generation)},
        {"artifact_id", request.artifact_id},
        {"artifact_state", static_cast<int>(request.artifact_state)},
        {"format", "rbfsafe-artifact-publish-request"},
        {"locator", request.locator},
        {"media_type", request.media_type},
        {"memory_id", request.memory_id},
        {"payload_bytes", std::to_string(request.payload_bytes)},
        {"payload_digest", request.payload_digest},
        {"receipt_authentication", static_cast<int>(request.receipt_authentication)},
        {"schema", 1},
        {"sequence", std::to_string(request.sequence)},
        {"service_id", request.service_id},
    };
}

internal::Json publish_receipt_json(const ArtifactPublishReceipt& receipt) {
    return internal::Json::Object{
        {"artifact_content_digest", receipt.artifact_content_digest},
        {"artifact_generation", std::to_string(receipt.artifact_generation)},
        {"artifact_id", receipt.artifact_id},
        {"artifact_state", static_cast<int>(receipt.artifact_state)},
        {"format", "rbfsafe-artifact-publish-receipt"},
        {"media_type", receipt.media_type},
        {"payload_bytes", std::to_string(receipt.payload_bytes)},
        {"payload_digest", receipt.payload_digest},
        {"request_id", receipt.request_id},
        {"schema", 1},
        {"service_id", receipt.service_id},
        {"service_sequence", std::to_string(receipt.service_sequence)},
    };
}

internal::Json transfer_json(const VerifiedArtifactTransfer& transfer) {
    return internal::Json::Object{
        {"artifact_content_digest", transfer.artifact_content_digest},
        {"artifact_generation", std::to_string(transfer.artifact_generation)},
        {"artifact_id", transfer.artifact_id},
        {"artifact_state", static_cast<int>(transfer.artifact_state)},
        {"attestation_id", transfer.attestation_id},
        {"authentication", static_cast<int>(transfer.authentication)},
        {"format", "rbfsafe-verified-artifact-transfer"},
        {"media_type", transfer.media_type},
        {"memory_id", transfer.memory_id},
        {"operation", static_cast<int>(transfer.operation)},
        {"payload_bytes", std::to_string(transfer.payload_bytes)},
        {"payload_digest", transfer.payload_digest},
        {"request_id", transfer.request_id},
        {"response_id", transfer.response_id},
        {"schema", 1},
        {"service_id", transfer.service_id},
        {"service_sequence", std::to_string(transfer.service_sequence)},
    };
}

internal::Json transfer_attestation_json(const ArtifactTransferAttestation& attestation) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(attestation.algorithm)},
        {"artifact_id", attestation.artifact_id},
        {"key_id", attestation.key_id},
        {"operation", static_cast<int>(attestation.operation)},
        {"payload_bytes", std::to_string(attestation.payload_bytes)},
        {"payload_digest", attestation.payload_digest},
        {"request_id", attestation.request_id},
        {"response_id", attestation.response_id},
        {"service_id", attestation.service_id},
        {"service_sequence", std::to_string(attestation.service_sequence)},
    };
}

std::string transfer_attestation_identity_message(const ArtifactTransferAttestation& attestation) {
    return std::string("rbfsafe-artifact-transfer-attestation-v1\n") +
           transfer_attestation_json(attestation).dump(false);
}

std::string transfer_attestation_authentication_message(const ArtifactTransferAttestation& attestation) {
    return std::string("rbfsafe-artifact-transfer-attestation-hmac-v1\n") +
           transfer_attestation_json(attestation).dump(false);
}

Result<MemoryArtifact> current_artifact(const SafetyMemory& memory, const std::string& artifact_id,
                                        bool require_active) {
    if (!memory.valid() || !internal::valid_sha256(artifact_id)) {
        return Result<MemoryArtifact>::failure(StatusCode::InvalidArgument,
                                               "remote artifact memory or artifact ID is invalid");
    }
    auto found = memory.artifact(artifact_id);
    if (!found)
        return found.error();
    if (!found.value()) {
        return Result<MemoryArtifact>::failure(StatusCode::IdentityMismatch,
                                               "artifact is not present in the supplied safety memory",
                                               artifact_id);
    }
    if (require_active && found.value()->state != MemoryArtifactState::Active) {
        return Result<MemoryArtifact>::failure(StatusCode::IdentityMismatch,
                                               "remote transfer requires an active artifact", artifact_id);
    }
    return *found.value();
}

Result<void> validate_options(const RemoteArtifactOptions& options) {
    if (options.maximum_payload_bytes == 0 ||
        options.maximum_payload_bytes >
            static_cast<std::uintmax_t>(std::numeric_limits<std::uint64_t>::max())) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "remote artifact transfer options are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<void>::failure(StatusCode::Cancelled, "remote artifact transfer was cancelled");
    }
    return Result<void>::success();
}

Result<MemoryArtifact> verify_request_artifact(const SafetyMemory& memory, const std::string& memory_id,
                                               const std::string& artifact_id, std::uint64_t generation,
                                               MemoryArtifactState state, const std::string& content_digest,
                                               const RemoteArtifactOptions& options) {
    auto options_status = validate_options(options);
    if (!options_status)
        return options_status.error();
    if (memory.identity() != memory_id) {
        return Result<MemoryArtifact>::failure(
            StatusCode::IdentityMismatch,
            "safety-memory identity changed after the remote request was prepared", memory_id);
    }
    auto artifact = current_artifact(memory, artifact_id, options.require_active_artifact);
    if (!artifact)
        return artifact.error();
    if (artifact.value().generation != generation || artifact.value().state != state ||
        artifact.value().content_digest != content_digest) {
        return Result<MemoryArtifact>::failure(
            StatusCode::IdentityMismatch,
            "artifact lifecycle or content changed after the remote request was prepared", artifact_id);
    }
    return artifact;
}

Result<std::string> verify_authentication(ArtifactTransferOperation operation, const std::string& request_id,
                                          const std::string& response_id, const std::string& service_id,
                                          const std::string& artifact_id, const std::string& payload_digest,
                                          std::uint64_t payload_bytes, std::uint64_t service_sequence,
                                          ArtifactTransferAuthentication authentication,
                                          const std::optional<ArtifactTransferAttestation>& attestation,
                                          std::string_view expected_key_id,
                                          std::span<const std::byte> hmac_key) {
    if (authentication == ArtifactTransferAuthentication::None) {
        if (attestation || !expected_key_id.empty() || !hmac_key.empty()) {
            return Result<std::string>::failure(
                StatusCode::IdentityMismatch,
                "unauthenticated transfer contains unexpected authentication material");
        }
        return std::string{};
    }
    if (authentication != ArtifactTransferAuthentication::HmacSha256 ||
        !valid_text(expected_key_id, kMaximumIdentifierBytes) || !valid_key(hmac_key)) {
        return Result<std::string>::failure(
            StatusCode::InvalidArgument,
            "authenticated remote transfer requires a valid trusted key ID and HMAC key");
    }
    if (!attestation) {
        return Result<std::string>::failure(StatusCode::IdentityMismatch,
                                            "remote response is missing its required service attestation");
    }
    if (!valid_artifact_transfer_attestation(*attestation)) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "remote service transfer attestation is malformed");
    }
    if (attestation->operation != operation || attestation->request_id != request_id ||
        attestation->response_id != response_id || attestation->service_id != service_id ||
        attestation->key_id != expected_key_id || attestation->artifact_id != artifact_id ||
        attestation->payload_digest != payload_digest || attestation->payload_bytes != payload_bytes ||
        attestation->service_sequence != service_sequence) {
        return Result<std::string>::failure(StatusCode::IdentityMismatch,
                                            "remote service attestation does not match the complete transfer",
                                            attestation->id);
    }
    const auto message = transfer_attestation_authentication_message(*attestation);
    const auto expected_tag =
        internal::hmac_sha256(hmac_key, std::as_bytes(std::span(message.data(), message.size())));
    if (!internal::constant_time_equal(expected_tag, attestation->authentication_tag)) {
        return Result<std::string>::failure(StatusCode::IdentityMismatch,
                                            "remote service authentication tag verification failed",
                                            attestation->id);
    }
    return attestation->id;
}

ArtifactTransferAttestation
make_transfer_attestation(ArtifactTransferOperation operation, const std::string& request_id,
                          const std::string& response_id, const std::string& service_id,
                          const std::string& artifact_id, const std::string& payload_digest,
                          std::uint64_t payload_bytes, std::uint64_t service_sequence, std::string key_id,
                          std::span<const std::byte> hmac_key) {
    ArtifactTransferAttestation result;
    result.service_id = service_id;
    result.key_id = std::move(key_id);
    result.operation = operation;
    result.request_id = request_id;
    result.response_id = response_id;
    result.artifact_id = artifact_id;
    result.payload_digest = payload_digest;
    result.payload_bytes = payload_bytes;
    result.service_sequence = service_sequence;
    result.id = internal::sha256(transfer_attestation_identity_message(result));
    const auto message = transfer_attestation_authentication_message(result);
    result.authentication_tag =
        internal::hmac_sha256(hmac_key, std::as_bytes(std::span(message.data(), message.size())));
    return result;
}

VerifiedArtifactTransfer
make_verified_transfer(ArtifactTransferOperation operation, const std::string& request_id,
                       const std::string& response_id, const std::string& service_id,
                       const std::string& memory_id, const MemoryArtifact& artifact,
                       const std::string& payload_digest, std::uint64_t payload_bytes,
                       const std::string& media_type, std::uint64_t service_sequence,
                       ArtifactTransferAuthentication authentication, std::string attestation_id) {
    VerifiedArtifactTransfer result;
    result.operation = operation;
    result.request_id = request_id;
    result.response_id = response_id;
    result.service_id = service_id;
    result.memory_id = memory_id;
    result.artifact_id = artifact.id;
    result.artifact_generation = artifact.generation;
    result.artifact_state = artifact.state;
    result.artifact_content_digest = artifact.content_digest;
    result.payload_digest = payload_digest;
    result.payload_bytes = payload_bytes;
    result.media_type = media_type;
    result.service_sequence = service_sequence;
    result.authentication = authentication;
    result.attestation_id = std::move(attestation_id);
    result.id = internal::verified_artifact_transfer_identity(result);
    return result;
}

} // namespace

namespace internal {

std::string artifact_fetch_request_identity(const ArtifactFetchRequest& request) {
    return sha256(fetch_request_json(request).dump(false));
}

std::string artifact_fetch_response_identity(const ArtifactFetchResponse& response) {
    return sha256(fetch_response_json(response).dump(false));
}

std::string artifact_publish_request_identity(const ArtifactPublishRequest& request) {
    return sha256(publish_request_json(request).dump(false));
}

std::string artifact_publish_receipt_identity(const ArtifactPublishReceipt& receipt) {
    return sha256(publish_receipt_json(receipt).dump(false));
}

std::string verified_artifact_transfer_identity(const VerifiedArtifactTransfer& transfer) {
    return sha256(transfer_json(transfer).dump(false));
}

std::string artifact_transfer_record_identity(const ArtifactTransferRecord& record) {
    return sha256(Json(Json::Object{
                           {"format", "rbfsafe-artifact-transfer-record"},
                           {"parent_id", record.parent_id},
                           {"schema", 1},
                           {"sequence", std::to_string(record.sequence)},
                           {"transfer_id", record.transfer.id},
                       })
                      .dump(false));
}

} // namespace internal

bool valid_artifact_fetch_request(const ArtifactFetchRequest& request) {
    return request.sequence > 0 && internal::valid_sha256(request.id) &&
           valid_text(request.service_id, kMaximumIdentifierBytes) &&
           internal::valid_sha256(request.memory_id) && internal::valid_sha256(request.artifact_id) &&
           request.artifact_generation > 0 && valid_state(request.artifact_state) &&
           internal::valid_sha256(request.artifact_content_digest) &&
           valid_text(request.locator, kMaximumLocatorBytes) &&
           valid_text(request.media_type, kMaximumMediaTypeBytes) && request.maximum_payload_bytes > 0 &&
           valid_authentication(request.response_authentication) &&
           internal::artifact_fetch_request_identity(request) == request.id;
}

bool valid_artifact_fetch_response(const ArtifactFetchResponse& response) {
    return internal::valid_sha256(response.id) && internal::valid_sha256(response.request_id) &&
           valid_text(response.service_id, kMaximumIdentifierBytes) &&
           internal::valid_sha256(response.artifact_id) && response.artifact_generation > 0 &&
           valid_state(response.artifact_state) && internal::valid_sha256(response.artifact_content_digest) &&
           internal::valid_sha256(response.payload_digest) &&
           valid_text(response.media_type, kMaximumMediaTypeBytes) && response.service_sequence > 0 &&
           (!response.service_attestation ||
            valid_artifact_transfer_attestation(*response.service_attestation)) &&
           internal::artifact_fetch_response_identity(response) == response.id;
}

bool valid_artifact_publish_request(const ArtifactPublishRequest& request) {
    return request.sequence > 0 && internal::valid_sha256(request.id) &&
           valid_text(request.service_id, kMaximumIdentifierBytes) &&
           internal::valid_sha256(request.memory_id) && internal::valid_sha256(request.artifact_id) &&
           request.artifact_generation > 0 && valid_state(request.artifact_state) &&
           internal::valid_sha256(request.artifact_content_digest) &&
           valid_text(request.locator, kMaximumLocatorBytes) &&
           internal::valid_sha256(request.payload_digest) &&
           valid_text(request.media_type, kMaximumMediaTypeBytes) &&
           valid_authentication(request.receipt_authentication) &&
           request.payload_digest == request.artifact_content_digest &&
           internal::artifact_publish_request_identity(request) == request.id;
}

bool valid_artifact_publish_receipt(const ArtifactPublishReceipt& receipt) {
    return internal::valid_sha256(receipt.id) && internal::valid_sha256(receipt.request_id) &&
           valid_text(receipt.service_id, kMaximumIdentifierBytes) &&
           internal::valid_sha256(receipt.artifact_id) && receipt.artifact_generation > 0 &&
           valid_state(receipt.artifact_state) && internal::valid_sha256(receipt.artifact_content_digest) &&
           internal::valid_sha256(receipt.payload_digest) &&
           valid_text(receipt.media_type, kMaximumMediaTypeBytes) && receipt.service_sequence > 0 &&
           receipt.payload_digest == receipt.artifact_content_digest &&
           (!receipt.service_attestation ||
            valid_artifact_transfer_attestation(*receipt.service_attestation)) &&
           internal::artifact_publish_receipt_identity(receipt) == receipt.id;
}

bool valid_artifact_transfer_attestation(const ArtifactTransferAttestation& attestation) {
    return internal::valid_sha256(attestation.id) &&
           valid_text(attestation.service_id, kMaximumIdentifierBytes) &&
           valid_text(attestation.key_id, kMaximumIdentifierBytes) &&
           attestation.algorithm == ArtifactAuthenticationAlgorithm::HmacSha256 &&
           valid_operation(attestation.operation) && internal::valid_sha256(attestation.request_id) &&
           internal::valid_sha256(attestation.response_id) &&
           internal::valid_sha256(attestation.artifact_id) &&
           internal::valid_sha256(attestation.payload_digest) && attestation.service_sequence > 0 &&
           internal::valid_sha256(attestation.authentication_tag) &&
           internal::sha256(transfer_attestation_identity_message(attestation)) == attestation.id;
}

bool valid_verified_artifact_transfer(const VerifiedArtifactTransfer& transfer) {
    return internal::valid_sha256(transfer.id) && valid_operation(transfer.operation) &&
           internal::valid_sha256(transfer.request_id) && internal::valid_sha256(transfer.response_id) &&
           valid_text(transfer.service_id, kMaximumIdentifierBytes) &&
           internal::valid_sha256(transfer.memory_id) && internal::valid_sha256(transfer.artifact_id) &&
           transfer.artifact_generation > 0 && valid_state(transfer.artifact_state) &&
           internal::valid_sha256(transfer.artifact_content_digest) &&
           internal::valid_sha256(transfer.payload_digest) &&
           transfer.payload_digest == transfer.artifact_content_digest &&
           valid_text(transfer.media_type, kMaximumMediaTypeBytes) && transfer.service_sequence > 0 &&
           valid_authentication(transfer.authentication) &&
           ((transfer.authentication == ArtifactTransferAuthentication::None &&
             transfer.attestation_id.empty()) ||
            (transfer.authentication == ArtifactTransferAuthentication::HmacSha256 &&
             internal::valid_sha256(transfer.attestation_id))) &&
           internal::verified_artifact_transfer_identity(transfer) == transfer.id;
}

Result<ArtifactFetchRequest> prepare_artifact_fetch(const SafetyMemory& memory,
                                                    const std::string& artifact_id, std::string service_id,
                                                    std::uint64_t sequence, std::string media_type,
                                                    ArtifactTransferAuthentication response_authentication,
                                                    const RemoteArtifactOptions& options) {
    auto options_status = validate_options(options);
    if (!options_status)
        return options_status.error();
    if (sequence == 0 || !valid_text(service_id, kMaximumIdentifierBytes) ||
        !valid_text(media_type, kMaximumMediaTypeBytes) || !valid_authentication(response_authentication)) {
        return Result<ArtifactFetchRequest>::failure(StatusCode::InvalidArgument,
                                                     "artifact fetch request input is invalid");
    }
    auto artifact = current_artifact(memory, artifact_id, options.require_active_artifact);
    if (!artifact)
        return artifact.error();
    ArtifactFetchRequest result;
    result.sequence = sequence;
    result.service_id = std::move(service_id);
    result.memory_id = memory.identity();
    result.artifact_id = artifact.value().id;
    result.artifact_generation = artifact.value().generation;
    result.artifact_state = artifact.value().state;
    result.artifact_content_digest = artifact.value().content_digest;
    result.locator = artifact.value().locator;
    result.media_type = std::move(media_type);
    result.maximum_payload_bytes = static_cast<std::uint64_t>(options.maximum_payload_bytes);
    result.response_authentication = response_authentication;
    result.id = internal::artifact_fetch_request_identity(result);
    return result;
}

Result<ArtifactFetchResponse> make_artifact_fetch_response(const ArtifactFetchRequest& request,
                                                           std::span<const std::byte> payload,
                                                           std::uint64_t service_sequence) {
    if (!valid_artifact_fetch_request(request) || service_sequence == 0 ||
        payload.size() > request.maximum_payload_bytes) {
        return Result<ArtifactFetchResponse>::failure(StatusCode::InvalidArgument,
                                                      "artifact fetch response input is invalid");
    }
    const auto payload_digest = internal::sha256(payload);
    if (payload_digest != request.artifact_content_digest) {
        return Result<ArtifactFetchResponse>::failure(
            StatusCode::IdentityMismatch,
            "fetched payload does not match the requested artifact content digest", request.artifact_id);
    }
    ArtifactFetchResponse result;
    result.request_id = request.id;
    result.service_id = request.service_id;
    result.artifact_id = request.artifact_id;
    result.artifact_generation = request.artifact_generation;
    result.artifact_state = request.artifact_state;
    result.artifact_content_digest = request.artifact_content_digest;
    result.payload_digest = payload_digest;
    result.payload_bytes = static_cast<std::uint64_t>(payload.size());
    result.media_type = request.media_type;
    result.service_sequence = service_sequence;
    result.id = internal::artifact_fetch_response_identity(result);
    return result;
}

Result<ArtifactFetchResponse> authenticate_artifact_fetch_response(ArtifactFetchResponse response,
                                                                   std::string key_id,
                                                                   std::span<const std::byte> hmac_key) {
    if (!valid_artifact_fetch_response(response) || response.service_attestation ||
        !valid_text(key_id, kMaximumIdentifierBytes) || !valid_key(hmac_key)) {
        return Result<ArtifactFetchResponse>::failure(
            StatusCode::InvalidArgument, "artifact fetch response authentication input is invalid");
    }
    response.service_attestation = make_transfer_attestation(
        ArtifactTransferOperation::Fetch, response.request_id, response.id, response.service_id,
        response.artifact_id, response.payload_digest, response.payload_bytes, response.service_sequence,
        std::move(key_id), hmac_key);
    return response;
}

Result<VerifiedArtifactTransfer>
verify_artifact_fetch(const SafetyMemory& memory, const ArtifactFetchRequest& request,
                      const ArtifactFetchResponse& response, std::span<const std::byte> payload,
                      std::string_view expected_key_id, std::span<const std::byte> hmac_key,
                      const RemoteArtifactOptions& options) {
    if (!valid_artifact_fetch_request(request)) {
        return Result<VerifiedArtifactTransfer>::failure(StatusCode::InvalidArgument,
                                                         "artifact fetch request is invalid");
    }
    if (!valid_artifact_fetch_response(response)) {
        return Result<VerifiedArtifactTransfer>::failure(StatusCode::CorruptData,
                                                         "artifact fetch response is malformed");
    }
    auto artifact =
        verify_request_artifact(memory, request.memory_id, request.artifact_id, request.artifact_generation,
                                request.artifact_state, request.artifact_content_digest, options);
    if (!artifact)
        return artifact.error();
    if (payload.size() > options.maximum_payload_bytes || payload.size() > request.maximum_payload_bytes) {
        return Result<VerifiedArtifactTransfer>::failure(StatusCode::ResourceLimit,
                                                         "fetched artifact payload exceeds configured limit");
    }
    const auto digest = internal::sha256(payload);
    if (response.request_id != request.id || response.service_id != request.service_id ||
        response.artifact_id != request.artifact_id ||
        response.artifact_generation != request.artifact_generation ||
        response.artifact_state != request.artifact_state ||
        response.artifact_content_digest != request.artifact_content_digest ||
        response.payload_digest != digest || response.payload_digest != request.artifact_content_digest ||
        response.payload_bytes != static_cast<std::uint64_t>(payload.size()) ||
        response.media_type != request.media_type) {
        return Result<VerifiedArtifactTransfer>::failure(
            StatusCode::IdentityMismatch,
            "fetched artifact bytes or response metadata do not match the request", request.id);
    }
    auto attestation_id = verify_authentication(
        ArtifactTransferOperation::Fetch, request.id, response.id, request.service_id, request.artifact_id,
        response.payload_digest, response.payload_bytes, response.service_sequence,
        request.response_authentication, response.service_attestation, expected_key_id, hmac_key);
    if (!attestation_id)
        return attestation_id.error();
    return make_verified_transfer(
        ArtifactTransferOperation::Fetch, request.id, response.id, request.service_id, request.memory_id,
        artifact.value(), digest, static_cast<std::uint64_t>(payload.size()), request.media_type,
        response.service_sequence, request.response_authentication, std::move(attestation_id).value());
}

Result<ArtifactPublishRequest>
prepare_artifact_publish(const SafetyMemory& memory, const std::string& artifact_id,
                         std::span<const std::byte> payload, std::string service_id, std::uint64_t sequence,
                         std::string media_type, ArtifactTransferAuthentication receipt_authentication,
                         const RemoteArtifactOptions& options) {
    auto options_status = validate_options(options);
    if (!options_status)
        return options_status.error();
    if (sequence == 0 || !valid_text(service_id, kMaximumIdentifierBytes) ||
        !valid_text(media_type, kMaximumMediaTypeBytes) || !valid_authentication(receipt_authentication)) {
        return Result<ArtifactPublishRequest>::failure(StatusCode::InvalidArgument,
                                                       "artifact publish request input is invalid");
    }
    if (payload.size() > options.maximum_payload_bytes) {
        return Result<ArtifactPublishRequest>::failure(StatusCode::ResourceLimit,
                                                       "published artifact payload exceeds configured limit");
    }
    auto artifact = current_artifact(memory, artifact_id, options.require_active_artifact);
    if (!artifact)
        return artifact.error();
    const auto digest = internal::sha256(payload);
    if (digest != artifact.value().content_digest) {
        return Result<ArtifactPublishRequest>::failure(
            StatusCode::IdentityMismatch,
            "published bytes do not match the registered artifact content digest", artifact_id);
    }
    ArtifactPublishRequest result;
    result.sequence = sequence;
    result.service_id = std::move(service_id);
    result.memory_id = memory.identity();
    result.artifact_id = artifact.value().id;
    result.artifact_generation = artifact.value().generation;
    result.artifact_state = artifact.value().state;
    result.artifact_content_digest = artifact.value().content_digest;
    result.locator = artifact.value().locator;
    result.payload_digest = digest;
    result.payload_bytes = static_cast<std::uint64_t>(payload.size());
    result.media_type = std::move(media_type);
    result.receipt_authentication = receipt_authentication;
    result.id = internal::artifact_publish_request_identity(result);
    return result;
}

Result<ArtifactPublishReceipt> make_artifact_publish_receipt(const ArtifactPublishRequest& request,
                                                             std::uint64_t service_sequence) {
    if (!valid_artifact_publish_request(request) || service_sequence == 0) {
        return Result<ArtifactPublishReceipt>::failure(StatusCode::InvalidArgument,
                                                       "artifact publish receipt input is invalid");
    }
    ArtifactPublishReceipt result;
    result.request_id = request.id;
    result.service_id = request.service_id;
    result.artifact_id = request.artifact_id;
    result.artifact_generation = request.artifact_generation;
    result.artifact_state = request.artifact_state;
    result.artifact_content_digest = request.artifact_content_digest;
    result.payload_digest = request.payload_digest;
    result.payload_bytes = request.payload_bytes;
    result.media_type = request.media_type;
    result.service_sequence = service_sequence;
    result.id = internal::artifact_publish_receipt_identity(result);
    return result;
}

Result<ArtifactPublishReceipt> authenticate_artifact_publish_receipt(ArtifactPublishReceipt receipt,
                                                                     std::string key_id,
                                                                     std::span<const std::byte> hmac_key) {
    if (!valid_artifact_publish_receipt(receipt) || receipt.service_attestation ||
        !valid_text(key_id, kMaximumIdentifierBytes) || !valid_key(hmac_key)) {
        return Result<ArtifactPublishReceipt>::failure(
            StatusCode::InvalidArgument, "artifact publish receipt authentication input is invalid");
    }
    receipt.service_attestation = make_transfer_attestation(
        ArtifactTransferOperation::Publish, receipt.request_id, receipt.id, receipt.service_id,
        receipt.artifact_id, receipt.payload_digest, receipt.payload_bytes, receipt.service_sequence,
        std::move(key_id), hmac_key);
    return receipt;
}

Result<VerifiedArtifactTransfer>
verify_artifact_publish(const SafetyMemory& memory, const ArtifactPublishRequest& request,
                        const ArtifactPublishReceipt& receipt, std::span<const std::byte> payload,
                        std::string_view expected_key_id, std::span<const std::byte> hmac_key,
                        const RemoteArtifactOptions& options) {
    if (!valid_artifact_publish_request(request)) {
        return Result<VerifiedArtifactTransfer>::failure(StatusCode::InvalidArgument,
                                                         "artifact publish request is invalid");
    }
    if (!valid_artifact_publish_receipt(receipt)) {
        return Result<VerifiedArtifactTransfer>::failure(StatusCode::CorruptData,
                                                         "artifact publish receipt is malformed");
    }
    auto artifact =
        verify_request_artifact(memory, request.memory_id, request.artifact_id, request.artifact_generation,
                                request.artifact_state, request.artifact_content_digest, options);
    if (!artifact)
        return artifact.error();
    if (payload.size() > options.maximum_payload_bytes) {
        return Result<VerifiedArtifactTransfer>::failure(
            StatusCode::ResourceLimit, "published artifact payload exceeds configured limit");
    }
    const auto digest = internal::sha256(payload);
    if (request.payload_digest != digest ||
        request.payload_bytes != static_cast<std::uint64_t>(payload.size()) ||
        receipt.request_id != request.id || receipt.service_id != request.service_id ||
        receipt.artifact_id != request.artifact_id ||
        receipt.artifact_generation != request.artifact_generation ||
        receipt.artifact_state != request.artifact_state ||
        receipt.artifact_content_digest != request.artifact_content_digest ||
        receipt.payload_digest != request.payload_digest || receipt.payload_bytes != request.payload_bytes ||
        receipt.media_type != request.media_type) {
        return Result<VerifiedArtifactTransfer>::failure(
            StatusCode::IdentityMismatch,
            "published artifact bytes or receipt metadata do not match the request", request.id);
    }
    auto attestation_id = verify_authentication(
        ArtifactTransferOperation::Publish, request.id, receipt.id, request.service_id, request.artifact_id,
        receipt.payload_digest, receipt.payload_bytes, receipt.service_sequence,
        request.receipt_authentication, receipt.service_attestation, expected_key_id, hmac_key);
    if (!attestation_id)
        return attestation_id.error();
    return make_verified_transfer(
        ArtifactTransferOperation::Publish, request.id, receipt.id, request.service_id, request.memory_id,
        artifact.value(), digest, static_cast<std::uint64_t>(payload.size()), request.media_type,
        receipt.service_sequence, request.receipt_authentication, std::move(attestation_id).value());
}

std::string ArtifactTransferJournal::identity() const {
    if (records_.empty())
        return internal::sha256("rbfsafe-artifact-transfer-journal-v1\nempty");
    return current_record_id_;
}

bool ArtifactTransferJournal::valid() const {
    if ((records_.empty() && !current_record_id_.empty()) ||
        (!records_.empty() && current_record_id_ != records_.back().id)) {
        return false;
    }
    std::string parent;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        const auto& record = records_[index];
        if (record.sequence != index + 1 || record.parent_id != parent ||
            !internal::valid_sha256(record.id) || !valid_verified_artifact_transfer(record.transfer) ||
            internal::artifact_transfer_record_identity(record) != record.id) {
            return false;
        }
        parent = record.id;
    }
    return true;
}

Result<ArtifactTransferRecord> ArtifactTransferJournal::append(VerifiedArtifactTransfer transfer,
                                                               const std::string& expected_current_record_id,
                                                               std::size_t maximum_records) {
    if (!valid() || !valid_verified_artifact_transfer(transfer) || maximum_records == 0) {
        return Result<ArtifactTransferRecord>::failure(StatusCode::InvalidArgument,
                                                       "artifact transfer journal append input is invalid");
    }
    if (expected_current_record_id != current_record_id_) {
        return Result<ArtifactTransferRecord>::failure(
            StatusCode::IdentityMismatch, "artifact transfer journal head does not match expected head",
            current_record_id_);
    }
    if (records_.size() >= maximum_records ||
        records_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
        return Result<ArtifactTransferRecord>::failure(StatusCode::ResourceLimit,
                                                       "artifact transfer journal record limit reached");
    }
    ArtifactTransferRecord record;
    record.sequence = static_cast<std::uint64_t>(records_.size() + 1);
    record.parent_id = current_record_id_;
    record.transfer = std::move(transfer);
    record.id = internal::artifact_transfer_record_identity(record);
    records_.push_back(record);
    current_record_id_ = record.id;
    return record;
}

Result<void> ArtifactTransferJournal::save(const std::filesystem::path& directory,
                                           const SaveOptions& options) const {
    return save_artifact_transfer_journal(*this, directory, options);
}

Result<ArtifactTransferJournal>
ArtifactTransferJournal::load(const std::filesystem::path& directory,
                              const ArtifactTransferJournalLoadOptions& options) {
    return load_artifact_transfer_journal(directory, options);
}

std::string artifact_transfer_operation_name(ArtifactTransferOperation operation) {
    switch (operation) {
    case ArtifactTransferOperation::Fetch:
        return "fetch";
    case ArtifactTransferOperation::Publish:
        return "publish";
    }
    return "unknown";
}

std::string artifact_transfer_authentication_name(ArtifactTransferAuthentication authentication) {
    switch (authentication) {
    case ArtifactTransferAuthentication::None:
        return "none";
    case ArtifactTransferAuthentication::HmacSha256:
        return "hmac_sha256";
    }
    return "unknown";
}

} // namespace rbfsafe
