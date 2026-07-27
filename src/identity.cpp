#include <rbfsafe/identity.h>

#include "internal/certificate_utils.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/remote.h"
#include "internal/sha256.h"

#include "monocypher.h"
#include "optional/monocypher-ed25519.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 256;

bool valid_text(std::string_view value, std::size_t maximum_bytes) {
    return !value.empty() && value.size() <= maximum_bytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_key_state(ServiceKeyState state) {
    return state >= ServiceKeyState::Pending && state <= ServiceKeyState::Revoked;
}

internal::Json key_material_json(const ServicePublicKey& key) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(key.algorithm)},
        {"format", "rbfsafe-service-public-key"},
        {"public_key", internal::encode_hex(key.public_key)},
        {"schema", 1},
        {"service_id", key.service_id},
    };
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

bool same_immutable_key_policy(const ServicePublicKey& previous, const ServicePublicKey& next) {
    return previous.id == next.id && previous.service_id == next.service_id &&
           previous.algorithm == next.algorithm && previous.public_key == next.public_key &&
           previous.valid_from_sequence == next.valid_from_sequence &&
           previous.allow_fetch == next.allow_fetch && previous.allow_publish == next.allow_publish;
}

bool valid_state_transition(ServiceKeyState previous, ServiceKeyState next) {
    switch (previous) {
    case ServiceKeyState::Pending:
        return next == ServiceKeyState::Pending || next == ServiceKeyState::Active ||
               next == ServiceKeyState::Revoked;
    case ServiceKeyState::Active:
        return next == ServiceKeyState::Active || next == ServiceKeyState::Retired ||
               next == ServiceKeyState::Revoked;
    case ServiceKeyState::Retired:
        return next == ServiceKeyState::Retired || next == ServiceKeyState::Revoked;
    case ServiceKeyState::Revoked:
        return next == ServiceKeyState::Revoked;
    }
    return false;
}

Result<void> validate_rotation(const ServicePublicKey& previous, const ServicePublicKey& next) {
    if (!same_immutable_key_policy(previous, next) || !valid_state_transition(previous.state, next.state)) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "service trust-bundle key policy or state transition is invalid",
                                     previous.id);
    }
    if (previous.valid_through_sequence != 0 &&
        next.valid_through_sequence != previous.valid_through_sequence) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "service key validity cannot change after its upper bound is fixed",
                                     previous.id);
    }
    if (previous.valid_through_sequence == 0 && next.valid_through_sequence != 0 &&
        next.valid_through_sequence < next.valid_from_sequence) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "service key rotation introduced an invalid sequence window",
                                     previous.id);
    }
    if (next.state == ServiceKeyState::Retired && next.valid_through_sequence == 0) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "retired service keys require a finite validity window", previous.id);
    }
    return Result<void>::success();
}

Result<void> validate_signing_key(const std::string& service_id, const std::string& key_id,
                                  std::span<const std::byte> secret_key) {
    if (!internal::valid_sha256(key_id) || secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "Ed25519 transfer signing key or key ID is invalid");
    }
    std::array<std::uint8_t, kEd25519SeedBytes> mutable_seed{};
    std::array<std::uint8_t, kEd25519SecretKeyBytes> derived_secret{};
    std::array<std::uint8_t, kEd25519PublicKeyBytes> derived_public{};
    std::memcpy(mutable_seed.data(), secret_key.data(), mutable_seed.size());
    crypto_ed25519_key_pair(derived_secret.data(), derived_public.data(), mutable_seed.data());
    const auto supplied_public = secret_key.subspan(kEd25519SeedBytes, kEd25519PublicKeyBytes);
    const bool consistent =
        crypto_verify32(derived_public.data(),
                        reinterpret_cast<const std::uint8_t*>(supplied_public.data())) == 0;
    auto derived = make_service_public_key(service_id, std::as_bytes(std::span(derived_public)));
    crypto_wipe(derived_secret.data(), derived_secret.size());
    crypto_wipe(derived_public.data(), derived_public.size());
    if (!consistent) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "Ed25519 secret key seed and public half do not match", key_id);
    }
    if (!derived)
        return derived.error();
    if (derived.value().id != key_id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "Ed25519 secret key does not match the service key ID", key_id);
    }
    return Result<void>::success();
}

