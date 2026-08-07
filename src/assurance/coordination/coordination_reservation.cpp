#include <rbfsafe/modules/assurance.h>

#include "internal/certificate_utils.h"
#include "internal/coordination.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::uint32_t kStorageSchema = 1;
constexpr std::size_t kMaximumIdentifierBytes = 256;

bool valid_text(std::string_view value) {
    return !value.empty() && value.size() <= kMaximumIdentifierBytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_parent(std::uint64_t round, const std::string& parent_id) {
    return round == 1 ? parent_id.empty() : internal::valid_sha256(parent_id);
}

bool negative_zero(double value) { return value == 0.0 && std::signbit(value); }

internal::Json participant_identity_json(const CoordinatedReservationParticipant& participant) {
    return internal::Json::Object{
        {"deployment_id", participant.deployment_id},
        {"occupancy_id", participant.occupancy_id},
        {"payload_bytes", std::to_string(participant.payload_bytes)},
        {"payload_digest", participant.payload_digest},
        {"publication_head_id", participant.publication_head_id},
        {"publication_root_id", participant.publication_root_id},
        {"publication_trust_bundle_id", participant.publication_trust_bundle_id},
        {"publisher_key_id", participant.publisher_key_id},
        {"publisher_sequence", std::to_string(participant.publisher_sequence)},
        {"publisher_service_id", participant.publisher_service_id},
        {"storage_schema", static_cast<int>(participant.storage_schema)},
        {"stream_id", participant.stream_id},
        {"trust_head_bundle_id", participant.trust_head_bundle_id},
        {"trust_root_bundle_id", participant.trust_root_bundle_id},
        {"valid_from_tick", std::to_string(participant.valid_from_tick)},
        {"valid_through_tick", std::to_string(participant.valid_through_tick)},
        {"verified_publication_id", participant.verified_publication_id},
    };
}

internal::Json agreement_identity_json(const CoordinatedReservationAgreement& agreement) {
    internal::Json::Array participants;
    participants.reserve(agreement.participants.size());
    for (const auto& participant : agreement.participants)
        participants.emplace_back(participant.id);
    return internal::Json::Object{
        {"evaluation_tick", std::to_string(agreement.evaluation_tick)},
        {"minimum_separation", agreement.minimum_separation},
        {"occupancy_bundle_id", agreement.occupancy_bundle_id},
        {"occupancy_report_id", agreement.occupancy_report_id},
        {"parent_agreement_id", agreement.parent_agreement_id},
        {"participants", std::move(participants)},
        {"payload_bytes", std::to_string(agreement.payload_bytes)},
        {"payload_digest", agreement.payload_digest},
        {"protocol_id", agreement.protocol_id},
        {"round", std::to_string(agreement.round)},
        {"storage_schema", static_cast<int>(agreement.storage_schema)},
        {"timeline_id", agreement.timeline_id},
        {"valid_from_tick", std::to_string(agreement.valid_from_tick)},
        {"valid_through_tick", std::to_string(agreement.valid_through_tick)},
        {"workspace_frame_id", agreement.workspace_frame_id},
    };
}

const RobotTrajectoryOccupancy* find_occupancy(const ContinuousFleetOccupancyBundle& bundle,
                                               std::string_view deployment_id) {
    const auto found = std::find_if(
        bundle.occupancies().begin(), bundle.occupancies().end(),
        [deployment_id](const auto& occupancy) { return occupancy.deployment_id == deployment_id; });
    return found == bundle.occupancies().end() ? nullptr : &*found;
}

Result<void> validate_bundle(const ContinuousFleetOccupancyBundle& bundle, std::size_t maximum_participants) {
    if (!bundle.valid() || maximum_participants == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "coordinated reservation bundle or limits are invalid");
    }
    if (bundle.occupancies().size() < 2) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "coordinated reservation requires at least two deployments");
    }
    if (bundle.occupancies().size() > maximum_participants) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "coordinated reservation participant count exceeds limit");
    }
    if (bundle.report().status != ContinuousFleetOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes ||
        !bundle.report().conflicts.empty()) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "coordinated reservation requires a separated continuous occupancy bundle", bundle.id());
    }
    return Result<void>::success();
}

