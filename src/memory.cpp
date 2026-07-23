#include <rbfsafe/memory.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/memory.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumLocatorBytes = 4096;
constexpr std::size_t kMaximumDetailBytes = 4096;
constexpr std::size_t kMaximumTags = 64;
constexpr std::size_t kMaximumTagBytes = 128;
constexpr std::size_t kMaximumFleetMembers = 10'000;

bool contains_control_character(const std::string& value) {
    return std::any_of(value.begin(), value.end(),
                       [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_artifact_type(MemoryArtifactType type) {
    return type >= MemoryArtifactType::SafeAtlas && type <= MemoryArtifactType::FleetSchedule;
}

bool valid_artifact_state(MemoryArtifactState state) {
    return state >= MemoryArtifactState::Active && state <= MemoryArtifactState::Retired;
}

bool valid_event_type(MemoryEventType type) {
    return type >= MemoryEventType::Registered && type <= MemoryEventType::SceneInvalidated;
}

bool valid_evidence(EvidenceLevel evidence) {
    return evidence >= EvidenceLevel::Unknown && evidence <= EvidenceLevel::RuntimeExecutable;
}

bool valid_identifier(const std::string& value, bool allow_empty = false) {
    return (allow_empty || !value.empty()) && value.size() <= kMaximumIdentifierBytes &&
           !contains_control_character(value);
}

Result<void> normalize_tags(std::vector<std::string>& tags) {
    if (tags.size() > kMaximumTags) {
        return Result<void>::failure(StatusCode::ResourceLimit, "memory artifact has too many tags");
    }
    for (const auto& tag : tags) {
        if (tag.empty() || tag.size() > kMaximumTagBytes || contains_control_character(tag)) {
            return Result<void>::failure(StatusCode::InvalidArgument, "memory artifact tag is invalid");
        }
    }
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    return Result<void>::success();
}

internal::Json string_array_json(const std::vector<std::string>& values) {
    internal::Json::Array result;
    result.reserve(values.size());
    for (const auto& value : values)
        result.emplace_back(value);
    return result;
}

internal::Json workspace_aabb_json(const WorkspaceAabb& box) {
    internal::Json::Array lower;
    internal::Json::Array upper;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        lower.emplace_back(box.lower[axis]);
        upper.emplace_back(box.upper[axis]);
    }
    return internal::Json::Object{{"lower", std::move(lower)}, {"upper", std::move(upper)}};
}

internal::Json artifact_identity_json(const MemoryArtifact& artifact) {
    return internal::Json::Object{
        {"content_digest", artifact.content_digest},       {"deployment_id", artifact.deployment_id},
        {"evidence", static_cast<int>(artifact.evidence)}, {"locator", artifact.locator},
        {"robot_digest", artifact.robot_digest},           {"scene_digest", artifact.scene_digest},
        {"tags", string_array_json(artifact.tags)},        {"task_id", artifact.task_id},
        {"type", static_cast<int>(artifact.type)},
    };
}

internal::Json event_identity_json(const MemoryEvent& event) {
    return internal::Json::Object{
        {"artifact_id", event.artifact_id},
        {"current_state", static_cast<int>(event.current_state)},
        {"detail", event.detail},
        {"previous_state", static_cast<int>(event.previous_state)},
        {"sequence", std::to_string(event.sequence)},
        {"task_id", event.task_id},
        {"type", static_cast<int>(event.type)},
    };
}

internal::Json safety_memory_identity_json(const SafetyMemory& memory) {
    internal::Json::Array artifacts;
    artifacts.reserve(memory.artifacts().size());
    for (const auto& artifact : memory.artifacts()) {
        artifacts.emplace_back(internal::Json::Object{
            {"generation", std::to_string(artifact.generation)},
            {"id", artifact.id},
            {"registered_sequence", std::to_string(artifact.registered_sequence)},
            {"state", static_cast<int>(artifact.state)},
        });
    }
    internal::Json::Array events;
    events.reserve(memory.events().size());
    for (const auto& event : memory.events())
        events.emplace_back(event.id);
    return internal::Json::Object{
        {"artifacts", std::move(artifacts)},
        {"events", std::move(events)},
        {"format", "rbfsafe-safety-memory-identity"},
        {"next_sequence", std::to_string(memory.next_sequence())},
        {"schema", 1},
    };
}

Result<void> validate_reuse_query(const MemoryReuseQuery& query) {
    if (!valid_identifier(query.deployment_id) || !internal::valid_sha256(query.robot_digest) ||
        !internal::valid_sha256(query.scene_digest) || !valid_identifier(query.target_task_id) ||
        !valid_evidence(query.minimum_evidence) || query.maximum_results == 0 ||
        (query.type && !valid_artifact_type(*query.type))) {
        return Result<void>::failure(StatusCode::InvalidArgument, "memory reuse query is invalid");
    }
    auto tags = query.required_tags;
    return normalize_tags(tags);
}

bool includes_all_tags(const std::vector<std::string>& available, const std::vector<std::string>& required) {
    return std::includes(available.begin(), available.end(), required.begin(), required.end());
}

MemoryReuseCandidate assess(const MemoryArtifact& artifact, const MemoryReuseQuery& query) {
    MemoryReuseCandidate result;
    result.artifact = artifact;
    result.cross_task = artifact.task_id != query.target_task_id;
    if (artifact.deployment_id != query.deployment_id) {
        result.reason = "deployment identity does not match";
        return result;
    }
    if (artifact.robot_digest != query.robot_digest) {
        result.reason = "robot identity does not match";
        return result;
    }
    if (query.type && artifact.type != *query.type) {
        result.reason = "artifact type does not match";
        return result;
    }
    if (!query.include_same_task && !result.cross_task) {
        result.reason = "same-task artifacts are excluded";
        return result;
    }
    if (static_cast<int>(artifact.evidence) < static_cast<int>(query.minimum_evidence)) {
        result.reason = "artifact evidence is below the requested minimum";
        return result;
    }
    auto required_tags = query.required_tags;
    std::sort(required_tags.begin(), required_tags.end());
    required_tags.erase(std::unique(required_tags.begin(), required_tags.end()), required_tags.end());
    if (!includes_all_tags(artifact.tags, required_tags)) {
        result.reason = "artifact tags do not satisfy the query";
        return result;
    }
    if (artifact.state == MemoryArtifactState::Quarantined ||
        artifact.state == MemoryArtifactState::Retired) {
        result.reason = "artifact lifecycle state forbids reuse";
        return result;
    }
    if (artifact.state == MemoryArtifactState::Stale) {
        result.disposition = ReuseDisposition::RequiresRevalidation;
        result.reason = "artifact is stale";
        return result;
    }
    if (artifact.scene_digest != query.scene_digest) {
        result.disposition = ReuseDisposition::RequiresRevalidation;
        result.reason = "scene identity changed";
        return result;
    }
    result.disposition = ReuseDisposition::Direct;
    result.reason =
        result.cross_task ? "exact identities permit cross-task reuse" : "exact identities permit reuse";
    return result;
}

bool transition_allowed(MemoryArtifactState from, MemoryArtifactState to) {
    if (from == MemoryArtifactState::Active) {
        return to == MemoryArtifactState::Stale || to == MemoryArtifactState::Quarantined ||
               to == MemoryArtifactState::Retired;
    }
    if (from == MemoryArtifactState::Stale)
        return to == MemoryArtifactState::Quarantined || to == MemoryArtifactState::Retired;
    return from == MemoryArtifactState::Quarantined && to == MemoryArtifactState::Retired;
}

std::vector<MemoryArtifact>::iterator find_artifact(std::vector<MemoryArtifact>& artifacts,
                                                    const std::string& id) {
    return std::lower_bound(
        artifacts.begin(), artifacts.end(), id,
        [](const MemoryArtifact& artifact, const std::string& candidate) { return artifact.id < candidate; });
}

std::vector<MemoryArtifact>::const_iterator find_artifact(const std::vector<MemoryArtifact>& artifacts,
                                                          const std::string& id) {
    return std::lower_bound(
        artifacts.begin(), artifacts.end(), id,
        [](const MemoryArtifact& artifact, const std::string& candidate) { return artifact.id < candidate; });
}

MemoryEvent make_event(std::uint64_t sequence, MemoryEventType type, const MemoryArtifact& artifact,
                       MemoryArtifactState previous_state, std::string task_id, std::string detail) {
    MemoryEvent event;
    event.sequence = sequence;
    event.type = type;
    event.artifact_id = artifact.id;
    event.previous_state = previous_state;
    event.current_state = artifact.state;
    event.task_id = std::move(task_id);
    event.detail = std::move(detail);
    event.id = internal::memory_event_identity(event);
    return event;
}

bool contains_box(const WorkspaceAabb& outer, const WorkspaceAabb& inner) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (inner.lower[axis] < outer.lower[axis] || inner.upper[axis] > outer.upper[axis])
            return false;
    }
    return true;
}