Result<void> verify_public_attestation(const ArtifactTransferAttestation& attestation,
                                       ArtifactTransferOperation operation, const std::string& request_id,
                                       const std::string& response_id, const std::string& service_id,
                                       const std::string& artifact_id, const std::string& payload_digest,
                                       std::uint64_t payload_bytes, std::uint64_t service_sequence,
                                       const ServiceTrustBundle& trust_bundle) {
    auto binding = internal::validate_artifact_transfer_attestation_binding(
        attestation, ArtifactAuthenticationAlgorithm::Ed25519, operation, request_id, response_id, service_id,
        artifact_id, payload_digest, payload_bytes, service_sequence);
    if (!binding)
        return binding;
    auto trusted =
        trusted_service_public_key(trust_bundle, service_id, attestation.key_id, operation, service_sequence);
    if (!trusted)
        return trusted.error();
    auto signature = internal::decode_hex(attestation.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::artifact_transfer_authentication_message(attestation);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          trusted.value().public_key);
}

} // namespace

namespace internal {

std::string encode_hex(std::span<const std::byte> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto value = std::to_integer<unsigned int>(bytes[index]);
        result[index * 2] = digits[value >> 4U];
        result[index * 2 + 1] = digits[value & 0x0fU];
    }
    return result;
}

Result<std::vector<std::byte>> decode_hex(const std::string& text, std::size_t expected_bytes) {
    if (text.size() != expected_bytes * 2) {
        return Result<std::vector<std::byte>>::failure(StatusCode::CorruptData,
                                                       "hexadecimal byte string has invalid length");
    }
    auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9')
            return character - '0';
        if (character >= 'a' && character <= 'f')
            return character - 'a' + 10;
        return -1;
    };
    std::vector<std::byte> result(expected_bytes);
    for (std::size_t index = 0; index < expected_bytes; ++index) {
        const int high = nibble(text[index * 2]);
        const int low = nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return Result<std::vector<std::byte>>::failure(
                StatusCode::CorruptData, "hexadecimal byte string contains an invalid character");
        }
        result[index] = static_cast<std::byte>((high << 4) | low);
    }
    return result;
}

std::string service_public_key_identity(const ServicePublicKey& key) {
    return sha256(std::string("rbfsafe-service-public-key-v1\n") + key_material_json(key).dump(false));
}

std::string service_trust_bundle_identity(const ServiceTrustBundle& bundle) {
    Json::Array keys;
    keys.reserve(bundle.keys().size());
    for (const auto& key : bundle.keys())
        keys.emplace_back(key_json(key));
    return sha256(Json(Json::Object{
                           {"format", "rbfsafe-service-trust-bundle"},
                           {"keys", std::move(keys)},
                           {"parent_id", bundle.parent_id()},
                           {"schema", 1},
                           {"sequence", std::to_string(bundle.sequence())},
                       })
                      .dump(false));
}

} // namespace internal

Result<Ed25519KeyPair> ed25519_key_pair_from_seed(std::span<const std::byte> seed) {
    if (seed.size() != kEd25519SeedBytes) {
        return Result<Ed25519KeyPair>::failure(StatusCode::InvalidArgument,
                                               "Ed25519 seed must contain exactly 32 bytes");
    }
    std::array<std::uint8_t, kEd25519SeedBytes> mutable_seed{};
    std::memcpy(mutable_seed.data(), seed.data(), seed.size());
    std::array<std::uint8_t, kEd25519SecretKeyBytes> secret_key{};
    std::array<std::uint8_t, kEd25519PublicKeyBytes> public_key{};
    crypto_ed25519_key_pair(secret_key.data(), public_key.data(), mutable_seed.data());

    Ed25519KeyPair result;
    std::memcpy(result.public_key.data(), public_key.data(), public_key.size());
    std::memcpy(result.secret_key.data(), secret_key.data(), secret_key.size());
    crypto_wipe(secret_key.data(), secret_key.size());
    crypto_wipe(public_key.data(), public_key.size());
    return result;
}