Result<void> verify_trust_prefix(const ServiceTrustHistory& trust_history,
                                 std::string_view publication_trust_bundle_id,
                                 std::string_view agreement_trust_head_bundle_id) {
    std::optional<std::size_t> publication_trust_index;
    std::optional<std::size_t> agreement_trust_head_index;
    for (std::size_t index = 0; index < trust_history.records().size(); ++index) {
        const auto& record = trust_history.records()[index];
        if (record.bundle_id == publication_trust_bundle_id)
            publication_trust_index = index;
        if (record.bundle_id == agreement_trust_head_bundle_id)
            agreement_trust_head_index = index;
    }
    if (!publication_trust_index || !agreement_trust_head_index ||
        *publication_trust_index > *agreement_trust_head_index) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "reservation publication trust is not a prefix of the pinned agreement trust head",
            std::string(publication_trust_bundle_id));
    }
    return Result<void>::success();
}

Result<CoordinatedReservationParticipant>
make_participant(std::string deployment_id, const RobotTrajectoryOccupancy& occupancy,
                 const RotatingOccupancyPublicationHistory& history, std::string_view publication_id,
                 std::string_view trust_head_bundle_id, std::uint64_t evaluation_tick) {
    if (!valid_text(deployment_id) || !occupancy.valid() || !history.valid() ||
        !internal::valid_sha256(std::string(publication_id)) ||
        !internal::valid_sha256(std::string(trust_head_bundle_id))) {
        return Result<CoordinatedReservationParticipant>::failure(
            StatusCode::InvalidArgument, "coordinated reservation participant input is invalid");
    }
    auto trust_history = history.trust_history();
    if (!trust_history)
        return trust_history.error();
    if (trust_history.value().root_bundle_id() != history.trust_root_bundle_id()) {
        return Result<CoordinatedReservationParticipant>::failure(
            StatusCode::IdentityMismatch,
            "coordinated reservation history has inconsistent trust-root metadata");
    }
    auto agreement_trust_head = trust_history.value().bundle(std::string(trust_head_bundle_id));
    if (!agreement_trust_head)
        return agreement_trust_head.error();
    auto publication = history.publication(publication_id);
    if (!publication)
        return publication.error();
    auto trust_prefix =
        verify_trust_prefix(trust_history.value(), publication.value().trust_bundle_id, trust_head_bundle_id);
    if (!trust_prefix)
        return trust_prefix.error();
    auto verified = history.verify(publication_id, evaluation_tick);
    if (!verified)
        return verified.error();

    CoordinatedReservationParticipant result;
    result.storage_schema = kStorageSchema;
    result.deployment_id = std::move(deployment_id);
    result.occupancy_id = occupancy.id;
    result.stream_id = history.stream_id();
    result.publisher_service_id = history.publisher_service_id();
    result.publisher_key_id = publication.value().publisher_key_id;
    result.publisher_sequence = publication.value().publisher_sequence;
    result.trust_root_bundle_id = history.trust_root_bundle_id();
    result.trust_head_bundle_id = std::string(trust_head_bundle_id);
    result.publication_trust_bundle_id = publication.value().trust_bundle_id;
    result.publication_root_id = history.root_publication_id();
    result.publication_head_id = publication.value().id;
    result.verified_publication_id = verified.value().id;
    result.payload_digest = verified.value().payload_digest;
    result.payload_bytes = verified.value().payload_bytes;
    result.valid_from_tick = verified.value().valid_from_tick;
    result.valid_through_tick = verified.value().valid_through_tick;
    result.id = internal::coordinated_reservation_participant_identity(result);
    if (!result.valid()) {
        return Result<CoordinatedReservationParticipant>::failure(
            StatusCode::InternalError,
            "coordinated reservation participant construction produced invalid output");
    }
    return result;
}