const FleetMember* find_member(const FleetSnapshot& fleet, const std::string& deployment_id) {
    const auto member = std::lower_bound(
        fleet.members.begin(), fleet.members.end(), deployment_id,
        [](const FleetMember& candidate, const std::string& id) { return candidate.deployment_id < id; });
    return member != fleet.members.end() && member->deployment_id == deployment_id ? &*member : nullptr;
}

bool time_overlaps(const FleetReservation& first, const FleetReservation& second) {
    return first.begin_tick < second.end_tick && second.begin_tick < first.end_tick;
}

bool valid_fleet_snapshot(const FleetSnapshot& fleet) {
    if (!valid_identifier(fleet.fleet_id) || !internal::valid_sha256(fleet.scene_digest) ||
        fleet.members.empty() || fleet.members.size() > kMaximumFleetMembers ||
        !internal::valid_sha256(fleet.id)) {
        return false;
    }
    std::string previous;
    for (const auto& member : fleet.members) {
        if (!valid_identifier(member.deployment_id) || !internal::valid_sha256(member.robot_digest) ||
            !member.operating_envelope.valid() || (!previous.empty() && previous >= member.deployment_id)) {
            return false;
        }
        previous = member.deployment_id;
    }
    return internal::fleet_snapshot_identity(fleet) == fleet.id;
}

bool source_type_supports_reservation(MemoryArtifactType type) {
    return type == MemoryArtifactType::SafeAtlas || type == MemoryArtifactType::RegionDatabase ||
           type == MemoryArtifactType::SafeCorridor || type == MemoryArtifactType::TrajectoryAudit;
}

} // namespace

