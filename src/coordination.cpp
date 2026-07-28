#include <rbfsafe/coordination.h>

#include "internal/binary.h"
#include "internal/certificate_utils.h"
#include "internal/coordination.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::uint32_t kStorageSchema = 1;

bool valid_text(std::string_view value) {
    return !value.empty() && value.size() <= kMaximumIdentifierBytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_signature(std::string_view value) {
    return value.size() == kEd25519SignatureBytes * 2 &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
           });
}

bool valid_parent(std::uint64_t sequence, const std::string& parent_id) {
    return sequence == 1 ? parent_id.empty() : internal::valid_sha256(parent_id);
}

bool bundle_covers_window(const ContinuousFleetOccupancyBundle& bundle, std::uint64_t valid_from_tick,
                          std::uint64_t valid_through_tick) {
    if (!bundle.valid() || valid_from_tick > valid_through_tick)
        return false;
    return std::all_of(bundle.occupancies().begin(), bundle.occupancies().end(),
                       [valid_from_tick, valid_through_tick](const auto& occupancy) {
                           return !occupancy.trajectory.empty() &&
                                  occupancy.trajectory.front().tick <= valid_from_tick &&
                                  occupancy.trajectory.back().tick >= valid_through_tick;
                       });
}

Result<std::vector<std::byte>>
read_occupancy_payload(const std::filesystem::path& path,
                       const ContinuousFleetOccupancyBundleLoadOptions& options) {
    if (path.empty() || options.maximum_payload_bytes == 0) {
        return Result<std::vector<std::byte>>::failure(
            StatusCode::InvalidArgument, "occupancy publication payload path or limits are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<std::vector<std::byte>>::failure(StatusCode::Cancelled,
                                                       "occupancy publication operation was cancelled");
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || status.type() == std::filesystem::file_type::not_found) {
        return Result<std::vector<std::byte>>::failure(
            StatusCode::IoError, "failed to inspect occupancy publication payload", path.string());
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<std::vector<std::byte>>::failure(
            StatusCode::CorruptData, "occupancy publication payload is indirect or is not a regular file",
            path.string());
    }
    return internal::read_binary_file(path, options.maximum_payload_bytes);
}

Result<ContinuousFleetOccupancyBundle>
load_exact_occupancy_payload(std::span<const std::byte> payload,
                             const ContinuousFleetOccupancyBundleLoadOptions& options) {
    auto loaded = load_continuous_fleet_occupancy_bundle(payload, options);
    if (!loaded)
        return loaded.error();
    if (!loaded.value().valid()) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::CorruptData, "occupancy publication payload contains an invalid bundle");
    }
    return loaded;
}

internal::Json publication_identity_json(const OccupancyPublication& publication) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(publication.algorithm)},
        {"occupancy_bundle_id", publication.occupancy_bundle_id},
        {"parent_publication_id", publication.parent_publication_id},
        {"payload_bytes", std::to_string(publication.payload_bytes)},
        {"payload_digest", publication.payload_digest},
        {"publisher_key_id", publication.publisher_key_id},
        {"publisher_sequence", std::to_string(publication.publisher_sequence)},
        {"publisher_service_id", publication.publisher_service_id},
        {"storage_schema", static_cast<int>(publication.storage_schema)},
        {"stream_id", publication.stream_id},
        {"timeline_id", publication.timeline_id},
        {"trust_bundle_id", publication.trust_bundle_id},
        {"valid_from_tick", std::to_string(publication.valid_from_tick)},
        {"valid_through_tick", std::to_string(publication.valid_through_tick)},
        {"workspace_frame_id", publication.workspace_frame_id},
    };
}