Result<void> validate_participant_coverage(const ContinuousFleetOccupancyBundle& bundle,
                                           const std::vector<CoordinatedReservationParticipant>& participants,
                                           std::uint64_t evaluation_tick) {
    if (participants.size() != bundle.occupancies().size()) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "coordinated reservation participants do not cover every occupancy deployment");
    }
    for (const auto& participant : participants) {
        const auto* occupancy = find_occupancy(bundle, participant.deployment_id);
        if (occupancy == nullptr || occupancy->id != participant.occupancy_id ||
            participant.payload_digest != participants.front().payload_digest ||
            participant.payload_bytes != participants.front().payload_bytes ||
            evaluation_tick < participant.valid_from_tick ||
            evaluation_tick > participant.valid_through_tick) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "coordinated reservation participant does not match the common occupancy plan",
                participant.id);
        }
    }
    return Result<void>::success();
}

Result<void> verify_stored_participant(const CoordinatedReservationParticipant& participant,
                                       const CoordinatedReservationAgreement& agreement,
                                       const RotatingOccupancyPublicationHistory& history) {
    if (!history.valid() || history.stream_id() != participant.stream_id ||
        history.publisher_service_id() != participant.publisher_service_id ||
        history.trust_root_bundle_id() != participant.trust_root_bundle_id ||
        history.root_publication_id() != participant.publication_root_id ||
        history.timeline_id() != agreement.timeline_id ||
        history.workspace_frame_id() != agreement.workspace_frame_id) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "coordinated reservation participant history does not match its pinned identity", participant.id);
    }
    auto publication = history.publication(participant.publication_head_id);
    if (!publication) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "coordinated reservation participant publication is absent from its history",
            participant.publication_head_id);
    }
    auto trust_history = history.trust_history();
    if (!trust_history)
        return trust_history.error();
    auto trust_head = trust_history.value().bundle(participant.trust_head_bundle_id);
    if (!trust_head) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "coordinated reservation participant trust head is absent from its history",
            participant.trust_head_bundle_id);
    }
    auto trust_prefix = verify_trust_prefix(trust_history.value(), participant.publication_trust_bundle_id,
                                            participant.trust_head_bundle_id);
    if (!trust_prefix)
        return trust_prefix;
    auto verified = history.verify(participant.publication_head_id, agreement.evaluation_tick);
    if (!verified)
        return verified.error();
    if (publication.value().publisher_key_id != participant.publisher_key_id ||
        publication.value().publisher_sequence != participant.publisher_sequence ||
        publication.value().trust_bundle_id != participant.publication_trust_bundle_id ||
        publication.value().occupancy_bundle_id != agreement.occupancy_bundle_id ||
        publication.value().timeline_id != agreement.timeline_id ||
        publication.value().workspace_frame_id != agreement.workspace_frame_id ||
        publication.value().payload_digest != participant.payload_digest ||
        publication.value().payload_bytes != participant.payload_bytes ||
        publication.value().valid_from_tick != participant.valid_from_tick ||
        publication.value().valid_through_tick != participant.valid_through_tick ||
        verified.value().id != participant.verified_publication_id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "coordinated reservation participant statement differs from its history",
                                     participant.id);
    }
    return Result<void>::success();
}