namespace internal {

std::string memory_artifact_identity(const MemoryArtifact& artifact) {
    return sha256(artifact_identity_json(artifact).dump(false));
}

std::string memory_event_identity(const MemoryEvent& event) {
    return sha256(event_identity_json(event).dump(false));
}

std::string safety_memory_revision_identity(const SafetyMemoryRevisionInfo& revision) {
    return sha256(internal::Json(internal::Json::Object{
                                     {"format", "rbfsafe-safety-memory-revision"},
                                     {"memory_id", revision.memory_id},
                                     {"parent_id", revision.parent_id},
                                     {"schema", 1},
                                     {"sequence", std::to_string(revision.sequence)},
                                 })
                      .dump(false));
}

Result<void> validate_memory_artifact(const MemoryArtifact& artifact) {
    auto tags = artifact.tags;
    auto tag_status = normalize_tags(tags);
    if (!tag_status)
        return tag_status;
    if (tags != artifact.tags || !valid_sha256(artifact.id) || !valid_artifact_type(artifact.type) ||
        !valid_artifact_state(artifact.state) || !valid_identifier(artifact.deployment_id) ||
        !valid_sha256(artifact.robot_digest) || !valid_sha256(artifact.scene_digest) ||
        !valid_identifier(artifact.task_id) || !valid_sha256(artifact.content_digest) ||
        artifact.locator.empty() || artifact.locator.size() > kMaximumLocatorBytes ||
        contains_control_character(artifact.locator) || !valid_evidence(artifact.evidence) ||
        artifact.generation == 0 || artifact.registered_sequence == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument, "memory artifact is invalid", artifact.id);
    }
    if (memory_artifact_identity(artifact) != artifact.id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "memory artifact identity does not match its content", artifact.id);
    }
    return Result<void>::success();
}

Result<void> validate_memory_event(const MemoryEvent& event) {
    if (!valid_sha256(event.id) || event.sequence == 0 || !valid_event_type(event.type) ||
        !valid_sha256(event.artifact_id) || !valid_artifact_state(event.previous_state) ||
        !valid_artifact_state(event.current_state) || !valid_identifier(event.task_id) ||
        event.detail.empty() || event.detail.size() > kMaximumDetailBytes ||
        contains_control_character(event.detail)) {
        return Result<void>::failure(StatusCode::InvalidArgument, "memory event is invalid", event.id);
    }
    const bool changes_state =
        event.type == MemoryEventType::StateTransition || event.type == MemoryEventType::SceneInvalidated;
    if ((changes_state && event.previous_state == event.current_state) ||
        (!changes_state && event.previous_state != event.current_state)) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "memory event state transition is inconsistent", event.id);
    }
    if (memory_event_identity(event) != event.id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "memory event identity does not match its content", event.id);
    }
    return Result<void>::success();
}

std::string fleet_snapshot_identity(const FleetSnapshot& fleet) {
    Json::Array members;
    members.reserve(fleet.members.size());
    for (const auto& member : fleet.members) {
        members.emplace_back(Json::Object{
            {"deployment_id", member.deployment_id},
            {"operating_envelope", workspace_aabb_json(member.operating_envelope)},
            {"robot_digest", member.robot_digest},
        });
    }
    return sha256(Json(Json::Object{{"fleet_id", fleet.fleet_id},
                                    {"members", std::move(members)},
                                    {"scene_digest", fleet.scene_digest}})
                      .dump(false));
}

std::string fleet_reservation_identity(const FleetSnapshot& fleet, const FleetReservation& reservation) {
    return sha256(Json(Json::Object{
                           {"begin_tick", std::to_string(reservation.begin_tick)},
                           {"deployment_id", reservation.deployment_id},
                           {"end_tick", std::to_string(reservation.end_tick)},
                           {"fleet_snapshot_id", fleet.id},
                           {"occupancy", workspace_aabb_json(reservation.occupancy)},
                           {"separation_margin", reservation.separation_margin},
                           {"source_artifact_id", reservation.source_artifact_id},
                       })
                      .dump(false));
}

std::string fleet_schedule_identity(const FleetScheduleReport& report) {
    Json::Array reservations;
    for (const auto& reservation : report.reservations)
        reservations.emplace_back(reservation.id);
    Json::Array conflicts;
    for (const auto& conflict : report.conflicts) {
        conflicts.emplace_back(Json::Object{
            {"clearance_lower_bound", conflict.clearance_lower_bound},
            {"first_reservation_id", conflict.first_reservation_id},
            {"reason", static_cast<int>(conflict.reason)},
            {"required_margin", conflict.required_margin},
            {"second_reservation_id", conflict.second_reservation_id},
        });
    }
    return sha256(Json(Json::Object{
                           {"conflicts", std::move(conflicts)},
                           {"fleet_snapshot_id", report.fleet_snapshot_id},
                           {"pair_evaluations", std::to_string(report.pair_evaluations)},
                           {"reservations", std::move(reservations)},
                           {"status", static_cast<int>(report.status)},
                       })
                      .dump(false));
}

std::string fleet_schedule_version_identity(const FleetScheduleVersion& version) {
    return sha256(Json(Json::Object{
                           {"fleet_snapshot_id", version.fleet.id},
                           {"memory_id", version.memory_id},
                           {"parent_id", version.parent_id},
                           {"report_id", version.report.id},
                           {"sequence", std::to_string(version.sequence)},
                       })
                      .dump(false));
}

Result<void> validate_fleet_snapshot(const FleetSnapshot& fleet) {
    if (!valid_fleet_snapshot(fleet)) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "fleet snapshot content or identity is invalid", fleet.id);
    }
    return Result<void>::success();
}