Result<std::array<std::byte, kEd25519SignatureBytes>> ed25519_sign(std::span<const std::byte> message,
                                                                   std::span<const std::byte> secret_key) {
    if (secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<std::array<std::byte, kEd25519SignatureBytes>>::failure(
            StatusCode::InvalidArgument, "Ed25519 secret key must contain exactly 64 bytes");
    }
    std::array<std::byte, kEd25519SignatureBytes> signature{};
    crypto_ed25519_sign(reinterpret_cast<std::uint8_t*>(signature.data()),
                        reinterpret_cast<const std::uint8_t*>(secret_key.data()),
                        reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
    return signature;
}

Result<void> ed25519_verify(std::span<const std::byte> message, std::span<const std::byte> signature,
                            std::span<const std::byte> public_key) {
    if (signature.size() != kEd25519SignatureBytes || public_key.size() != kEd25519PublicKeyBytes) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "Ed25519 signature or public key has invalid length");
    }
    const int verified =
        crypto_ed25519_check(reinterpret_cast<const std::uint8_t*>(signature.data()),
                             reinterpret_cast<const std::uint8_t*>(public_key.data()),
                             reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
    if (verified != 0) {
        return Result<void>::failure(StatusCode::IdentityMismatch, "Ed25519 signature verification failed");
    }
    return Result<void>::success();
}

bool valid_service_public_key(const ServicePublicKey& key) {
    const bool any_public_key_byte = std::any_of(key.public_key.begin(), key.public_key.end(),
                                                 [](std::byte value) { return value != std::byte{0}; });
    return internal::valid_sha256(key.id) && valid_text(key.service_id, kMaximumIdentifierBytes) &&
           key.algorithm == ArtifactAuthenticationAlgorithm::Ed25519 && any_public_key_byte &&
           key.valid_from_sequence > 0 &&
           (key.valid_through_sequence == 0 || key.valid_through_sequence >= key.valid_from_sequence) &&
           valid_key_state(key.state) && (key.allow_fetch || key.allow_publish) &&
           (key.state != ServiceKeyState::Retired || key.valid_through_sequence != 0) &&
           internal::service_public_key_identity(key) == key.id;
}

Result<ServicePublicKey> make_service_public_key(std::string service_id,
                                                 std::span<const std::byte> public_key,
                                                 std::uint64_t valid_from_sequence,
                                                 std::uint64_t valid_through_sequence, ServiceKeyState state,
                                                 bool allow_fetch, bool allow_publish) {
    if (!valid_text(service_id, kMaximumIdentifierBytes) || public_key.size() != kEd25519PublicKeyBytes ||
        valid_from_sequence == 0 ||
        (valid_through_sequence != 0 && valid_through_sequence < valid_from_sequence) ||
        !valid_key_state(state) || (!allow_fetch && !allow_publish) ||
        (state == ServiceKeyState::Retired && valid_through_sequence == 0)) {
        return Result<ServicePublicKey>::failure(StatusCode::InvalidArgument,
                                                 "service public-key input is invalid");
    }
    ServicePublicKey result;
    result.service_id = std::move(service_id);
    std::copy(public_key.begin(), public_key.end(), result.public_key.begin());
    result.valid_from_sequence = valid_from_sequence;
    result.valid_through_sequence = valid_through_sequence;
    result.state = state;
    result.allow_fetch = allow_fetch;
    result.allow_publish = allow_publish;
    result.id = internal::service_public_key_identity(result);
    if (!valid_service_public_key(result)) {
        return Result<ServicePublicKey>::failure(StatusCode::InvalidArgument,
                                                 "service public key is invalid");
    }
    return result;
}