std::optional<std::size_t> publication_index(const RotatingOccupancyPublicationHistory& history,
                                             std::string_view publication_id) {
    for (std::size_t index = 0; index < history.records().size(); ++index) {
        if (history.records()[index].publication_id == publication_id)
            return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> trust_index(const ServiceTrustHistory& history, std::string_view bundle_id) {
    for (std::size_t index = 0; index < history.records().size(); ++index) {
        if (history.records()[index].bundle_id == bundle_id)
            return index;
    }
    return std::nullopt;
}

Result<CoordinatedReservationAgreement>
assemble_agreement(std::string protocol_id, std::uint64_t round, std::string parent_agreement_id,
                   std::uint64_t evaluation_tick, const ContinuousFleetOccupancyBundle& bundle,
                   std::vector<CoordinatedReservationParticipant> participants) {
    if (!valid_text(protocol_id) || round == 0 || !valid_parent(round, parent_agreement_id) ||
        participants.empty()) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::InvalidArgument, "coordinated reservation agreement metadata is invalid");
    }
    std::sort(participants.begin(), participants.end(),
              [](const auto& left, const auto& right) { return left.deployment_id < right.deployment_id; });
    auto coverage = validate_participant_coverage(bundle, participants, evaluation_tick);
    if (!coverage)
        return coverage.error();

    CoordinatedReservationAgreement result;
    result.storage_schema = kStorageSchema;
    result.protocol_id = std::move(protocol_id);
    result.round = round;
    result.parent_agreement_id = std::move(parent_agreement_id);
    result.evaluation_tick = evaluation_tick;
    result.valid_from_tick = 0;
    result.valid_through_tick = std::numeric_limits<std::uint64_t>::max();
    for (const auto& participant : participants) {
        result.valid_from_tick = std::max(result.valid_from_tick, participant.valid_from_tick);
        result.valid_through_tick = std::min(result.valid_through_tick, participant.valid_through_tick);
    }
    result.occupancy_bundle_id = bundle.id();
    result.occupancy_report_id = bundle.report().id;
    result.timeline_id = bundle.report().timeline_id;
    result.workspace_frame_id = bundle.report().workspace_frame_id;
    result.minimum_separation = bundle.report().minimum_separation;
    result.payload_digest = participants.front().payload_digest;
    result.payload_bytes = participants.front().payload_bytes;
    result.participants = std::move(participants);
    result.id = internal::coordinated_reservation_agreement_identity(result);
    if (!result.valid()) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::InternalError,
            "coordinated reservation agreement construction produced invalid output");
    }
    return result;
}

} // namespace

namespace internal {

std::string
coordinated_reservation_participant_identity(const CoordinatedReservationParticipant& participant) {
    return sha256(participant_identity_json(participant).dump(false));
}

std::string coordinated_reservation_agreement_identity(const CoordinatedReservationAgreement& agreement) {
    return sha256(agreement_identity_json(agreement).dump(false));
}

} // namespace internal

bool CoordinatedReservationParticipant::valid() const {
    return storage_schema == kStorageSchema && internal::valid_sha256(id) &&
           id == internal::coordinated_reservation_participant_identity(*this) && valid_text(deployment_id) &&
           internal::valid_sha256(occupancy_id) && valid_text(stream_id) &&
           valid_text(publisher_service_id) && internal::valid_sha256(publisher_key_id) &&
           publisher_sequence > 0 && internal::valid_sha256(trust_root_bundle_id) &&
           internal::valid_sha256(trust_head_bundle_id) &&
           internal::valid_sha256(publication_trust_bundle_id) &&
           internal::valid_sha256(publication_root_id) && internal::valid_sha256(publication_head_id) &&
           internal::valid_sha256(verified_publication_id) && internal::valid_sha256(payload_digest) &&
           payload_bytes > 0 && valid_from_tick <= valid_through_tick;
}