Result<void> validate_fleet_schedule_report(const FleetSnapshot& fleet, const FleetScheduleReport& report,
                                            std::size_t maximum_pair_evaluations) {
    if (!valid_fleet_snapshot(fleet) || maximum_pair_evaluations == 0 ||
        report.fleet_snapshot_id != fleet.id || !valid_sha256(report.id) ||
        (report.status != FleetScheduleStatus::ConflictFreeUnderDeclaredEnvelopes &&
         report.status != FleetScheduleStatus::Conflicted) ||
        report.pair_evaluations > maximum_pair_evaluations) {
        return Result<void>::failure(StatusCode::InvalidArgument, "fleet schedule report metadata is invalid",
                                     report.id);
    }

    auto reservation_less = [](const FleetReservation& first, const FleetReservation& second) {
        if (first.begin_tick != second.begin_tick)
            return first.begin_tick < second.begin_tick;
        if (first.end_tick != second.end_tick)
            return first.end_tick < second.end_tick;
        return first.id < second.id;
    };
    std::set<std::string> reservation_ids;
    for (std::size_t index = 0; index < report.reservations.size(); ++index) {
        const auto& reservation = report.reservations[index];
        const auto* member = find_member(fleet, reservation.deployment_id);
        if (member == nullptr || !valid_sha256(reservation.id) ||
            !valid_sha256(reservation.source_artifact_id) || !reservation.occupancy.valid() ||
            !contains_box(member->operating_envelope, reservation.occupancy) ||
            reservation.begin_tick >= reservation.end_tick || !std::isfinite(reservation.separation_margin) ||
            reservation.separation_margin < 0.0 ||
            fleet_reservation_identity(fleet, reservation) != reservation.id ||
            !reservation_ids.insert(reservation.id).second ||
            (index != 0 && reservation_less(reservation, report.reservations[index - 1]))) {
            return Result<void>::failure(StatusCode::InvalidArgument, "fleet schedule reservation is invalid",
                                         reservation.id);
        }
    }

    std::size_t pair_evaluations = 0;
    std::size_t conflict_index = 0;
    for (std::size_t first_index = 0; first_index < report.reservations.size(); ++first_index) {
        for (std::size_t second_index = first_index + 1; second_index < report.reservations.size();
             ++second_index) {
            const auto& first = report.reservations[first_index];
            const auto& second = report.reservations[second_index];
            if (second.begin_tick >= first.end_tick)
                break;
            if (!time_overlaps(first, second))
                continue;
            if (pair_evaluations >= maximum_pair_evaluations) {
                return Result<void>::failure(StatusCode::ResourceLimit,
                                             "fleet schedule pair count exceeds validation limit");
            }
            ++pair_evaluations;
            FleetConflict expected;
            expected.first_reservation_id = first.id;
            expected.second_reservation_id = second.id;
            expected.clearance_lower_bound = first.occupancy.distance_lower_bound(second.occupancy);
            expected.required_margin = std::max(first.separation_margin, second.separation_margin);
            bool has_conflict = true;
            if (first.deployment_id == second.deployment_id) {
                expected.reason = FleetConflictReason::DuplicateRobotWindow;
            } else if (first.occupancy.overlaps(second.occupancy)) {
                expected.reason = FleetConflictReason::WorkspaceOverlap;
            } else if (expected.clearance_lower_bound < expected.required_margin) {
                expected.reason = FleetConflictReason::SeparationMarginViolated;
            } else {
                has_conflict = false;
            }
            if (!has_conflict)
                continue;
            if (conflict_index >= report.conflicts.size()) {
                return Result<void>::failure(StatusCode::InvalidArgument,
                                             "fleet schedule conflict list is incomplete", report.id);
            }
            const auto& actual = report.conflicts[conflict_index++];
            if (actual.first_reservation_id != expected.first_reservation_id ||
                actual.second_reservation_id != expected.second_reservation_id ||
                actual.reason != expected.reason ||
                actual.clearance_lower_bound != expected.clearance_lower_bound ||
                actual.required_margin != expected.required_margin) {
                return Result<void>::failure(StatusCode::InvalidArgument,
                                             "fleet schedule conflict content is inconsistent", report.id);
            }
        }
    }
    const auto expected_status = report.conflicts.empty()
                                     ? FleetScheduleStatus::ConflictFreeUnderDeclaredEnvelopes
                                     : FleetScheduleStatus::Conflicted;
    if (pair_evaluations != report.pair_evaluations || conflict_index != report.conflicts.size() ||
        report.status != expected_status || fleet_schedule_identity(report) != report.id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "fleet schedule report identity or derived values are invalid",
                                     report.id);
    }
    return Result<void>::success();
}

} // namespace internal

Result<MemoryArtifact> SafetyMemory::register_artifact(MemoryArtifactInput input,
                                                       std::size_t maximum_artifacts,
                                                       std::size_t maximum_events) {
    if (maximum_artifacts == 0 || maximum_events == 0) {
        return Result<MemoryArtifact>::failure(StatusCode::InvalidArgument,
                                               "safety memory limits must be positive");
    }
    auto tag_status = normalize_tags(input.tags);
    if (!tag_status)
        return tag_status.error();
    MemoryArtifact artifact;
    artifact.type = input.type;
    artifact.state = MemoryArtifactState::Active;
    artifact.deployment_id = std::move(input.deployment_id);
    artifact.robot_digest = std::move(input.robot_digest);
    artifact.scene_digest = std::move(input.scene_digest);
    artifact.task_id = std::move(input.task_id);
    artifact.content_digest = std::move(input.content_digest);
    artifact.locator = std::move(input.locator);
    artifact.evidence = input.evidence;
    artifact.tags = std::move(input.tags);
    artifact.generation = 1;
    artifact.registered_sequence = next_sequence_;
    artifact.id = internal::memory_artifact_identity(artifact);
    auto status = internal::validate_memory_artifact(artifact);
    if (!status)
        return status.error();

    auto position = find_artifact(artifacts_, artifact.id);
    if (position != artifacts_.end() && position->id == artifact.id)
        return *position;
    if (artifacts_.size() >= maximum_artifacts || events_.size() >= maximum_events) {
        return Result<MemoryArtifact>::failure(StatusCode::ResourceLimit,
                                               "safety memory registration exceeds configured limits");
    }
    auto event = make_event(next_sequence_, MemoryEventType::Registered, artifact,
                            MemoryArtifactState::Active, artifact.task_id, "registered");
    artifacts_.insert(position, artifact);
    events_.push_back(std::move(event));
    ++next_sequence_;
    return artifact;
}