internal::Json verified_identity_json(const VerifiedOccupancyPublication& verification) {
    return internal::Json::Object{
        {"evaluation_tick", std::to_string(verification.evaluation_tick)},
        {"occupancy_bundle_id", verification.occupancy_bundle_id},
        {"payload_bytes", std::to_string(verification.payload_bytes)},
        {"payload_digest", verification.payload_digest},
        {"publication_id", verification.publication_id},
        {"publisher_key_id", verification.publisher_key_id},
        {"publisher_sequence", std::to_string(verification.publisher_sequence)},
        {"publisher_service_id", verification.publisher_service_id},
        {"stream_id", verification.stream_id},
        {"timeline_id", verification.timeline_id},
        {"trust_bundle_id", verification.trust_bundle_id},
        {"valid_from_tick", std::to_string(verification.valid_from_tick)},
        {"valid_through_tick", std::to_string(verification.valid_through_tick)},
        {"workspace_frame_id", verification.workspace_frame_id},
    };
}

Result<void> verify_payload_binding(const ContinuousFleetOccupancyBundle& bundle,
                                    const OccupancyPublication& publication,
                                    std::span<const std::byte> payload) {
    if (payload.empty() ||
        payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "occupancy publication payload size is not representable");
    }
    if (publication.payload_bytes != static_cast<std::uint64_t>(payload.size()) ||
        publication.payload_digest != internal::sha256(payload)) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "occupancy publication is bound to different payload bytes",
                                     publication.id);
    }
    if (publication.occupancy_bundle_id != bundle.id() ||
        publication.timeline_id != bundle.report().timeline_id ||
        publication.workspace_frame_id != bundle.report().workspace_frame_id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "occupancy publication metadata does not match the payload bundle",
                                     publication.id);
    }
    if (!bundle_covers_window(bundle, publication.valid_from_tick, publication.valid_through_tick)) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "occupancy publication validity window is not covered by every deployment trajectory",
            publication.id);
    }
    return Result<void>::success();
}

} // namespace

namespace internal {

std::string occupancy_publication_identity(const OccupancyPublication& publication) {
    return sha256(publication_identity_json(publication).dump(false));
}

std::string occupancy_publication_signature_message(const OccupancyPublication& publication) {
    return Json(Json::Object{
                    {"domain", "rbfsafe-continuous-fleet-occupancy-publication-signature-v1"},
                    {"publication_id", publication.id},
                })
        .dump(false);
}

std::string verified_occupancy_publication_identity(const VerifiedOccupancyPublication& verification) {
    return sha256(verified_identity_json(verification).dump(false));
}

} // namespace internal

bool OccupancyPublication::valid() const {
    return storage_schema == kStorageSchema && publisher_sequence > 0 &&
           valid_parent(publisher_sequence, parent_publication_id) && internal::valid_sha256(id) &&
           id == internal::occupancy_publication_identity(*this) && valid_text(stream_id) &&
           valid_text(publisher_service_id) && internal::valid_sha256(publisher_key_id) &&
           internal::valid_sha256(trust_bundle_id) && internal::valid_sha256(occupancy_bundle_id) &&
           valid_text(timeline_id) && valid_text(workspace_frame_id) &&
           valid_from_tick <= valid_through_tick && internal::valid_sha256(payload_digest) &&
           payload_bytes > 0 && algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           valid_signature(authentication_tag);
}

bool VerifiedOccupancyPublication::valid() const {
    return internal::valid_sha256(id) && id == internal::verified_occupancy_publication_identity(*this) &&
           internal::valid_sha256(publication_id) && valid_text(stream_id) && publisher_sequence > 0 &&
           valid_text(publisher_service_id) && internal::valid_sha256(publisher_key_id) &&
           internal::valid_sha256(trust_bundle_id) && internal::valid_sha256(occupancy_bundle_id) &&
           valid_text(timeline_id) && valid_text(workspace_frame_id) &&
           valid_from_tick <= valid_through_tick && evaluation_tick >= valid_from_tick &&
           evaluation_tick <= valid_through_tick && internal::valid_sha256(payload_digest) &&
           payload_bytes > 0;
}

