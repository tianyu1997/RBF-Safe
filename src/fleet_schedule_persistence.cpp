#include <rbfsafe/memory.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/memory.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kSchema = 1;
constexpr std::size_t kMaximumStringBytes = 4096;
constexpr std::size_t kMaximumExactJsonInteger = sizeof(std::size_t) < sizeof(std::uint64_t)
                                                     ? std::numeric_limits<std::size_t>::max()
                                                     : static_cast<std::size_t>(9'007'199'254'740'991ULL);

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json aabb_json(const WorkspaceAabb& box) {
    internal::Json::Array lower;
    internal::Json::Array upper;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        lower.emplace_back(box.lower[axis]);
        upper.emplace_back(box.upper[axis]);
    }
    return internal::Json::Object{{"lower", std::move(lower)}, {"upper", std::move(upper)}};
}

internal::Json snapshot_json(const FleetSnapshot& fleet) {
    internal::Json::Array members;
    members.reserve(fleet.members.size());
    for (const auto& member : fleet.members) {
        members.emplace_back(internal::Json::Object{
            {"deployment_id", member.deployment_id},
            {"operating_envelope", aabb_json(member.operating_envelope)},
            {"robot_digest", member.robot_digest},
        });
    }
    return internal::Json::Object{
        {"fleet_id", fleet.fleet_id},
        {"id", fleet.id},
        {"members", std::move(members)},
        {"scene_digest", fleet.scene_digest},
    };
}

internal::Json reservation_json(const FleetReservation& reservation) {
    return internal::Json::Object{
        {"begin_tick", std::to_string(reservation.begin_tick)},
        {"deployment_id", reservation.deployment_id},
        {"end_tick", std::to_string(reservation.end_tick)},
        {"id", reservation.id},
        {"occupancy", aabb_json(reservation.occupancy)},
        {"separation_margin", reservation.separation_margin},
        {"source_artifact_id", reservation.source_artifact_id},
    };
}

internal::Json conflict_json(const FleetConflict& conflict) {
    return internal::Json::Object{
        {"clearance_lower_bound", conflict.clearance_lower_bound},
        {"first_reservation_id", conflict.first_reservation_id},
        {"reason", static_cast<int>(conflict.reason)},
        {"required_margin", conflict.required_margin},
        {"second_reservation_id", conflict.second_reservation_id},
    };
}

internal::Json report_json(const FleetScheduleReport& report) {
    internal::Json::Array reservations;
    reservations.reserve(report.reservations.size());
    for (const auto& reservation : report.reservations)
        reservations.emplace_back(reservation_json(reservation));
    internal::Json::Array conflicts;
    conflicts.reserve(report.conflicts.size());
    for (const auto& conflict : report.conflicts)
        conflicts.emplace_back(conflict_json(conflict));
    return internal::Json::Object{
        {"conflicts", std::move(conflicts)},
        {"fleet_snapshot_id", report.fleet_snapshot_id},
        {"id", report.id},
        {"pair_evaluations", std::to_string(report.pair_evaluations)},
        {"reservations", std::move(reservations)},
        {"status", static_cast<int>(report.status)},
    };
}

internal::Json version_json(const FleetScheduleVersion& version) {
    return internal::Json::Object{
        {"fleet", snapshot_json(version.fleet)}, {"id", version.id},
        {"memory_id", version.memory_id},        {"parent_id", version.parent_id},
        {"report", report_json(version.report)}, {"sequence", std::to_string(version.sequence)},
    };
}

internal::Json payload_json(const FleetScheduleArchive& archive) {
    internal::Json::Array versions;
    versions.reserve(archive.versions().size());
    for (const auto& version : archive.versions())
        versions.emplace_back(version_json(version));
    return internal::Json::Object{
        {"fleet_id", archive.fleet_id()},
        {"format", "rbfsafe-fleet-schedule-records"},
        {"schema", static_cast<double>(kSchema)},
        {"versions", std::move(versions)},
    };
}

Result<internal::Json> read_bounded_json(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error)
        return Result<internal::Json>::failure(StatusCode::IoError, "failed to inspect fleet archive file",
                                               path.string());
    if (bytes > maximum_bytes) {
        return Result<internal::Json>::failure(StatusCode::ResourceLimit,
                                               "fleet archive file exceeds configured limit", path.string());
    }
    return internal::read_json_file(path);
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 bool allow_empty = false) {
    if (!object.is_object())
        return Result<std::string>::failure(StatusCode::CorruptData, "fleet archive record is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData, "fleet archive string field is invalid",
                                            std::string(key));
    }
    return value->as_string();
}

