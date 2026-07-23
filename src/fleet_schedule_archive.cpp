#include <rbfsafe/memory.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/memory.h"
#include "internal/sha256.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 256;

bool valid_identifier(const std::string& value) {
    return !value.empty() && value.size() <= kMaximumIdentifierBytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

} // namespace

Result<FleetScheduleArchive> FleetScheduleArchive::create(std::string fleet_id) {
    if (!valid_identifier(fleet_id)) {
        return Result<FleetScheduleArchive>::failure(StatusCode::InvalidArgument,
                                                     "fleet archive ID is invalid");
    }
    FleetScheduleArchive result;
    result.fleet_id_ = std::move(fleet_id);
    return result;
}

bool FleetScheduleArchive::valid() const {
    if (!valid_identifier(fleet_id_) || (versions_.empty() != current_version_id_.empty()))
        return false;
    std::set<std::string> ids;
    for (std::size_t index = 0; index < versions_.size(); ++index) {
        const auto& candidate = versions_[index];
        const auto expected_parent = index == 0 ? std::string{} : versions_[index - 1].id;
        if (candidate.sequence != static_cast<std::uint64_t>(index) ||
            candidate.parent_id != expected_parent || candidate.fleet.fleet_id != fleet_id_ ||
            candidate.report.fleet_snapshot_id != candidate.fleet.id ||
            !internal::valid_sha256(candidate.id) || !internal::valid_sha256(candidate.memory_id) ||
            internal::fleet_schedule_version_identity(candidate) != candidate.id ||
            !ids.insert(candidate.id).second) {
            return false;
        }
        const auto pair_limit = std::max<std::size_t>(candidate.report.pair_evaluations, 1);
        if (!internal::validate_fleet_schedule_report(candidate.fleet, candidate.report, pair_limit))
            return false;
    }
    return versions_.empty() || current_version_id_ == versions_.back().id;
}

Result<FleetScheduleVersion> FleetScheduleArchive::current_version() const {
    if (versions_.empty()) {
        return Result<FleetScheduleVersion>::failure(StatusCode::InvalidArgument,
                                                     "fleet schedule archive has no versions");
    }
    return versions_.back();
}

Result<FleetScheduleVersion> FleetScheduleArchive::version(const std::string& version_id) const {
    if (!internal::valid_sha256(version_id)) {
        return Result<FleetScheduleVersion>::failure(StatusCode::InvalidArgument,
                                                     "fleet schedule version ID is invalid");
    }
    const auto found = std::find_if(versions_.begin(), versions_.end(),
                                    [&](const auto& candidate) { return candidate.id == version_id; });
    if (found == versions_.end()) {
        return Result<FleetScheduleVersion>::failure(StatusCode::InvalidArgument,
                                                     "fleet schedule version is not registered", version_id);
    }
    return *found;
}

Result<FleetScheduleVersion> FleetScheduleArchive::publish(const FleetSnapshot& fleet,
                                                           const SafetyMemory& memory,
                                                           std::span<const FleetReservation> reservations,
                                                           const std::string& expected_current_version_id,
                                                           const FleetScheduleOptions& schedule_options,
                                                           std::size_t maximum_versions) {
    if (!valid() || maximum_versions == 0 || fleet.fleet_id != fleet_id_) {
        return Result<FleetScheduleVersion>::failure(StatusCode::InvalidArgument,
                                                     "fleet schedule publication input is invalid");
    }
    if (versions_.empty()) {
        if (!expected_current_version_id.empty()) {
            return Result<FleetScheduleVersion>::failure(
                StatusCode::IdentityMismatch, "empty fleet archive requires an empty expected head");
        }
    } else if (!internal::valid_sha256(expected_current_version_id) ||
               expected_current_version_id != current_version_id_) {
        return Result<FleetScheduleVersion>::failure(
            StatusCode::IdentityMismatch, "fleet schedule archive head changed", current_version_id_);
    }
    auto report = analyze_fleet_schedule(fleet, memory, reservations, schedule_options);
    if (!report)
        return report.error();
    const auto memory_id = memory.identity();
    if (!versions_.empty() && versions_.back().memory_id == memory_id &&
        versions_.back().fleet.id == fleet.id && versions_.back().report.id == report.value().id) {
        return versions_.back();
    }
    if (versions_.size() >= maximum_versions ||
        versions_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
        return Result<FleetScheduleVersion>::failure(StatusCode::ResourceLimit,
                                                     "fleet schedule archive version limit reached");
    }
    FleetScheduleVersion result;
    result.sequence = static_cast<std::uint64_t>(versions_.size());
    result.parent_id = current_version_id_;
    result.memory_id = memory_id;
    result.fleet = fleet;
    result.report = std::move(report).value();
    result.id = internal::fleet_schedule_version_identity(result);
    versions_.push_back(result);
    current_version_id_ = result.id;
    return result;
}

Result<FleetScheduleReport> FleetScheduleArchive::verify_version(const std::string& version_id,
                                                                 const FleetSnapshot& fleet,
                                                                 const SafetyMemory& memory,
                                                                 const FleetScheduleOptions& options) const {
    auto stored = version(version_id);
    if (!stored)
        return stored.error();
    if (!internal::validate_fleet_snapshot(fleet) || fleet.id != stored.value().fleet.id ||
        memory.identity() != stored.value().memory_id) {
        return Result<FleetScheduleReport>::failure(
            StatusCode::IdentityMismatch,
            "fleet schedule version is incompatible with the supplied fleet or memory", version_id);
    }
    auto replayed = analyze_fleet_schedule(fleet, memory, stored.value().report.reservations, options);
    if (!replayed)
        return replayed.error();
    if (replayed.value().id != stored.value().report.id) {
        return Result<FleetScheduleReport>::failure(
            StatusCode::CorruptData, "fleet schedule replay differs from stored report", version_id);
    }
    return replayed;
}

} // namespace rbfsafe