Result<ServiceTrustBundle> ServiceTrustBundle::create(std::uint64_t sequence, std::string parent_id,
                                                      std::vector<ServicePublicKey> keys) {
    if (sequence == 0 || keys.empty() || (sequence == 1 && !parent_id.empty()) ||
        (sequence > 1 && !internal::valid_sha256(parent_id))) {
        return Result<ServiceTrustBundle>::failure(StatusCode::InvalidArgument,
                                                   "service trust-bundle input is invalid");
    }
    for (const auto& key : keys) {
        if (!valid_service_public_key(key)) {
            return Result<ServiceTrustBundle>::failure(
                StatusCode::InvalidArgument, "service trust bundle contains an invalid key", key.id);
        }
    }
    std::sort(keys.begin(), keys.end(), [](const ServicePublicKey& left, const ServicePublicKey& right) {
        if (left.service_id != right.service_id)
            return left.service_id < right.service_id;
        return left.id < right.id;
    });
    for (std::size_t index = 1; index < keys.size(); ++index) {
        if (keys[index - 1].service_id == keys[index].service_id && keys[index - 1].id == keys[index].id) {
            return Result<ServiceTrustBundle>::failure(
                StatusCode::InvalidArgument, "service trust bundle contains a duplicate key", keys[index].id);
        }
    }
    ServiceTrustBundle result;
    result.sequence_ = sequence;
    result.parent_id_ = std::move(parent_id);
    result.keys_ = std::move(keys);
    result.id_ = internal::service_trust_bundle_identity(result);
    return result;
}

bool ServiceTrustBundle::valid() const {
    if (sequence_ == 0 || keys_.empty() || (sequence_ == 1 && !parent_id_.empty()) ||
        (sequence_ > 1 && !internal::valid_sha256(parent_id_)) || !internal::valid_sha256(id_)) {
        return false;
    }
    for (std::size_t index = 0; index < keys_.size(); ++index) {
        if (!valid_service_public_key(keys_[index]))
            return false;
        if (index > 0) {
            const auto& previous = keys_[index - 1];
            const auto& current = keys_[index];
            if (previous.service_id > current.service_id ||
                (previous.service_id == current.service_id && previous.id >= current.id)) {
                return false;
            }
        }
    }
    return internal::service_trust_bundle_identity(*this) == id_;
}

Result<std::optional<ServicePublicKey>> ServiceTrustBundle::key(const std::string& service_id,
                                                                const std::string& key_id) const {
    if (!valid() || !valid_text(service_id, kMaximumIdentifierBytes) || !internal::valid_sha256(key_id)) {
        return Result<std::optional<ServicePublicKey>>::failure(
            StatusCode::InvalidArgument, "service trust-bundle lookup input is invalid");
    }
    const auto found = std::find_if(keys_.begin(), keys_.end(), [&](const ServicePublicKey& candidate) {
        return candidate.service_id == service_id && candidate.id == key_id;
    });
    if (found == keys_.end())
        return std::optional<ServicePublicKey>{};
    return std::optional<ServicePublicKey>{*found};
}

Result<ServiceTrustBundle> rotate_service_trust_bundle(const ServiceTrustBundle& previous,
                                                       std::vector<ServicePublicKey> keys) {
    if (!previous.valid() || previous.sequence() == std::numeric_limits<std::uint64_t>::max()) {
        return Result<ServiceTrustBundle>::failure(StatusCode::InvalidArgument,
                                                   "previous service trust bundle is invalid or exhausted");
    }
    auto next = ServiceTrustBundle::create(previous.sequence() + 1, previous.id(), std::move(keys));
    if (!next)
        return next.error();
    for (const auto& old_key : previous.keys()) {
        auto replacement = next.value().key(old_key.service_id, old_key.id);
        if (!replacement)
            return replacement.error();
        if (!replacement.value()) {
            return Result<ServiceTrustBundle>::failure(
                StatusCode::IdentityMismatch, "service trust-bundle rotation removed an existing key",
                old_key.id);
        }
        auto transition = validate_rotation(old_key, *replacement.value());
        if (!transition)
            return transition.error();
    }
    return next;
}