bool CoordinatedReservationAgreement::valid() const {
    if (storage_schema != kStorageSchema || !internal::valid_sha256(id) ||
        id != internal::coordinated_reservation_agreement_identity(*this) || !valid_text(protocol_id) ||
        round == 0 || !valid_parent(round, parent_agreement_id) || evaluation_tick < valid_from_tick ||
        evaluation_tick > valid_through_tick || !internal::valid_sha256(occupancy_bundle_id) ||
        !internal::valid_sha256(occupancy_report_id) || !valid_text(timeline_id) ||
        !valid_text(workspace_frame_id) || !std::isfinite(minimum_separation) || minimum_separation < 0.0 ||
        negative_zero(minimum_separation) || !internal::valid_sha256(payload_digest) || payload_bytes == 0 ||
        participants.size() < 2) {
        return false;
    }
    std::uint64_t expected_from = 0;
    std::uint64_t expected_through = std::numeric_limits<std::uint64_t>::max();
    std::set<std::string> occupancy_ids;
    std::set<std::pair<std::string, std::string>> publishers;
    for (std::size_t index = 0; index < participants.size(); ++index) {
        const auto& participant = participants[index];
        if (!participant.valid() ||
            (index > 0 && participants[index - 1].deployment_id >= participant.deployment_id) ||
            !occupancy_ids.insert(participant.occupancy_id).second ||
            !publishers.insert({participant.publisher_service_id, participant.stream_id}).second ||
            participant.payload_digest != payload_digest || participant.payload_bytes != payload_bytes ||
            evaluation_tick < participant.valid_from_tick ||
            evaluation_tick > participant.valid_through_tick) {
            return false;
        }
        expected_from = std::max(expected_from, participant.valid_from_tick);
        expected_through = std::min(expected_through, participant.valid_through_tick);
    }
    return valid_from_tick == expected_from && valid_through_tick == expected_through;
}

Result<CoordinatedReservationAgreement>
make_coordinated_reservation_agreement(std::string protocol_id, std::uint64_t round,
                                       std::string parent_agreement_id, std::uint64_t evaluation_tick,
                                       const ContinuousFleetOccupancyBundle& occupancy_bundle,
                                       std::span<const std::string> deployment_ids,
                                       std::span<const RotatingOccupancyPublicationHistory> histories,
                                       const CoordinatedReservationAgreementLoadOptions& options) {
    if (options.maximum_participants == 0 || options.maximum_payload_bytes == 0 ||
        deployment_ids.size() != histories.size()) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::InvalidArgument, "coordinated reservation input or limits are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::Cancelled, "coordinated reservation creation was cancelled");
    }
    auto bundle_valid = validate_bundle(occupancy_bundle, options.maximum_participants);
    if (!bundle_valid)
        return bundle_valid.error();
    if (deployment_ids.size() != occupancy_bundle.occupancies().size()) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::IdentityMismatch,
            "coordinated reservation histories do not cover every occupancy deployment");
    }

    std::set<std::string> unique_deployments;
    for (const auto& deployment_id : deployment_ids) {
        if (!valid_text(deployment_id) || !unique_deployments.insert(deployment_id).second) {
            return Result<CoordinatedReservationAgreement>::failure(
                StatusCode::InvalidArgument,
                "coordinated reservation deployment input is invalid or duplicated", deployment_id);
        }
    }
    std::vector<CoordinatedReservationParticipant> participants;
    participants.reserve(histories.size());
    for (std::size_t index = 0; index < histories.size(); ++index) {
        if (options.cancellation.cancelled()) {
            return Result<CoordinatedReservationAgreement>::failure(
                StatusCode::Cancelled, "coordinated reservation creation was cancelled");
        }
        const auto* occupancy = find_occupancy(occupancy_bundle, deployment_ids[index]);
        if (occupancy == nullptr) {
            return Result<CoordinatedReservationAgreement>::failure(
                StatusCode::IdentityMismatch,
                "coordinated reservation deployment is absent from the occupancy bundle",
                deployment_ids[index]);
        }
        auto participant = make_participant(deployment_ids[index], *occupancy, histories[index],
                                            histories[index].current_publication_id(),
                                            histories[index].current_trust_bundle_id(), evaluation_tick);
        if (!participant)
            return participant.error();
        auto publication = histories[index].publication(participant.value().publication_head_id);
        if (!publication)
            return publication.error();
        if (publication.value().occupancy_bundle_id != occupancy_bundle.id() ||
            publication.value().timeline_id != occupancy_bundle.report().timeline_id ||
            publication.value().workspace_frame_id != occupancy_bundle.report().workspace_frame_id ||
            participant.value().payload_bytes > options.maximum_payload_bytes) {
            return Result<CoordinatedReservationAgreement>::failure(
                StatusCode::IdentityMismatch,
                "coordinated reservation publisher did not sign the exact common occupancy plan",
                participant.value().publication_head_id);
        }
        participants.push_back(std::move(participant).value());
    }
    return assemble_agreement(std::move(protocol_id), round, std::move(parent_agreement_id), evaluation_tick,
                              occupancy_bundle, std::move(participants));
}