Result<MemoryArtifact> SafetyMemory::transition(const std::string& artifact_id,
                                                std::uint64_t expected_generation,
                                                MemoryArtifactState target_state, std::string detail,
                                                std::size_t maximum_events) {
    if (!internal::valid_sha256(artifact_id) || !valid_artifact_state(target_state) || detail.empty() ||
        detail.size() > kMaximumDetailBytes || contains_control_character(detail) || maximum_events == 0) {
        return Result<MemoryArtifact>::failure(StatusCode::InvalidArgument,
                                               "memory transition request is invalid");
    }
    auto position = find_artifact(artifacts_, artifact_id);
    if (position == artifacts_.end() || position->id != artifact_id) {
        return Result<MemoryArtifact>::failure(StatusCode::InvalidArgument, "memory artifact does not exist",
                                               artifact_id);
    }
    if (position->generation != expected_generation) {
        return Result<MemoryArtifact>::failure(StatusCode::IdentityMismatch,
                                               "memory artifact generation does not match", artifact_id);
    }
    if (!transition_allowed(position->state, target_state)) {
        return Result<MemoryArtifact>::failure(
            StatusCode::InvalidArgument, "memory artifact state transition is not allowed", artifact_id);
    }
    if (events_.size() >= maximum_events) {
        return Result<MemoryArtifact>::failure(StatusCode::ResourceLimit,
                                               "safety memory event limit reached");
    }
    const auto previous = position->state;
    position->state = target_state;
    ++position->generation;
    events_.push_back(make_event(next_sequence_, MemoryEventType::StateTransition, *position, previous,
                                 position->task_id, std::move(detail)));
    ++next_sequence_;
    return *position;
}

Result<std::size_t> SafetyMemory::invalidate_scene(const std::string& deployment_id,
                                                   const std::string& scene_digest, std::string detail,
                                                   std::size_t maximum_events) {
    if (!valid_identifier(deployment_id) || !internal::valid_sha256(scene_digest) || detail.empty() ||
        detail.size() > kMaximumDetailBytes || contains_control_character(detail) || maximum_events == 0) {
        return Result<std::size_t>::failure(StatusCode::InvalidArgument,
                                            "scene invalidation request is invalid");
    }
    std::size_t count = 0;
    for (const auto& artifact : artifacts_) {
        if (artifact.state == MemoryArtifactState::Active && artifact.deployment_id == deployment_id &&
            artifact.scene_digest == scene_digest) {
            ++count;
        }
    }
    if (count > maximum_events - std::min(maximum_events, events_.size())) {
        return Result<std::size_t>::failure(StatusCode::ResourceLimit,
                                            "scene invalidation exceeds event limit");
    }
    for (auto& artifact : artifacts_) {
        if (artifact.state != MemoryArtifactState::Active || artifact.deployment_id != deployment_id ||
            artifact.scene_digest != scene_digest) {
            continue;
        }
        const auto previous = artifact.state;
        artifact.state = MemoryArtifactState::Stale;
        ++artifact.generation;
        events_.push_back(make_event(next_sequence_, MemoryEventType::SceneInvalidated, artifact, previous,
                                     artifact.task_id, detail));
        ++next_sequence_;
    }
    return count;
}

Result<std::optional<MemoryArtifact>> SafetyMemory::artifact(const std::string& artifact_id) const {
    if (!internal::valid_sha256(artifact_id)) {
        return Result<std::optional<MemoryArtifact>>::failure(StatusCode::InvalidArgument,
                                                              "memory artifact ID is invalid");
    }
    const auto position = find_artifact(artifacts_, artifact_id);
    if (position == artifacts_.end() || position->id != artifact_id)
        return std::optional<MemoryArtifact>{};
    return std::optional<MemoryArtifact>{*position};
}

Result<MemoryReuseCandidate> SafetyMemory::assess_reuse(const std::string& artifact_id,
                                                        const MemoryReuseQuery& query) const {
    auto query_status = validate_reuse_query(query);
    if (!query_status)
        return query_status.error();
    auto found = artifact(artifact_id);
    if (!found)
        return found.error();
    if (!found.value()) {
        return Result<MemoryReuseCandidate>::failure(StatusCode::InvalidArgument,
                                                     "memory artifact does not exist", artifact_id);
    }
    return assess(*found.value(), query);
}