Result<ServicePublicKey> trusted_service_public_key(const ServiceTrustBundle& bundle,
                                                    const std::string& service_id, const std::string& key_id,
                                                    ArtifactTransferOperation operation,
                                                    std::uint64_t service_sequence) {
    if (!bundle.valid() || service_sequence == 0 ||
        (operation != ArtifactTransferOperation::Fetch && operation != ArtifactTransferOperation::Publish)) {
        return Result<ServicePublicKey>::failure(StatusCode::InvalidArgument,
                                                 "trusted service-key lookup input is invalid");
    }
    auto found = bundle.key(service_id, key_id);
    if (!found)
        return found.error();
    if (!found.value()) {
        return Result<ServicePublicKey>::failure(
            StatusCode::IdentityMismatch, "service key is absent from the pinned trust bundle", key_id);
    }
    const auto& key = *found.value();
    const bool state_trusted = key.state == ServiceKeyState::Active || key.state == ServiceKeyState::Retired;
    const bool sequence_trusted =
        service_sequence >= key.valid_from_sequence &&
        (key.valid_through_sequence == 0 || service_sequence <= key.valid_through_sequence);
    const bool operation_trusted = (operation == ArtifactTransferOperation::Fetch && key.allow_fetch) ||
                                   (operation == ArtifactTransferOperation::Publish && key.allow_publish);
    if (!state_trusted || !sequence_trusted || !operation_trusted) {
        return Result<ServicePublicKey>::failure(StatusCode::IdentityMismatch,
                                                 "service key is not trusted for this operation and sequence",
                                                 key.id);
    }
    return key;
}

Result<ArtifactFetchResponse> sign_artifact_fetch_response(ArtifactFetchResponse response, std::string key_id,
                                                           std::span<const std::byte> ed25519_secret_key) {
    if (!valid_artifact_fetch_response(response) || response.service_attestation) {
        return Result<ArtifactFetchResponse>::failure(StatusCode::InvalidArgument,
                                                      "artifact fetch response cannot be signed");
    }
    auto key = validate_signing_key(response.service_id, key_id, ed25519_secret_key);
    if (!key)
        return key.error();
    auto attestation = internal::make_artifact_transfer_attestation(
        ArtifactAuthenticationAlgorithm::Ed25519, ArtifactTransferOperation::Fetch, response.request_id,
        response.id, response.service_id, response.artifact_id, response.payload_digest,
        response.payload_bytes, response.service_sequence, std::move(key_id));
    const auto message = internal::artifact_transfer_authentication_message(attestation);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    attestation.authentication_tag = internal::encode_hex(signature.value());
    if (!valid_artifact_transfer_attestation(attestation)) {
        return Result<ArtifactFetchResponse>::failure(StatusCode::InternalError,
                                                      "generated Ed25519 fetch attestation is invalid");
    }
    response.service_attestation = std::move(attestation);
    return response;
}

Result<ArtifactPublishReceipt> sign_artifact_publish_receipt(ArtifactPublishReceipt receipt,
                                                             std::string key_id,
                                                             std::span<const std::byte> ed25519_secret_key) {
    if (!valid_artifact_publish_receipt(receipt) || receipt.service_attestation) {
        return Result<ArtifactPublishReceipt>::failure(StatusCode::InvalidArgument,
                                                       "artifact publish receipt cannot be signed");
    }
    auto key = validate_signing_key(receipt.service_id, key_id, ed25519_secret_key);
    if (!key)
        return key.error();
    auto attestation = internal::make_artifact_transfer_attestation(
        ArtifactAuthenticationAlgorithm::Ed25519, ArtifactTransferOperation::Publish, receipt.request_id,
        receipt.id, receipt.service_id, receipt.artifact_id, receipt.payload_digest, receipt.payload_bytes,
        receipt.service_sequence, std::move(key_id));
    const auto message = internal::artifact_transfer_authentication_message(attestation);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    attestation.authentication_tag = internal::encode_hex(signature.value());
    if (!valid_artifact_transfer_attestation(attestation)) {
        return Result<ArtifactPublishReceipt>::failure(
            StatusCode::InternalError, "generated Ed25519 publication attestation is invalid");
    }
    receipt.service_attestation = std::move(attestation);
    return receipt;
}