Result<std::size_t> size_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object())
        return Result<std::size_t>::failure(StatusCode::CorruptData, "fleet archive record is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || std::floor(value->as_number()) != value->as_number() ||
        value->as_number() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "fleet archive numeric field is invalid",
                                            std::string(key));
    }
    return static_cast<std::size_t>(value->as_number());
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData,
                                              "fleet archive decimal field is invalid", std::string(key));
    }
    return result;
}

Result<std::size_t> decimal_size_field(const internal::Json& object, std::string_view key) {
    auto value = decimal_field(object, key);
    if (!value)
        return value.error();
    if (value.value() > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return Result<std::size_t>::failure(
            StatusCode::ResourceLimit, "fleet archive size field exceeds platform limit", std::string(key));
    }
    return static_cast<std::size_t>(value.value());
}

Result<double> double_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object())
        return Result<double>::failure(StatusCode::CorruptData, "fleet archive record is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData, "fleet archive scalar is invalid",
                                       std::string(key));
    }
    return value->as_number();
}

Result<WorkspaceAabb> decode_aabb(const internal::Json& object) {
    if (!object.is_object())
        return Result<WorkspaceAabb>::failure(StatusCode::CorruptData, "fleet archive AABB is invalid");
    const auto* lower = object.find("lower");
    const auto* upper = object.find("upper");
    if (lower == nullptr || upper == nullptr || !lower->is_array() || !upper->is_array() ||
        lower->as_array().size() != 3 || upper->as_array().size() != 3) {
        return Result<WorkspaceAabb>::failure(StatusCode::CorruptData, "fleet archive AABB is incomplete");
    }
    WorkspaceAabb result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto& low = lower->as_array()[axis];
        const auto& high = upper->as_array()[axis];
        if (!low.is_number() || !high.is_number() || !std::isfinite(low.as_number()) ||
            !std::isfinite(high.as_number())) {
            return Result<WorkspaceAabb>::failure(StatusCode::CorruptData,
                                                  "fleet archive AABB coordinate is invalid");
        }
        result.lower[axis] = low.as_number();
        result.upper[axis] = high.as_number();
    }
    if (!result.valid())
        return Result<WorkspaceAabb>::failure(StatusCode::CorruptData, "fleet archive AABB is empty");
    return result;
}

Result<FleetSnapshot> decode_snapshot(const internal::Json& object, std::size_t maximum_members) {
    auto id = string_field(object, "id");
    auto fleet_id = string_field(object, "fleet_id");
    auto scene_digest = string_field(object, "scene_digest");
    const auto* members = object.is_object() ? object.find("members") : nullptr;
    if (!id || !fleet_id || !scene_digest || members == nullptr || !members->is_array()) {
        return Result<FleetSnapshot>::failure(StatusCode::CorruptData,
                                              "fleet archive snapshot is incomplete");
    }
    if (members->as_array().size() > maximum_members) {
        return Result<FleetSnapshot>::failure(StatusCode::ResourceLimit,
                                              "fleet archive member count exceeds limit");
    }
    FleetSnapshot result;
    result.id = std::move(id).value();
    result.fleet_id = std::move(fleet_id).value();
    result.scene_digest = std::move(scene_digest).value();
    result.members.reserve(members->as_array().size());
    for (const auto& item : members->as_array()) {
        auto deployment_id = string_field(item, "deployment_id");
        auto robot_digest = string_field(item, "robot_digest");
        const auto* envelope = item.is_object() ? item.find("operating_envelope") : nullptr;
        if (!deployment_id || !robot_digest || envelope == nullptr) {
            return Result<FleetSnapshot>::failure(StatusCode::CorruptData,
                                                  "fleet archive member is incomplete");
        }
        auto box = decode_aabb(*envelope);
        if (!box)
            return box.error();
        result.members.push_back(
            {std::move(deployment_id).value(), std::move(robot_digest).value(), box.value()});
    }
    auto valid = internal::validate_fleet_snapshot(result);
    if (!valid) {
        return Result<FleetSnapshot>::failure(StatusCode::CorruptData, valid.error().message,
                                              valid.error().context);
    }
    return result;
}