Result<std::vector<MemoryReuseCandidate>> SafetyMemory::query_reuse(const MemoryReuseQuery& query) const {
    auto query_status = validate_reuse_query(query);
    if (!query_status)
        return query_status.error();
    std::vector<MemoryReuseCandidate> result;
    for (const auto& candidate : artifacts_) {
        auto assessed = assess(candidate, query);
        if (assessed.disposition == ReuseDisposition::Direct ||
            (query.include_revalidation_candidates &&
             assessed.disposition == ReuseDisposition::RequiresRevalidation)) {
            result.push_back(std::move(assessed));
            if (result.size() > query.maximum_results) {
                return Result<std::vector<MemoryReuseCandidate>>::failure(
                    StatusCode::ResourceLimit, "memory reuse result count exceeds configured limit");
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& first, const auto& second) {
        if (first.disposition != second.disposition)
            return first.disposition < second.disposition;
        if (first.cross_task != second.cross_task)
            return !first.cross_task;
        if (first.artifact.evidence != second.artifact.evidence)
            return first.artifact.evidence > second.artifact.evidence;
        if (first.artifact.task_id != second.artifact.task_id)
            return first.artifact.task_id < second.artifact.task_id;
        return first.artifact.id < second.artifact.id;
    });
    return result;
}

Result<void> SafetyMemory::record_reuse(const std::string& artifact_id, const MemoryReuseQuery& query,
                                        std::string detail, std::size_t maximum_events) {
    auto candidate = assess_reuse(artifact_id, query);
    if (!candidate)
        return candidate.error();
    if (candidate.value().disposition != ReuseDisposition::Direct) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "only direct-reuse artifacts can be recorded", artifact_id);
    }
    if (detail.empty() || detail.size() > kMaximumDetailBytes || contains_control_character(detail) ||
        maximum_events == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument, "reuse audit detail is invalid");
    }
    if (events_.size() >= maximum_events) {
        return Result<void>::failure(StatusCode::ResourceLimit, "safety memory event limit reached");
    }
    const auto& source = candidate.value().artifact;
    events_.push_back(make_event(next_sequence_, MemoryEventType::ReuseRecorded, source, source.state,
                                 query.target_task_id, std::move(detail)));
    ++next_sequence_;
    return Result<void>::success();
}

SafetyMemorySummary SafetyMemory::summary() const noexcept {
    SafetyMemorySummary result;
    result.artifacts = artifacts_.size();
    result.events = events_.size();
    for (const auto& artifact : artifacts_) {
        switch (artifact.state) {
        case MemoryArtifactState::Active:
            ++result.active;
            break;
        case MemoryArtifactState::Stale:
            ++result.stale;
            break;
        case MemoryArtifactState::Quarantined:
            ++result.quarantined;
            break;
        case MemoryArtifactState::Retired:
            ++result.retired;
            break;
        }
    }
    result.recorded_reuses = static_cast<std::uint64_t>(
        std::count_if(events_.begin(), events_.end(),
                      [](const MemoryEvent& event) { return event.type == MemoryEventType::ReuseRecorded; }));
    return result;
}

bool SafetyMemory::valid() const {
    if (next_sequence_ == 0 || artifacts_.size() > 1'000'000 || events_.size() > 4'000'000)
        return false;
    std::map<std::string, MemoryArtifactState> replay_state;
    std::map<std::string, std::uint64_t> replay_generation;
    std::string previous_artifact_id;
    for (const auto& artifact : artifacts_) {
        if (!internal::validate_memory_artifact(artifact) ||
            (!previous_artifact_id.empty() && previous_artifact_id >= artifact.id)) {
            return false;
        }
        previous_artifact_id = artifact.id;
    }
    std::uint64_t expected_sequence = 1;
    std::set<std::string> event_ids;
    for (const auto& event : events_) {
        if (!internal::validate_memory_event(event) || event.sequence != expected_sequence ||
            !event_ids.insert(event.id).second) {
            return false;
        }
        const auto stored = find_artifact(artifacts_, event.artifact_id);
        if (stored == artifacts_.end() || stored->id != event.artifact_id)
            return false;
        if (event.type == MemoryEventType::Registered) {
            if (replay_state.contains(event.artifact_id) ||
                event.previous_state != MemoryArtifactState::Active ||
                event.current_state != MemoryArtifactState::Active ||
                stored->registered_sequence != event.sequence || event.task_id != stored->task_id ||
                event.detail != "registered") {
                return false;
            }
            replay_state[event.artifact_id] = MemoryArtifactState::Active;
            replay_generation[event.artifact_id] = 1;
        } else {
            const auto state = replay_state.find(event.artifact_id);
            if (state == replay_state.end() || state->second != event.previous_state)
                return false;
            if (event.type == MemoryEventType::ReuseRecorded) {
                if (event.current_state != state->second)
                    return false;
            } else {
                if (event.task_id != stored->task_id ||
                    !transition_allowed(state->second, event.current_state))
                    return false;
                state->second = event.current_state;
                ++replay_generation[event.artifact_id];
            }
        }
        ++expected_sequence;
    }
    if (next_sequence_ != expected_sequence || replay_state.size() != artifacts_.size())
        return false;
    for (const auto& artifact : artifacts_) {
        if (replay_state[artifact.id] != artifact.state ||
            replay_generation[artifact.id] != artifact.generation) {
            return false;
        }
    }
    return true;
}

std::string SafetyMemory::identity() const {
    return internal::sha256(safety_memory_identity_json(*this).dump(false));
}

Result<void> SafetyMemory::save(const std::filesystem::path& directory, const SaveOptions& options) const {
    return save_safety_memory_directory(*this, directory, options);
}

Result<SafetyMemory> SafetyMemory::load(const std::filesystem::path& directory,
                                        const SafetyMemoryLoadOptions& options) {
    return load_safety_memory_directory(directory, options);
}