Result<VerifiedArtifactTransfer>
verify_artifact_fetch_offline(const SafetyMemory& memory, const ArtifactFetchRequest& request,
                              const ArtifactFetchResponse& response, std::span<const std::byte> payload,
                              const ServiceTrustBundle& trust_bundle, const RemoteArtifactOptions& options) {
    if (!valid_artifact_fetch_request(request) ||
        request.response_authentication != ArtifactTransferAuthentication::Ed25519) {
        return Result<VerifiedArtifactTransfer>::failure(
            StatusCode::InvalidArgument, "offline artifact fetch request is not an Ed25519 request");
    }
    if (!valid_artifact_fetch_response(response)) {
        return Result<VerifiedArtifactTransfer>::failure(StatusCode::CorruptData,
                                                         "offline artifact fetch response is malformed");
    }
    if (!response.service_attestation) {
        return Result<VerifiedArtifactTransfer>::failure(
            StatusCode::IdentityMismatch, "offline artifact fetch response is missing its signature");
    }
    auto verified = verify_public_attestation(
        *response.service_attestation, ArtifactTransferOperation::Fetch, request.id, response.id,
        request.service_id, request.artifact_id, response.payload_digest, response.payload_bytes,
        response.service_sequence, trust_bundle);
    if (!verified)
        return verified.error();
    return internal::finalize_verified_artifact_fetch(
        memory, request, response, payload, ArtifactTransferAuthentication::Ed25519,
        response.service_attestation->id, response.service_attestation->key_id, trust_bundle.id(), options);
}

Result<VerifiedArtifactTransfer> verify_artifact_publish_offline(const SafetyMemory& memory,
                                                                 const ArtifactPublishRequest& request,
                                                                 const ArtifactPublishReceipt& receipt,
                                                                 std::span<const std::byte> payload,
                                                                 const ServiceTrustBundle& trust_bundle,
                                                                 const RemoteArtifactOptions& options) {
    if (!valid_artifact_publish_request(request) ||
        request.receipt_authentication != ArtifactTransferAuthentication::Ed25519) {
        return Result<VerifiedArtifactTransfer>::failure(
            StatusCode::InvalidArgument, "offline artifact publication request is not an Ed25519 request");
    }
    if (!valid_artifact_publish_receipt(receipt)) {
        return Result<VerifiedArtifactTransfer>::failure(StatusCode::CorruptData,
                                                         "offline artifact publication receipt is malformed");
    }
    if (!receipt.service_attestation) {
        return Result<VerifiedArtifactTransfer>::failure(
            StatusCode::IdentityMismatch, "offline artifact publication receipt is missing its signature");
    }
    auto verified = verify_public_attestation(*receipt.service_attestation,
                                              ArtifactTransferOperation::Publish, request.id, receipt.id,
                                              request.service_id, request.artifact_id, receipt.payload_digest,
                                              receipt.payload_bytes, receipt.service_sequence, trust_bundle);
    if (!verified)
        return verified.error();
    return internal::finalize_verified_artifact_publish(
        memory, request, receipt, payload, ArtifactTransferAuthentication::Ed25519,
        receipt.service_attestation->id, receipt.service_attestation->key_id, trust_bundle.id(), options);
}

Result<void> ServiceTrustBundle::save(const std::filesystem::path& path, const SaveOptions& options) const {
    return save_service_trust_bundle(*this, path, options);
}

Result<ServiceTrustBundle> ServiceTrustBundle::load(const std::filesystem::path& path,
                                                    const ServiceTrustBundleLoadOptions& options) {
    return load_service_trust_bundle(path, options);
}

std::string service_key_state_name(ServiceKeyState state) {
    switch (state) {
    case ServiceKeyState::Pending:
        return "pending";
    case ServiceKeyState::Active:
        return "active";
    case ServiceKeyState::Retired:
        return "retired";
    case ServiceKeyState::Revoked:
        return "revoked";
    }
    return "unknown";
}

} // namespace rbfsafe