Result<void>
verify_coordinated_reservation_agreement(const CoordinatedReservationAgreement& agreement,
                                         const ContinuousFleetOccupancyBundle& occupancy_bundle,
                                         std::span<const std::string> deployment_ids,
                                         std::span<const RotatingOccupancyPublicationHistory> histories,
                                         const CoordinatedReservationAgreementLoadOptions& options) {
    if (!agreement.valid() || options.maximum_participants == 0 || options.maximum_payload_bytes == 0 ||
        deployment_ids.size() != histories.size()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "coordinated reservation verification input or limits are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<void>::failure(StatusCode::Cancelled,
                                     "coordinated reservation verification was cancelled");
    }
    if (agreement.participants.size() > options.maximum_participants ||
        agreement.payload_bytes > options.maximum_payload_bytes) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "coordinated reservation agreement exceeds verification limits");
    }
    auto bundle_valid = validate_bundle(occupancy_bundle, options.maximum_participants);
    if (!bundle_valid)
        return bundle_valid;
    if (agreement.occupancy_bundle_id != occupancy_bundle.id() ||
        agreement.occupancy_report_id != occupancy_bundle.report().id ||
        agreement.timeline_id != occupancy_bundle.report().timeline_id ||
        agreement.workspace_frame_id != occupancy_bundle.report().workspace_frame_id ||
        agreement.minimum_separation != occupancy_bundle.report().minimum_separation ||
        deployment_ids.size() != agreement.participants.size()) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "coordinated reservation agreement does not match the supplied occupancy bundle");
    }

    std::vector<bool> used(histories.size(), false);
    for (const auto& stored : agreement.participants) {
        if (options.cancellation.cancelled()) {
            return Result<void>::failure(StatusCode::Cancelled,
                                         "coordinated reservation verification was cancelled");
        }
        std::optional<std::size_t> source_index;
        for (std::size_t index = 0; index < deployment_ids.size(); ++index) {
            if (deployment_ids[index] == stored.deployment_id) {
                if (source_index) {
                    return Result<void>::failure(StatusCode::InvalidArgument,
                                                 "coordinated reservation deployment input is duplicated",
                                                 stored.deployment_id);
                }
                source_index = index;
            }
        }
        if (!source_index || used[*source_index]) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "coordinated reservation participant history is missing or duplicated", stored.deployment_id);
        }
        used[*source_index] = true;
        const auto& history = histories[*source_index];
        if (!history.valid() || history.stream_id() != stored.stream_id ||
            history.publisher_service_id() != stored.publisher_service_id ||
            history.trust_root_bundle_id() != stored.trust_root_bundle_id ||
            history.root_publication_id() != stored.publication_root_id ||
            history.timeline_id() != agreement.timeline_id ||
            history.workspace_frame_id() != agreement.workspace_frame_id) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "coordinated reservation participant history does not match its pinned identity", stored.id);
        }
        const auto* occupancy = find_occupancy(occupancy_bundle, stored.deployment_id);
        if (occupancy == nullptr) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "coordinated reservation participant occupancy is missing",
                                         stored.deployment_id);
        }
        auto replayed =
            make_participant(stored.deployment_id, *occupancy, history, stored.publication_head_id,
                             stored.trust_head_bundle_id, agreement.evaluation_tick);
        if (!replayed)
            return replayed.error();
        auto publication = history.publication(stored.publication_head_id);
        if (!publication)
            return publication.error();
        if (publication.value().occupancy_bundle_id != occupancy_bundle.id() ||
            replayed.value().id != stored.id) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "coordinated reservation participant replay differs from the agreement", stored.id);
        }
    }
    return Result<void>::success();
}