Result<FleetSnapshot> make_fleet_snapshot(std::string fleet_id, std::string scene_digest,
                                          std::vector<FleetMember> members) {
    if (!valid_identifier(fleet_id) || !internal::valid_sha256(scene_digest) || members.empty() ||
        members.size() > kMaximumFleetMembers) {
        return Result<FleetSnapshot>::failure(StatusCode::InvalidArgument, "fleet snapshot input is invalid");
    }
    std::sort(members.begin(), members.end(), [](const auto& first, const auto& second) {
        return first.deployment_id < second.deployment_id;
    });
    for (std::size_t index = 0; index < members.size(); ++index) {
        const auto& member = members[index];
        if (!valid_identifier(member.deployment_id) || !internal::valid_sha256(member.robot_digest) ||
            !member.operating_envelope.valid() ||
            (index != 0 && members[index - 1].deployment_id == member.deployment_id)) {
            return Result<FleetSnapshot>::failure(StatusCode::InvalidArgument,
                                                  "fleet member is invalid or duplicated");
        }
    }
    FleetSnapshot result;
    result.fleet_id = std::move(fleet_id);
    result.scene_digest = std::move(scene_digest);
    result.members = std::move(members);
    result.id = internal::fleet_snapshot_identity(result);
    return result;
}

Result<FleetReservation> make_fleet_reservation(const FleetSnapshot& fleet, const SafetyMemory& memory,
                                                std::string deployment_id, std::string source_artifact_id,
                                                WorkspaceAabb occupancy, std::uint64_t begin_tick,
                                                std::uint64_t end_tick, double separation_margin) {
    if (!valid_fleet_snapshot(fleet)) {
        return Result<FleetReservation>::failure(StatusCode::InvalidArgument, "fleet snapshot is invalid");
    }
    const auto* member = find_member(fleet, deployment_id);
    if (member == nullptr) {
        return Result<FleetReservation>::failure(StatusCode::IdentityMismatch,
                                                 "deployment is not a fleet member", deployment_id);
    }
    if (!occupancy.valid() || !contains_box(member->operating_envelope, occupancy) ||
        begin_tick >= end_tick || !std::isfinite(separation_margin) || separation_margin < 0.0) {
        return Result<FleetReservation>::failure(StatusCode::InvalidArgument,
                                                 "fleet reservation geometry or time window is invalid");
    }
    auto source = memory.artifact(source_artifact_id);
    if (!source)
        return source.error();
    if (!source.value()) {
        return Result<FleetReservation>::failure(StatusCode::InvalidArgument,
                                                 "fleet reservation source artifact does not exist");
    }
    const auto& artifact = *source.value();
    if (artifact.state != MemoryArtifactState::Active || artifact.deployment_id != deployment_id ||
        artifact.robot_digest != member->robot_digest || artifact.scene_digest != fleet.scene_digest ||
        static_cast<int>(artifact.evidence) < static_cast<int>(EvidenceLevel::CertifiedRegion) ||
        !source_type_supports_reservation(artifact.type)) {
        return Result<FleetReservation>::failure(
            StatusCode::IdentityMismatch,
            "fleet reservation source is inactive, incompatible, or insufficiently certified",
            source_artifact_id);
    }
    FleetReservation result;
    result.deployment_id = std::move(deployment_id);
    result.source_artifact_id = std::move(source_artifact_id);
    result.occupancy = occupancy;
    result.begin_tick = begin_tick;
    result.end_tick = end_tick;
    result.separation_margin = separation_margin;
    result.id = internal::fleet_reservation_identity(fleet, result);
    return result;
}