Result<FleetReservation> decode_reservation(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto deployment_id = string_field(object, "deployment_id");
    auto source_id = string_field(object, "source_artifact_id");
    auto begin = decimal_field(object, "begin_tick");
    auto end = decimal_field(object, "end_tick");
    auto margin = double_field(object, "separation_margin");
    const auto* occupancy = object.is_object() ? object.find("occupancy") : nullptr;
    if (!id || !deployment_id || !source_id || !begin || !end || !margin || occupancy == nullptr) {
        return Result<FleetReservation>::failure(StatusCode::CorruptData,
                                                 "fleet archive reservation is incomplete");
    }
    auto box = decode_aabb(*occupancy);
    if (!box)
        return box.error();
    FleetReservation result;
    result.id = std::move(id).value();
    result.deployment_id = std::move(deployment_id).value();
    result.source_artifact_id = std::move(source_id).value();
    result.occupancy = box.value();
    result.begin_tick = begin.value();
    result.end_tick = end.value();
    result.separation_margin = margin.value();
    return result;
}

Result<FleetConflict> decode_conflict(const internal::Json& object) {
    auto first = string_field(object, "first_reservation_id");
    auto second = string_field(object, "second_reservation_id");
    auto reason =
        size_field(object, "reason", static_cast<std::size_t>(FleetConflictReason::SeparationMarginViolated));
    auto clearance = double_field(object, "clearance_lower_bound");
    auto margin = double_field(object, "required_margin");
    if (!first || !second || !reason || !clearance || !margin) {
        return Result<FleetConflict>::failure(StatusCode::CorruptData,
                                              "fleet archive conflict is incomplete");
    }
    FleetConflict result;
    result.first_reservation_id = std::move(first).value();
    result.second_reservation_id = std::move(second).value();
    result.reason = static_cast<FleetConflictReason>(reason.value());
    result.clearance_lower_bound = clearance.value();
    result.required_margin = margin.value();
    return result;
}

Result<FleetScheduleReport> decode_report(const internal::Json& object, const FleetSnapshot& fleet,
                                          std::size_t maximum_reservations, std::size_t maximum_conflicts,
                                          std::size_t maximum_pair_evaluations) {
    auto id = string_field(object, "id");
    auto snapshot_id = string_field(object, "fleet_snapshot_id");
    auto status = size_field(object, "status", static_cast<std::size_t>(FleetScheduleStatus::Conflicted));
    auto pair_evaluations = decimal_size_field(object, "pair_evaluations");
    const auto* reservations = object.is_object() ? object.find("reservations") : nullptr;
    const auto* conflicts = object.is_object() ? object.find("conflicts") : nullptr;
    if (!id || !snapshot_id || !status || !pair_evaluations || reservations == nullptr ||
        conflicts == nullptr || !reservations->is_array() || !conflicts->is_array()) {
        return Result<FleetScheduleReport>::failure(StatusCode::CorruptData,
                                                    "fleet archive report is incomplete");
    }
    if (reservations->as_array().size() > maximum_reservations ||
        conflicts->as_array().size() > maximum_conflicts ||
        pair_evaluations.value() > maximum_pair_evaluations) {
        return Result<FleetScheduleReport>::failure(StatusCode::ResourceLimit,
                                                    "fleet archive report exceeds configured limits");
    }
    FleetScheduleReport result;
    result.id = std::move(id).value();
    result.fleet_snapshot_id = std::move(snapshot_id).value();
    result.status = static_cast<FleetScheduleStatus>(status.value());
    result.pair_evaluations = pair_evaluations.value();
    result.reservations.reserve(reservations->as_array().size());
    for (const auto& item : reservations->as_array()) {
        auto reservation = decode_reservation(item);
        if (!reservation)
            return reservation.error();
        result.reservations.push_back(std::move(reservation).value());
    }
    result.conflicts.reserve(conflicts->as_array().size());
    for (const auto& item : conflicts->as_array()) {
        auto conflict = decode_conflict(item);
        if (!conflict)
            return conflict.error();
        result.conflicts.push_back(std::move(conflict).value());
    }
    auto valid = internal::validate_fleet_schedule_report(fleet, result,
                                                          std::max<std::size_t>(maximum_pair_evaluations, 1));
    if (!valid) {
        const auto code = valid.error().code == StatusCode::ResourceLimit ? StatusCode::ResourceLimit
                                                                          : StatusCode::CorruptData;
        return Result<FleetScheduleReport>::failure(code, valid.error().message, valid.error().context);
    }
    return result;
}