Result<OccupancyPublication> sign_continuous_fleet_occupancy_publication(
    const std::filesystem::path& occupancy_payload_path, const ServiceTrustBundle& trust_bundle,
    std::string stream_id, std::string publisher_service_id, std::string publisher_key_id,
    std::span<const std::byte> ed25519_secret_key, std::uint64_t publisher_sequence,
    std::string parent_publication_id, std::uint64_t valid_from_tick, std::uint64_t valid_through_tick,
    const ContinuousFleetOccupancyBundleLoadOptions& options) {
    if (!trust_bundle.valid() || !valid_text(stream_id) || !valid_text(publisher_service_id) ||
        !internal::valid_sha256(publisher_key_id) || publisher_sequence == 0 ||
        !valid_parent(publisher_sequence, parent_publication_id) || valid_from_tick > valid_through_tick ||
        ed25519_secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<OccupancyPublication>::failure(StatusCode::InvalidArgument,
                                                     "occupancy publication signing input is invalid");
    }
    auto trusted = trusted_service_public_key(trust_bundle, publisher_service_id, publisher_key_id,
                                              ArtifactTransferOperation::Publish, publisher_sequence);
    if (!trusted)
        return trusted.error();
    auto payload = read_occupancy_payload(occupancy_payload_path, options);
    if (!payload)
        return payload.error();
    auto bundle = load_exact_occupancy_payload(payload.value(), options);
    if (!bundle)
        return bundle.error();
    if (!bundle_covers_window(bundle.value(), valid_from_tick, valid_through_tick)) {
        return Result<OccupancyPublication>::failure(
            StatusCode::InvalidArgument,
            "occupancy publication validity window is not covered by every deployment trajectory");
    }

    OccupancyPublication publication;
    publication.storage_schema = kStorageSchema;
    publication.stream_id = std::move(stream_id);
    publication.publisher_sequence = publisher_sequence;
    publication.parent_publication_id = std::move(parent_publication_id);
    publication.publisher_service_id = std::move(publisher_service_id);
    publication.publisher_key_id = std::move(publisher_key_id);
    publication.trust_bundle_id = trust_bundle.id();
    publication.occupancy_bundle_id = bundle.value().id();
    publication.timeline_id = bundle.value().report().timeline_id;
    publication.workspace_frame_id = bundle.value().report().workspace_frame_id;
    publication.valid_from_tick = valid_from_tick;
    publication.valid_through_tick = valid_through_tick;
    publication.payload_digest = internal::sha256(payload.value());
    publication.payload_bytes = static_cast<std::uint64_t>(payload.value().size());
    publication.algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    publication.id = internal::occupancy_publication_identity(publication);
    const auto message = internal::occupancy_publication_signature_message(publication);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    publication.authentication_tag = internal::encode_hex(signature.value());
    auto verified_signature = ed25519_verify(std::as_bytes(std::span(message.data(), message.size())),
                                             signature.value(), trusted.value().public_key);
    if (!verified_signature) {
        return Result<OccupancyPublication>::failure(
            StatusCode::IdentityMismatch,
            "occupancy publication signing key does not match the trusted publisher key", publisher_key_id);
    }
    if (!publication.valid()) {
        return Result<OccupancyPublication>::failure(StatusCode::InternalError,
                                                     "occupancy publication signer produced invalid output");
    }
    return publication;
}