Result<void>
verify_coordinated_reservation_successor(const CoordinatedReservationAgreement& previous,
                                         const CoordinatedReservationAgreement& successor,
                                         std::span<const std::string> deployment_ids,
                                         std::span<const RotatingOccupancyPublicationHistory> histories,
                                         const CoordinatedReservationAgreementLoadOptions& options) {
    if (!previous.valid() || !successor.valid() || options.maximum_participants == 0 ||
        deployment_ids.size() != histories.size() || deployment_ids.size() != previous.participants.size()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "coordinated reservation successor input is invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<void>::failure(StatusCode::Cancelled,
                                     "coordinated reservation successor verification was cancelled");
    }
    if (previous.participants.size() > options.maximum_participants ||
        previous.payload_bytes > options.maximum_payload_bytes ||
        successor.payload_bytes > options.maximum_payload_bytes) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "coordinated reservation successor exceeds verification limits");
    }
    if (previous.round == std::numeric_limits<std::uint64_t>::max() ||
        successor.round != previous.round + 1 || successor.parent_agreement_id != previous.id ||
        successor.protocol_id != previous.protocol_id || successor.timeline_id != previous.timeline_id ||
        successor.workspace_frame_id != previous.workspace_frame_id ||
        successor.evaluation_tick < previous.evaluation_tick ||
        successor.participants.size() != previous.participants.size()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "coordinated reservation successor does not extend the same protocol");
    }
    std::set<std::string> mapped_deployments;
    for (std::size_t index = 0; index < previous.participants.size(); ++index) {
        const auto& before = previous.participants[index];
        const auto& after = successor.participants[index];
        if (after.deployment_id != before.deployment_id || after.stream_id != before.stream_id ||
            after.publisher_service_id != before.publisher_service_id ||
            after.trust_root_bundle_id != before.trust_root_bundle_id ||
            after.publication_root_id != before.publication_root_id ||
            after.publisher_sequence <= before.publisher_sequence) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "coordinated reservation successor changes membership or lacks a fresh signature", after.id);
        }
        std::optional<std::size_t> history_index;
        for (std::size_t candidate = 0; candidate < deployment_ids.size(); ++candidate) {
            if (deployment_ids[candidate] == before.deployment_id) {
                if (history_index) {
                    return Result<void>::failure(
                        StatusCode::InvalidArgument,
                        "coordinated reservation successor deployment input is duplicated",
                        before.deployment_id);
                }
                history_index = candidate;
            }
        }
        if (!history_index || !mapped_deployments.insert(before.deployment_id).second) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "coordinated reservation successor participant history is missing",
                                         before.deployment_id);
        }
        if (options.cancellation.cancelled()) {
            return Result<void>::failure(StatusCode::Cancelled,
                                         "coordinated reservation successor verification was cancelled");
        }
        const auto& history = histories[*history_index];
        auto before_verified = verify_stored_participant(before, previous, history);
        if (!before_verified)
            return before_verified;
        auto after_verified = verify_stored_participant(after, successor, history);
        if (!after_verified)
            return after_verified;
        const auto before_publication = publication_index(history, before.publication_head_id);
        const auto after_publication = publication_index(history, after.publication_head_id);
        auto participant_trust_history = history.trust_history();
        if (!participant_trust_history)
            return participant_trust_history.error();
        const auto before_trust = trust_index(participant_trust_history.value(), before.trust_head_bundle_id);
        const auto after_trust = trust_index(participant_trust_history.value(), after.trust_head_bundle_id);
        if (!before_publication || !after_publication || *before_publication >= *after_publication ||
            !before_trust || !after_trust || *before_trust > *after_trust) {
            return Result<void>::failure(
                StatusCode::IdentityMismatch,
                "coordinated reservation successor is not a true participant history extension", after.id);
        }
    }
    return Result<void>::success();
}

} // namespace rbfsafe