Result<FleetScheduleVersion> decode_version(const internal::Json& object, std::size_t remaining_members,
                                            std::size_t remaining_reservations,
                                            std::size_t remaining_conflicts,
                                            std::size_t remaining_pair_evaluations) {
    auto sequence = decimal_field(object, "sequence");
    auto id = string_field(object, "id");
    auto parent = string_field(object, "parent_id", true);
    auto memory_id = string_field(object, "memory_id");
    const auto* fleet = object.is_object() ? object.find("fleet") : nullptr;
    const auto* report = object.is_object() ? object.find("report") : nullptr;
    if (!sequence || !id || !parent || !memory_id || fleet == nullptr || report == nullptr) {
        return Result<FleetScheduleVersion>::failure(StatusCode::CorruptData,
                                                     "fleet schedule version is incomplete");
    }
    auto snapshot = decode_snapshot(*fleet, remaining_members);
    if (!snapshot)
        return snapshot.error();
    auto decoded_report = decode_report(*report, snapshot.value(), remaining_reservations,
                                        remaining_conflicts, remaining_pair_evaluations);
    if (!decoded_report)
        return decoded_report.error();
    FleetScheduleVersion result;
    result.sequence = sequence.value();
    result.id = std::move(id).value();
    result.parent_id = std::move(parent).value();
    result.memory_id = std::move(memory_id).value();
    result.fleet = std::move(snapshot).value();
    result.report = std::move(decoded_report).value();
    if (!internal::valid_sha256(result.id) || !internal::valid_sha256(result.memory_id) ||
        internal::fleet_schedule_version_identity(result) != result.id) {
        return Result<FleetScheduleVersion>::failure(StatusCode::CorruptData,
                                                     "fleet schedule version identity is invalid", result.id);
    }
    return result;
}

bool exceeds(std::size_t current, std::size_t added, std::size_t maximum) {
    return added > maximum || current > maximum - added;
}

Result<void> publish_directory(const std::filesystem::path& temporary,
                               const std::filesystem::path& destination, bool destination_exists) {
    std::error_code error;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(destination, ".backup-");
        std::filesystem::rename(destination, backup, error);
        if (error)
            return Result<void>::failure(StatusCode::IoError, "failed to stage existing fleet archive");
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish fleet archive");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove_all(backup, ignored);
    }
    return Result<void>::success();
}

} // namespace

Result<void> FleetScheduleArchive::save(const std::filesystem::path& directory,
                                        const SaveOptions& options) const {
    if (!valid())
        return Result<void>::failure(StatusCode::InvalidArgument, "cannot save invalid fleet archive");
    if (directory.empty() || directory == directory.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "fleet archive destination must be a specific directory");
    }
    std::size_t members = 0;
    std::size_t reservations = 0;
    std::size_t conflicts = 0;
    std::size_t pair_evaluations = 0;
    for (const auto& version : versions_) {
        if (exceeds(members, version.fleet.members.size(), std::numeric_limits<std::size_t>::max()) ||
            exceeds(reservations, version.report.reservations.size(),
                    std::numeric_limits<std::size_t>::max()) ||
            exceeds(conflicts, version.report.conflicts.size(), std::numeric_limits<std::size_t>::max()) ||
            exceeds(pair_evaluations, version.report.pair_evaluations,
                    std::numeric_limits<std::size_t>::max())) {
            return Result<void>::failure(StatusCode::ResourceLimit, "fleet archive counts overflow");
        }
        members += version.fleet.members.size();
        reservations += version.report.reservations.size();
        conflicts += version.report.conflicts.size();
        pair_evaluations += version.report.pair_evaluations;
    }
    if (versions_.size() > kMaximumExactJsonInteger || members > kMaximumExactJsonInteger ||
        reservations > kMaximumExactJsonInteger || conflicts > kMaximumExactJsonInteger ||
        pair_evaluations > kMaximumExactJsonInteger) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "fleet archive counts exceed exact JSON integer range");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(directory, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to inspect fleet archive destination");
    if (destination_exists && !options.overwrite)
        return Result<void>::failure(StatusCode::IoError, "fleet archive destination already exists");
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error)
            return Result<void>::failure(StatusCode::IoError, "failed to create fleet archive parent");
    }
    const auto temporary = unique_sibling(directory, ".tmp-");
    std::filesystem::create_directories(temporary, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to create fleet archive temporary directory");
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    const auto payload = payload_json(*this).dump(true) + "\n";
    auto written = internal::write_text_file(temporary / "schedules.json", payload);
    if (!written) {
        cleanup();
        return written;
    }
    const internal::Json manifest(internal::Json::Object{
        {"conflicts", static_cast<double>(conflicts)},
        {"current_version_id", current_version_id_},
        {"fleet_id", fleet_id_},
        {"format", "rbfsafe-fleet-schedule-archive"},
        {"library_version", kVersion},
        {"members", static_cast<double>(members)},
        {"pair_evaluations", static_cast<double>(pair_evaluations)},
        {"payload_sha256", internal::sha256(payload)},
        {"reservations", static_cast<double>(reservations)},
        {"schema", static_cast<double>(kSchema)},
        {"versions", static_cast<double>(versions_.size())},
    });
    written = internal::write_text_file(temporary / "manifest.json", manifest.dump(true) + "\n");
    if (!written) {
        cleanup();
        return written;
    }
    auto published = publish_directory(temporary, directory, destination_exists);
    if (!published)
        cleanup();
    return published;
}