Result<VerifiedOccupancyPublication> verify_continuous_fleet_occupancy_publication(
    const std::filesystem::path& occupancy_payload_path, const OccupancyPublication& publication,
    const ServiceTrustBundle& trust_bundle, std::string_view expected_stream_id,
    std::string_view expected_publisher_service_id, std::string_view expected_trust_bundle_id,
    std::string_view expected_parent_publication_id, std::uint64_t evaluation_tick,
    const ContinuousFleetOccupancyBundleLoadOptions& options) {
    const bool expected_parent_valid = expected_parent_publication_id.empty() ||
                                       internal::valid_sha256(std::string(expected_parent_publication_id));
    if (!publication.valid() || !trust_bundle.valid() || !valid_text(expected_stream_id) ||
        !valid_text(expected_publisher_service_id) ||
        !internal::valid_sha256(std::string(expected_trust_bundle_id)) || !expected_parent_valid) {
        return Result<VerifiedOccupancyPublication>::failure(
            StatusCode::InvalidArgument,
            "occupancy publication verification input or caller pins are invalid");
    }
    if (publication.stream_id != expected_stream_id ||
        publication.publisher_service_id != expected_publisher_service_id ||
        publication.trust_bundle_id != expected_trust_bundle_id ||
        trust_bundle.id() != expected_trust_bundle_id ||
        publication.parent_publication_id != expected_parent_publication_id) {
        return Result<VerifiedOccupancyPublication>::failure(
            StatusCode::IdentityMismatch,
            "occupancy publication does not match caller-pinned stream, publisher, trust, or parent",
            publication.id);
    }
    if (evaluation_tick < publication.valid_from_tick || evaluation_tick > publication.valid_through_tick) {
        return Result<VerifiedOccupancyPublication>::failure(
            StatusCode::IdentityMismatch,
            "occupancy publication is not valid at the requested evaluation tick", publication.id);
    }
    auto trusted = trusted_service_public_key(
        trust_bundle, publication.publisher_service_id, publication.publisher_key_id,
        ArtifactTransferOperation::Publish, publication.publisher_sequence);
    if (!trusted)
        return trusted.error();
    auto signature = internal::decode_hex(publication.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::occupancy_publication_signature_message(publication);
    auto signature_verified = ed25519_verify(std::as_bytes(std::span(message.data(), message.size())),
                                             signature.value(), trusted.value().public_key);
    if (!signature_verified)
        return signature_verified.error();
    auto payload = read_occupancy_payload(occupancy_payload_path, options);
    if (!payload)
        return payload.error();
    auto bundle = load_exact_occupancy_payload(payload.value(), options);
    if (!bundle)
        return bundle.error();
    auto binding = verify_payload_binding(bundle.value(), publication, payload.value());
    if (!binding)
        return binding.error();

    VerifiedOccupancyPublication result;
    result.publication_id = publication.id;
    result.stream_id = publication.stream_id;
    result.publisher_sequence = publication.publisher_sequence;
    result.publisher_service_id = publication.publisher_service_id;
    result.publisher_key_id = publication.publisher_key_id;
    result.trust_bundle_id = publication.trust_bundle_id;
    result.occupancy_bundle_id = publication.occupancy_bundle_id;
    result.timeline_id = publication.timeline_id;
    result.workspace_frame_id = publication.workspace_frame_id;
    result.valid_from_tick = publication.valid_from_tick;
    result.valid_through_tick = publication.valid_through_tick;
    result.evaluation_tick = evaluation_tick;
    result.payload_digest = publication.payload_digest;
    result.payload_bytes = publication.payload_bytes;
    result.id = internal::verified_occupancy_publication_identity(result);
    if (!result.valid()) {
        return Result<VerifiedOccupancyPublication>::failure(
            StatusCode::InternalError, "occupancy publication verifier produced invalid output");
    }
    return result;
}

Result<void> verify_occupancy_publication_successor(const OccupancyPublication& previous,
                                                    const OccupancyPublication& successor) {
    if (!previous.valid() || !successor.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "occupancy publication successor input is invalid");
    }
    if (previous.publisher_sequence == std::numeric_limits<std::uint64_t>::max() ||
        successor.publisher_sequence != previous.publisher_sequence + 1 ||
        successor.parent_publication_id != previous.id || successor.stream_id != previous.stream_id ||
        successor.publisher_service_id != previous.publisher_service_id ||
        successor.timeline_id != previous.timeline_id ||
        successor.workspace_frame_id != previous.workspace_frame_id) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "occupancy publication successor does not extend the same publisher stream", successor.id);
    }
    return Result<void>::success();
}

Result<void> OccupancyPublication::save(const std::filesystem::path& path, const SaveOptions& options) const {
    return save_occupancy_publication(*this, path, options);
}

Result<OccupancyPublication> OccupancyPublication::load(const std::filesystem::path& path,
                                                        std::uintmax_t maximum_payload_bytes) {
    return load_occupancy_publication(path, maximum_payload_bytes);
}

} // namespace rbfsafe