Result<FleetScheduleReport> analyze_fleet_schedule(const FleetSnapshot& fleet, const SafetyMemory& memory,
                                                   std::span<const FleetReservation> reservations,
                                                   const FleetScheduleOptions& options) {
    if (!valid_fleet_snapshot(fleet)) {
        return Result<FleetScheduleReport>::failure(StatusCode::InvalidArgument, "fleet snapshot is invalid");
    }
    if (options.maximum_reservations == 0 || options.maximum_pair_evaluations == 0) {
        return Result<FleetScheduleReport>::failure(StatusCode::InvalidArgument,
                                                    "fleet schedule limits must be positive");
    }
    if (reservations.size() > options.maximum_reservations) {
        return Result<FleetScheduleReport>::failure(StatusCode::ResourceLimit,
                                                    "fleet reservation count exceeds limit");
    }
    if (options.cancellation.cancelled()) {
        return Result<FleetScheduleReport>::failure(StatusCode::Cancelled,
                                                    "fleet schedule analysis cancelled");
    }
    FleetScheduleReport report;
    report.fleet_snapshot_id = fleet.id;
    report.reservations.assign(reservations.begin(), reservations.end());
    std::sort(report.reservations.begin(), report.reservations.end(),
              [](const auto& first, const auto& second) {
                  if (first.begin_tick != second.begin_tick)
                      return first.begin_tick < second.begin_tick;
                  if (first.end_tick != second.end_tick)
                      return first.end_tick < second.end_tick;
                  return first.id < second.id;
              });
    std::set<std::string> reservation_ids;
    for (const auto& reservation : report.reservations) {
        if (options.cancellation.cancelled()) {
            return Result<FleetScheduleReport>::failure(StatusCode::Cancelled,
                                                        "fleet schedule analysis cancelled");
        }
        const auto* member = find_member(fleet, reservation.deployment_id);
        if (member == nullptr || !internal::valid_sha256(reservation.source_artifact_id) ||
            !reservation.occupancy.valid() ||
            !contains_box(member->operating_envelope, reservation.occupancy) ||
            reservation.begin_tick >= reservation.end_tick || !std::isfinite(reservation.separation_margin) ||
            reservation.separation_margin < 0.0 ||
            internal::fleet_reservation_identity(fleet, reservation) != reservation.id ||
            !reservation_ids.insert(reservation.id).second) {
            return Result<FleetScheduleReport>::failure(StatusCode::InvalidArgument,
                                                        "fleet reservation is invalid or duplicated");
        }
        auto source = memory.artifact(reservation.source_artifact_id);
        if (!source)
            return source.error();
        if (!source.value() || source.value()->state != MemoryArtifactState::Active ||
            source.value()->deployment_id != reservation.deployment_id ||
            source.value()->robot_digest != member->robot_digest ||
            source.value()->scene_digest != fleet.scene_digest ||
            static_cast<int>(source.value()->evidence) < static_cast<int>(EvidenceLevel::CertifiedRegion) ||
            !source_type_supports_reservation(source.value()->type)) {
            return Result<FleetScheduleReport>::failure(
                StatusCode::IdentityMismatch, "fleet reservation source is no longer active and compatible",
                reservation.source_artifact_id);
        }
    }
    for (std::size_t first_index = 0; first_index < report.reservations.size(); ++first_index) {
        for (std::size_t second_index = first_index + 1; second_index < report.reservations.size();
             ++second_index) {
            const auto& first = report.reservations[first_index];
            const auto& second = report.reservations[second_index];
            if (second.begin_tick >= first.end_tick)
                break;
            if (!time_overlaps(first, second))
                continue;
            if (options.cancellation.cancelled()) {
                return Result<FleetScheduleReport>::failure(StatusCode::Cancelled,
                                                            "fleet schedule analysis cancelled");
            }
            if (report.pair_evaluations >= options.maximum_pair_evaluations) {
                return Result<FleetScheduleReport>::failure(StatusCode::ResourceLimit,
                                                            "fleet pair-evaluation limit reached");
            }
            ++report.pair_evaluations;
            FleetConflict conflict;
            conflict.first_reservation_id = first.id;
            conflict.second_reservation_id = second.id;
            conflict.clearance_lower_bound = first.occupancy.distance_lower_bound(second.occupancy);
            conflict.required_margin = std::max(first.separation_margin, second.separation_margin);
            if (first.deployment_id == second.deployment_id) {
                conflict.reason = FleetConflictReason::DuplicateRobotWindow;
                report.conflicts.push_back(std::move(conflict));
            } else if (first.occupancy.overlaps(second.occupancy)) {
                conflict.reason = FleetConflictReason::WorkspaceOverlap;
                report.conflicts.push_back(std::move(conflict));
            } else if (conflict.clearance_lower_bound < conflict.required_margin) {
                conflict.reason = FleetConflictReason::SeparationMarginViolated;
                report.conflicts.push_back(std::move(conflict));
            }
        }
    }
    report.status = report.conflicts.empty() ? FleetScheduleStatus::ConflictFreeUnderDeclaredEnvelopes
                                             : FleetScheduleStatus::Conflicted;
    report.id = internal::fleet_schedule_identity(report);
    return report;
}

std::string memory_artifact_type_name(MemoryArtifactType type) {
    switch (type) {
    case MemoryArtifactType::SafeAtlas:
        return "safe_atlas";
    case MemoryArtifactType::RegionDatabase:
        return "region_database";
    case MemoryArtifactType::SafeCorridor:
        return "safe_corridor";
    case MemoryArtifactType::TrajectoryAudit:
        return "trajectory_audit";
    case MemoryArtifactType::PolicyFeedback:
        return "policy_feedback";
    case MemoryArtifactType::RuntimeTrace:
        return "runtime_trace";
    case MemoryArtifactType::FleetSchedule:
        return "fleet_schedule";
    }
    return "unknown";
}

std::string memory_artifact_state_name(MemoryArtifactState state) {
    switch (state) {
    case MemoryArtifactState::Active:
        return "active";
    case MemoryArtifactState::Stale:
        return "stale";
    case MemoryArtifactState::Quarantined:
        return "quarantined";
    case MemoryArtifactState::Retired:
        return "retired";
    }
    return "unknown";
}

std::string memory_event_type_name(MemoryEventType type) {
    switch (type) {
    case MemoryEventType::Registered:
        return "registered";
    case MemoryEventType::StateTransition:
        return "state_transition";
    case MemoryEventType::ReuseRecorded:
        return "reuse_recorded";
    case MemoryEventType::SceneInvalidated:
        return "scene_invalidated";
    }
    return "unknown";
}

std::string reuse_disposition_name(ReuseDisposition disposition) {
    switch (disposition) {
    case ReuseDisposition::Direct:
        return "direct";
    case ReuseDisposition::RequiresRevalidation:
        return "requires_revalidation";
    case ReuseDisposition::Ineligible:
        return "ineligible";
    }
    return "unknown";
}

std::string fleet_conflict_reason_name(FleetConflictReason reason) {
    switch (reason) {
    case FleetConflictReason::DuplicateRobotWindow:
        return "duplicate_robot_window";
    case FleetConflictReason::WorkspaceOverlap:
        return "workspace_overlap";
    case FleetConflictReason::SeparationMarginViolated:
        return "separation_margin_violated";
    }
    return "unknown";
}

std::string fleet_schedule_status_name(FleetScheduleStatus status) {
    switch (status) {
    case FleetScheduleStatus::ConflictFreeUnderDeclaredEnvelopes:
        return "conflict_free_under_declared_envelopes";
    case FleetScheduleStatus::Conflicted:
        return "conflicted";
    }
    return "unknown";
}

} // namespace rbfsafe