Result<FleetScheduleArchive> FleetScheduleArchive::load(const std::filesystem::path& directory,
                                                        const FleetScheduleArchiveLoadOptions& options) {
    if (directory.empty() || options.maximum_versions == 0 || options.maximum_members == 0 ||
        options.maximum_reservations == 0 || options.maximum_conflicts == 0 ||
        options.maximum_pair_evaluations == 0 || options.maximum_metadata_bytes == 0 ||
        options.maximum_payload_bytes == 0) {
        return Result<FleetScheduleArchive>::failure(StatusCode::InvalidArgument,
                                                     "fleet archive load options are invalid");
    }
    auto manifest = read_bounded_json(directory / "manifest.json", options.maximum_metadata_bytes);
    if (!manifest)
        return manifest.error();
    auto format = string_field(manifest.value(), "format");
    auto schema = size_field(manifest.value(), "schema", 1000);
    auto library_version = string_field(manifest.value(), "library_version");
    auto fleet_id = string_field(manifest.value(), "fleet_id");
    auto current_id = string_field(manifest.value(), "current_version_id", true);
    auto versions_count = size_field(manifest.value(), "versions", kMaximumExactJsonInteger);
    auto members_count = size_field(manifest.value(), "members", kMaximumExactJsonInteger);
    auto reservations_count = size_field(manifest.value(), "reservations", kMaximumExactJsonInteger);
    auto conflicts_count = size_field(manifest.value(), "conflicts", kMaximumExactJsonInteger);
    auto pairs_count = size_field(manifest.value(), "pair_evaluations", kMaximumExactJsonInteger);
    auto checksum = string_field(manifest.value(), "payload_sha256");
    if (!format || !schema || !library_version || !fleet_id || !current_id || !versions_count ||
        !members_count || !reservations_count || !conflicts_count || !pairs_count || !checksum) {
        return Result<FleetScheduleArchive>::failure(StatusCode::CorruptData,
                                                     "fleet archive manifest is incomplete");
    }
    if (format.value() != "rbfsafe-fleet-schedule-archive" || schema.value() != kSchema) {
        return Result<FleetScheduleArchive>::failure(StatusCode::IncompatibleFormat,
                                                     "unsupported fleet archive schema");
    }
    if (!internal::valid_sha256(checksum.value()) ||
        (versions_count.value() == 0) != current_id.value().empty()) {
        return Result<FleetScheduleArchive>::failure(StatusCode::CorruptData,
                                                     "fleet archive manifest identity is invalid");
    }
    if (versions_count.value() > options.maximum_versions ||
        members_count.value() > options.maximum_members ||
        reservations_count.value() > options.maximum_reservations ||
        conflicts_count.value() > options.maximum_conflicts ||
        pairs_count.value() > options.maximum_pair_evaluations) {
        return Result<FleetScheduleArchive>::failure(StatusCode::ResourceLimit,
                                                     "fleet archive manifest exceeds configured limits");
    }
    const auto payload_path = directory / "schedules.json";
    std::error_code error;
    const auto payload_size = std::filesystem::file_size(payload_path, error);
    if (error)
        return Result<FleetScheduleArchive>::failure(StatusCode::IoError,
                                                     "failed to inspect fleet archive payload");
    if (payload_size > options.maximum_payload_bytes) {
        return Result<FleetScheduleArchive>::failure(StatusCode::ResourceLimit,
                                                     "fleet archive payload exceeds configured limit");
    }
    auto actual_checksum = internal::sha256_file(payload_path);
    if (!actual_checksum)
        return actual_checksum.error();
    if (actual_checksum.value() != checksum.value()) {
        return Result<FleetScheduleArchive>::failure(StatusCode::CorruptData,
                                                     "fleet archive payload checksum mismatch");
    }
    auto payload = internal::read_json_file(payload_path);
    if (!payload)
        return payload.error();
    auto payload_format = string_field(payload.value(), "format");
    auto payload_schema = size_field(payload.value(), "schema", 1000);
    auto payload_fleet_id = string_field(payload.value(), "fleet_id");
    const auto* versions = payload.value().is_object() ? payload.value().find("versions") : nullptr;
    if (!payload_format || !payload_schema || !payload_fleet_id ||
        payload_format.value() != "rbfsafe-fleet-schedule-records" || payload_schema.value() != kSchema ||
        payload_fleet_id.value() != fleet_id.value() || versions == nullptr || !versions->is_array() ||
        versions->as_array().size() != versions_count.value()) {
        return Result<FleetScheduleArchive>::failure(StatusCode::CorruptData,
                                                     "fleet archive payload metadata is inconsistent");
    }
    FleetScheduleArchive result;
    result.fleet_id_ = std::move(fleet_id).value();
    result.current_version_id_ = std::move(current_id).value();
    result.versions_.reserve(versions_count.value());
    std::size_t members = 0;
    std::size_t reservations = 0;
    std::size_t conflicts = 0;
    std::size_t pairs = 0;
    for (std::size_t index = 0; index < versions->as_array().size(); ++index) {
        auto decoded =
            decode_version(versions->as_array()[index], options.maximum_members - members,
                           options.maximum_reservations - reservations, options.maximum_conflicts - conflicts,
                           std::max<std::size_t>(options.maximum_pair_evaluations - pairs, 1));
        if (!decoded)
            return decoded.error();
        const auto expected_parent = index == 0 ? std::string{} : result.versions_.back().id;
        if (decoded.value().sequence != static_cast<std::uint64_t>(index) ||
            decoded.value().parent_id != expected_parent ||
            decoded.value().fleet.fleet_id != result.fleet_id_ ||
            exceeds(members, decoded.value().fleet.members.size(), options.maximum_members) ||
            exceeds(reservations, decoded.value().report.reservations.size(), options.maximum_reservations) ||
            exceeds(conflicts, decoded.value().report.conflicts.size(), options.maximum_conflicts) ||
            exceeds(pairs, decoded.value().report.pair_evaluations, options.maximum_pair_evaluations)) {
            return Result<FleetScheduleArchive>::failure(StatusCode::CorruptData,
                                                         "fleet archive version chain is inconsistent");
        }
        members += decoded.value().fleet.members.size();
        reservations += decoded.value().report.reservations.size();
        conflicts += decoded.value().report.conflicts.size();
        pairs += decoded.value().report.pair_evaluations;
        result.versions_.push_back(std::move(decoded).value());
    }
    if (members != members_count.value() || reservations != reservations_count.value() ||
        conflicts != conflicts_count.value() || pairs != pairs_count.value() ||
        (!result.versions_.empty() && result.current_version_id_ != result.versions_.back().id)) {
        return Result<FleetScheduleArchive>::failure(StatusCode::CorruptData,
                                                     "fleet archive aggregate counts or head are invalid");
    }
    if (!result.valid()) {
        return Result<FleetScheduleArchive>::failure(StatusCode::CorruptData,
                                                     "fleet archive semantic validation failed");
    }
    return result;
}

} // namespace rbfsafe
