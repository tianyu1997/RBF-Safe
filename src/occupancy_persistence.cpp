#include <rbfsafe/occupancy.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/occupancy.h"
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

namespace rbfsafe {
namespace {

using internal::Json;

constexpr std::size_t kMaximumStorageStringBytes = 4'096;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

bool exact_object(const Json& value, std::size_t fields) {
    return value.is_object() && value.as_object().size() == fields;
}

Result<std::string> string_field(const Json& object, std::string_view key, bool allow_empty = false,
                                 std::size_t maximum = kMaximumStorageStringBytes) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "occupancy value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > maximum ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData, "occupancy string field is invalid",
                                            std::string(key));
    }
    return value->as_string();
}

Result<std::uint64_t> decimal_field(const Json& object, std::string_view key) {
    auto text = string_field(object, key, false, 32);
    if (!text)
        return text.error();
    std::uint64_t value = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "occupancy decimal field is invalid",
                                              std::string(key));
    }
    return value;
}

Result<std::size_t> size_field(const Json& object, std::string_view key) {
    auto value = decimal_field(object, key);
    if (!value)
        return value.error();
    if (value.value() > std::numeric_limits<std::size_t>::max()) {
        return Result<std::size_t>::failure(StatusCode::ResourceLimit,
                                            "occupancy size field exceeds platform limit", std::string(key));
    }
    return static_cast<std::size_t>(value.value());
}

Result<double> number_field(const Json& object, std::string_view key, bool non_negative = false) {
    if (!object.is_object()) {
        return Result<double>::failure(StatusCode::CorruptData, "occupancy value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        (non_negative && value->as_number() < 0.0)) {
        return Result<double>::failure(StatusCode::CorruptData, "occupancy numeric field is invalid",
                                       std::string(key));
    }
    return value->as_number();
}

Result<std::size_t> enum_field(const Json& object, std::string_view key, std::size_t maximum) {
    auto number = number_field(object, key, true);
    if (!number || number.value() > static_cast<double>(maximum) ||
        number.value() != static_cast<double>(static_cast<std::size_t>(number.value()))) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "occupancy enum field is invalid",
                                            std::string(key));
    }
    return static_cast<std::size_t>(number.value());
}

Result<const Json::Array*> array_field(const Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object()) {
        return Result<const Json::Array*>::failure(StatusCode::CorruptData,
                                                   "occupancy value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_array()) {
        return Result<const Json::Array*>::failure(StatusCode::CorruptData,
                                                   "occupancy array field is invalid", std::string(key));
    }
    if (value->as_array().size() > maximum) {
        return Result<const Json::Array*>::failure(
            StatusCode::ResourceLimit, "occupancy array exceeds configured entry limit", std::string(key));
    }
    return &value->as_array();
}

template <std::size_t Size>
Result<std::array<double, Size>> number_array_field(const Json& object, std::string_view key,
                                                    bool non_negative = false) {
    auto values = array_field(object, key, Size);
    if (!values)
        return values.error();
    if (values.value()->size() != Size) {
        return Result<std::array<double, Size>>::failure(
            StatusCode::CorruptData, "occupancy numeric array has an invalid length", std::string(key));
    }
    std::array<double, Size> result{};
    for (std::size_t index = 0; index < Size; ++index) {
        const auto& value = (*values.value())[index];
        if (!value.is_number() || !std::isfinite(value.as_number()) ||
            (non_negative && value.as_number() < 0.0)) {
            return Result<std::array<double, Size>>::failure(
                StatusCode::CorruptData, "occupancy numeric array contains an invalid value",
                std::string(key));
        }
        result[index] = value.as_number();
    }
    return result;
}

Result<Interval> decode_interval(const Json& value) {
    if (!exact_object(value, 2)) {
        return Result<Interval>::failure(StatusCode::CorruptData, "occupancy interval object is invalid");
    }
    auto lower = number_field(value, "lower");
    auto upper = number_field(value, "upper");
    if (!lower || !upper)
        return Result<Interval>::failure(StatusCode::CorruptData, "occupancy interval is incomplete");
    Interval result{lower.value(), upper.value()};
    if (!result.valid()) {
        return Result<Interval>::failure(StatusCode::CorruptData, "occupancy interval bounds are invalid");
    }
    return result;
}

Result<CspaceAabb> decode_cspace_aabb(const Json& value, std::size_t maximum_dimension) {
    if (!exact_object(value, 1)) {
        return Result<CspaceAabb>::failure(StatusCode::CorruptData,
                                           "occupancy C-space AABB object is invalid");
    }
    auto axes = array_field(value, "axes", maximum_dimension);
    if (!axes)
        return axes.error();
    if (axes.value()->empty()) {
        return Result<CspaceAabb>::failure(StatusCode::CorruptData, "occupancy C-space AABB is empty");
    }
    std::vector<Interval> result_axes;
    result_axes.reserve(axes.value()->size());
    for (const auto& item : *axes.value()) {
        auto interval = decode_interval(item);
        if (!interval)
            return interval.error();
        result_axes.push_back(interval.value());
    }
    return CspaceAabb(std::move(result_axes));
}

Result<WorkspaceAabb> decode_workspace_aabb(const Json& value) {
    if (!exact_object(value, 2)) {
        return Result<WorkspaceAabb>::failure(StatusCode::CorruptData,
                                              "occupancy workspace AABB object is invalid");
    }
    auto lower = array_field(value, "lower", 3);
    auto upper = array_field(value, "upper", 3);
    if (!lower || !upper || lower.value()->size() != 3 || upper.value()->size() != 3) {
        return Result<WorkspaceAabb>::failure(StatusCode::CorruptData,
                                              "occupancy workspace AABB coordinates are invalid");
    }
    WorkspaceAabb result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto& lower_value = (*lower.value())[axis];
        const auto& upper_value = (*upper.value())[axis];
        if (!lower_value.is_number() || !upper_value.is_number() || !std::isfinite(lower_value.as_number()) ||
            !std::isfinite(upper_value.as_number())) {
            return Result<WorkspaceAabb>::failure(StatusCode::CorruptData,
                                                  "occupancy workspace AABB coordinate is invalid");
        }
        result.lower[axis] = lower_value.as_number();
        result.upper[axis] = upper_value.as_number();
    }
    if (!result.valid()) {
        return Result<WorkspaceAabb>::failure(StatusCode::CorruptData,
                                              "occupancy workspace AABB bounds are invalid");
    }
    return result;
}

Result<Configuration> decode_configuration(const Json& value, std::size_t maximum_dimension) {
    if (!value.is_array() || value.as_array().empty() || value.as_array().size() > maximum_dimension) {
        return Result<Configuration>::failure(StatusCode::CorruptData, "occupancy configuration is invalid");
    }
    Configuration result;
    result.reserve(value.as_array().size());
    for (const auto& coordinate : value.as_array()) {
        if (!coordinate.is_number() || !std::isfinite(coordinate.as_number())) {
            return Result<Configuration>::failure(StatusCode::CorruptData,
                                                  "occupancy configuration coordinate is invalid");
        }
        result.push_back(coordinate.as_number());
    }
    return result;
}

Result<TimedConfiguration> decode_waypoint(const Json& value, std::size_t maximum_dimension) {
    if (!exact_object(value, 2)) {
        return Result<TimedConfiguration>::failure(StatusCode::CorruptData,
                                                   "occupancy waypoint object is invalid");
    }
    auto tick = decimal_field(value, "tick");
    const auto* configuration = value.find("configuration");
    if (!tick || configuration == nullptr) {
        return Result<TimedConfiguration>::failure(StatusCode::CorruptData,
                                                   "occupancy waypoint is incomplete");
    }
    auto decoded = decode_configuration(*configuration, maximum_dimension);
    if (!decoded)
        return decoded.error();
    return TimedConfiguration{tick.value(), std::move(decoded).value()};
}

Result<SweptLinkOccupancySlice> decode_slice(const Json& value, std::size_t dimension,
                                             std::size_t maximum_link_envelopes,
                                             std::size_t& total_link_envelopes,
                                             const CancellationToken& cancellation) {
    if (!exact_object(value, 6)) {
        return Result<SweptLinkOccupancySlice>::failure(StatusCode::CorruptData,
                                                        "occupancy slice object is invalid");
    }
    auto id = string_field(value, "id");
    auto segment = size_field(value, "trajectory_segment_index");
    auto begin = decimal_field(value, "begin_tick");
    auto end = decimal_field(value, "end_tick");
    const auto* domain_json = value.find("configuration_domain");
    auto envelopes_json = array_field(value, "link_envelopes", maximum_link_envelopes);
    if (!id || !segment || !begin || !end || domain_json == nullptr || !envelopes_json) {
        return Result<SweptLinkOccupancySlice>::failure(StatusCode::CorruptData,
                                                        "occupancy slice is incomplete");
    }
    if (envelopes_json.value()->empty() ||
        envelopes_json.value()->size() > maximum_link_envelopes - total_link_envelopes) {
        return Result<SweptLinkOccupancySlice>::failure(
            StatusCode::ResourceLimit, "occupancy link envelopes exceed configured aggregate limit");
    }
    auto domain = decode_cspace_aabb(*domain_json, dimension);
    if (!domain || domain.value().dimension() != dimension)
        return Result<SweptLinkOccupancySlice>::failure(
            StatusCode::CorruptData, "occupancy slice dimension does not match trajectory");
    SweptLinkOccupancySlice result;
    result.id = std::move(id).value();
    result.trajectory_segment_index = segment.value();
    result.begin_tick = begin.value();
    result.end_tick = end.value();
    result.configuration_domain = std::move(domain).value();
    result.link_envelopes.reserve(envelopes_json.value()->size());
    for (const auto& item : *envelopes_json.value()) {
        if (cancellation.cancelled()) {
            return Result<SweptLinkOccupancySlice>::failure(StatusCode::Cancelled,
                                                            "continuous occupancy bundle load was cancelled");
        }
        auto envelope = decode_workspace_aabb(item);
        if (!envelope)
            return envelope.error();
        result.link_envelopes.push_back(envelope.value());
    }
    total_link_envelopes += result.link_envelopes.size();
    return result;
}

Result<RobotTrajectoryOccupancy> decode_occupancy(const Json& value,
                                                  const ContinuousFleetOccupancyBundleLoadOptions& options,
                                                  std::size_t& total_waypoints, std::size_t& total_slices,
                                                  std::size_t& total_link_envelopes) {
    if (!value.is_object()) {
        return Result<RobotTrajectoryOccupancy>::failure(StatusCode::CorruptData,
                                                         "robot trajectory occupancy object is invalid");
    }
    auto storage = decimal_field(value, "storage_schema");
    if (!storage)
        return storage.error();
    if (storage.value() != 1 && storage.value() != 2) {
        return Result<RobotTrajectoryOccupancy>::failure(
            StatusCode::IncompatibleFormat, "robot trajectory occupancy schema is not supported");
    }
    if (!exact_object(value, storage.value() == 1 ? 14 : 17)) {
        return Result<RobotTrajectoryOccupancy>::failure(
            StatusCode::CorruptData, "robot trajectory occupancy fields do not match its schema");
    }
    auto id = string_field(value, "id");
    auto timeline = string_field(value, "timeline_id");
    auto frame = string_field(value, "workspace_frame_id");
    auto deployment = string_field(value, "deployment_id");
    auto robot = string_field(value, "robot_digest");
    auto algorithm = string_field(value, "algorithm");
    auto algorithm_version = string_field(value, "algorithm_version");
    auto maximum_depth = size_field(value, "maximum_subdivision_depth");
    auto maximum_width = number_field(value, "maximum_normalized_joint_width", true);
    auto padding = number_field(value, "link_padding", true);
    auto trajectory_json = array_field(value, "trajectory", options.maximum_input_waypoints);
    auto slices_json = array_field(value, "slices", options.maximum_slices);
    auto translation = number_array_field<3>(value, "workspace_translation");
    if (!id || !timeline || !frame || !deployment || !robot || !algorithm || !algorithm_version ||
        !maximum_depth || !maximum_width || !padding || !trajectory_json || !slices_json || !translation) {
        return Result<RobotTrajectoryOccupancy>::failure(StatusCode::CorruptData,
                                                         "robot trajectory occupancy is incomplete");
    }
    if (trajectory_json.value()->size() > options.maximum_input_waypoints - total_waypoints ||
        slices_json.value()->size() > options.maximum_slices - total_slices) {
        return Result<RobotTrajectoryOccupancy>::failure(
            StatusCode::ResourceLimit, "occupancy records exceed configured aggregate limits");
    }
    if (trajectory_json.value()->size() < 2 || slices_json.value()->empty()) {
        return Result<RobotTrajectoryOccupancy>::failure(
            StatusCode::CorruptData, "robot trajectory occupancy has no replayable content");
    }
    std::array<double, 9> rotation{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    std::array<double, 3> translation_uncertainty{};
    double angular_uncertainty = 0.0;
    if (storage.value() == 2) {
        auto decoded_rotation = number_array_field<9>(value, "workspace_rotation");
        auto decoded_translation_uncertainty =
            number_array_field<3>(value, "workspace_translation_uncertainty", true);
        auto decoded_angular_uncertainty = number_field(value, "workspace_angular_uncertainty_radians", true);
        if (!decoded_rotation || !decoded_translation_uncertainty || !decoded_angular_uncertainty) {
            return Result<RobotTrajectoryOccupancy>::failure(StatusCode::CorruptData,
                                                             "deployment-frame bounds are incomplete");
        }
        rotation = decoded_rotation.value();
        translation_uncertainty = decoded_translation_uncertainty.value();
        angular_uncertainty = decoded_angular_uncertainty.value();
    }
    RobotTrajectoryOccupancy result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.timeline_id = std::move(timeline).value();
    result.workspace_frame_id = std::move(frame).value();
    result.deployment_id = std::move(deployment).value();
    result.robot_digest = std::move(robot).value();
    result.workspace_translation = translation.value();
    result.workspace_rotation = rotation;
    result.workspace_translation_uncertainty = translation_uncertainty;
    result.workspace_angular_uncertainty_radians = angular_uncertainty;
    result.algorithm = std::move(algorithm).value();
    result.algorithm_version = std::move(algorithm_version).value();
    result.maximum_subdivision_depth = maximum_depth.value();
    result.maximum_normalized_joint_width = maximum_width.value();
    result.link_padding = padding.value();
    result.trajectory.reserve(trajectory_json.value()->size());
    for (const auto& item : *trajectory_json.value()) {
        if (options.cancellation.cancelled()) {
            return Result<RobotTrajectoryOccupancy>::failure(
                StatusCode::Cancelled, "continuous occupancy bundle load was cancelled");
        }
        auto waypoint = decode_waypoint(item, options.maximum_dimension);
        if (!waypoint)
            return waypoint.error();
        result.trajectory.push_back(std::move(waypoint).value());
    }
    const std::size_t dimension = result.trajectory.front().configuration.size();
    result.slices.reserve(slices_json.value()->size());
    for (const auto& item : *slices_json.value()) {
        if (options.cancellation.cancelled()) {
            return Result<RobotTrajectoryOccupancy>::failure(
                StatusCode::Cancelled, "continuous occupancy bundle load was cancelled");
        }
        auto slice = decode_slice(item, dimension, options.maximum_link_envelopes, total_link_envelopes,
                                  options.cancellation);
        if (!slice)
            return slice.error();
        result.slices.push_back(std::move(slice).value());
    }
    total_waypoints += result.trajectory.size();
    total_slices += result.slices.size();
    if (!result.valid()) {
        return Result<RobotTrajectoryOccupancy>::failure(
            StatusCode::IdentityMismatch, "robot trajectory occupancy identity is invalid", result.id);
    }
    return result;
}

Result<ContinuousOccupancyConflict> decode_conflict(const Json& value) {
    if (!exact_object(value, 11)) {
        return Result<ContinuousOccupancyConflict>::failure(
            StatusCode::CorruptData, "continuous occupancy conflict object is invalid");
    }
    auto first_occupancy = string_field(value, "first_occupancy_id");
    auto second_occupancy = string_field(value, "second_occupancy_id");
    auto first_slice = string_field(value, "first_slice_id");
    auto second_slice = string_field(value, "second_slice_id");
    auto first_link = size_field(value, "first_link_index");
    auto second_link = size_field(value, "second_link_index");
    auto begin = decimal_field(value, "overlap_begin_tick");
    auto end = decimal_field(value, "overlap_end_tick");
    auto reason =
        enum_field(value, "reason",
                   static_cast<std::size_t>(ContinuousOccupancyConflictReason::SeparationMarginViolated));
    auto clearance = number_field(value, "clearance_lower_bound", true);
    auto margin = number_field(value, "required_margin", true);
    if (!first_occupancy || !second_occupancy || !first_slice || !second_slice || !first_link ||
        !second_link || !begin || !end || !reason || !clearance || !margin) {
        return Result<ContinuousOccupancyConflict>::failure(StatusCode::CorruptData,
                                                            "continuous occupancy conflict is incomplete");
    }
    ContinuousOccupancyConflict result;
    result.first_occupancy_id = std::move(first_occupancy).value();
    result.second_occupancy_id = std::move(second_occupancy).value();
    result.first_slice_id = std::move(first_slice).value();
    result.second_slice_id = std::move(second_slice).value();
    result.first_link_index = first_link.value();
    result.second_link_index = second_link.value();
    result.overlap_begin_tick = begin.value();
    result.overlap_end_tick = end.value();
    result.reason = static_cast<ContinuousOccupancyConflictReason>(reason.value());
    result.clearance_lower_bound = clearance.value();
    result.required_margin = margin.value();
    return result;
}

Result<ContinuousFleetOccupancyReport>
decode_report(const Json& value, const ContinuousFleetOccupancyBundleLoadOptions& options) {
    if (!exact_object(value, 9)) {
        return Result<ContinuousFleetOccupancyReport>::failure(
            StatusCode::CorruptData, "continuous fleet occupancy report object is invalid");
    }
    auto id = string_field(value, "id");
    auto timeline = string_field(value, "timeline_id");
    auto frame = string_field(value, "workspace_frame_id");
    auto status = enum_field(value, "status",
                             static_cast<std::size_t>(ContinuousFleetOccupancyStatus::PotentialConflict));
    auto separation = number_field(value, "minimum_separation", true);
    auto occupancy_ids = array_field(value, "occupancy_ids", options.maximum_occupancies);
    auto conflicts = array_field(value, "conflicts", options.maximum_conflicts);
    auto slice_evaluations = size_field(value, "slice_pair_evaluations");
    auto link_evaluations = size_field(value, "link_pair_evaluations");
    if (!id || !timeline || !frame || !status || !separation || !occupancy_ids || !conflicts ||
        !slice_evaluations || !link_evaluations) {
        return Result<ContinuousFleetOccupancyReport>::failure(
            StatusCode::CorruptData, "continuous fleet occupancy report is incomplete");
    }
    if (slice_evaluations.value() > options.maximum_slice_pair_evaluations ||
        link_evaluations.value() > options.maximum_link_pair_evaluations) {
        return Result<ContinuousFleetOccupancyReport>::failure(
            StatusCode::ResourceLimit, "continuous occupancy report exceeds configured evaluation limits");
    }
    ContinuousFleetOccupancyReport result;
    result.id = std::move(id).value();
    result.timeline_id = std::move(timeline).value();
    result.workspace_frame_id = std::move(frame).value();
    result.status = static_cast<ContinuousFleetOccupancyStatus>(status.value());
    result.minimum_separation = separation.value();
    result.slice_pair_evaluations = slice_evaluations.value();
    result.link_pair_evaluations = link_evaluations.value();
    result.occupancy_ids.reserve(occupancy_ids.value()->size());
    for (const auto& item : *occupancy_ids.value()) {
        if (!item.is_string() || item.as_string().size() > kMaximumStorageStringBytes) {
            return Result<ContinuousFleetOccupancyReport>::failure(
                StatusCode::CorruptData, "continuous occupancy report identifier is invalid");
        }
        result.occupancy_ids.push_back(item.as_string());
    }
    result.conflicts.reserve(conflicts.value()->size());
    for (const auto& item : *conflicts.value()) {
        if (options.cancellation.cancelled()) {
            return Result<ContinuousFleetOccupancyReport>::failure(
                StatusCode::Cancelled, "continuous occupancy bundle load was cancelled");
        }
        auto conflict = decode_conflict(item);
        if (!conflict)
            return conflict.error();
        result.conflicts.push_back(std::move(conflict).value());
    }
    if (!result.valid()) {
        return Result<ContinuousFleetOccupancyReport>::failure(
            StatusCode::IdentityMismatch, "continuous occupancy report identity is invalid", result.id);
    }
    return result;
}

Result<Json> read_bounded_json(const std::filesystem::path& path,
                               const ContinuousFleetOccupancyBundleLoadOptions& options) {
    if (options.cancellation.cancelled()) {
        return Result<Json>::failure(StatusCode::Cancelled, "continuous occupancy bundle load was cancelled");
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || status.type() == std::filesystem::file_type::not_found) {
        return Result<Json>::failure(StatusCode::IoError, "failed to inspect continuous occupancy bundle",
                                     path.string());
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<Json>::failure(
            StatusCode::CorruptData,
            "continuous occupancy bundle is missing, indirect, or not a regular file", path.string());
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<Json>::failure(StatusCode::IoError,
                                     "failed to inspect continuous occupancy bundle size", path.string());
    }
    if (bytes > options.maximum_payload_bytes ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return Result<Json>::failure(StatusCode::ResourceLimit,
                                     "continuous occupancy bundle exceeds configured byte limit",
                                     path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<Json>::failure(StatusCode::IoError, "failed to open continuous occupancy bundle",
                                     path.string());
    }
    std::string text(static_cast<std::size_t>(bytes), '\0');
    if (!text.empty()) {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (input.gcount() != static_cast<std::streamsize>(text.size())) {
            return Result<Json>::failure(StatusCode::CorruptData,
                                         "continuous occupancy bundle changed while reading", path.string());
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        return Result<Json>::failure(StatusCode::CorruptData,
                                     "continuous occupancy bundle changed while reading", path.string());
    }
    return Json::parse(text);
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
                                         "failed to stage existing continuous occupancy bundle",
                                         destination.string());
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish continuous occupancy bundle",
                                     destination.string());
    }
    if (destination_exists) {
        std::filesystem::remove(backup, error);
        if (error) {
            return Result<void>::failure(
                StatusCode::IoError, "failed to remove staged continuous occupancy bundle", backup.string());
        }
    }
    return Result<void>::success();
}

Json bundle_document(const ContinuousFleetOccupancyBundle& bundle) {
    const auto payload = internal::continuous_fleet_occupancy_bundle_payload_json(bundle, true);
    return Json::Object{
        {"checksum", internal::sha256(payload.dump(false))},
        {"format", "rbfsafe-continuous-fleet-occupancy-bundle"},
        {"library_version", kVersion},
        {"payload", payload},
        {"schema", static_cast<int>(bundle.storage_schema())},
    };
}

} // namespace

Result<void> save_continuous_fleet_occupancy_bundle(const ContinuousFleetOccupancyBundle& bundle,
                                                    const std::filesystem::path& path,
                                                    const SaveOptions& options) {
    if (!bundle.valid() || path.empty()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "continuous occupancy bundle or destination is invalid");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return Result<void>::failure(
            StatusCode::IoError, "failed to inspect continuous occupancy bundle destination", path.string());
    }
    if (destination_exists) {
        const auto status = std::filesystem::symlink_status(path, error);
        if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
            return Result<void>::failure(StatusCode::IoError,
                                         "continuous occupancy destination is indirect or not a regular file",
                                         path.string());
        }
        if (!options.overwrite) {
            return Result<void>::failure(StatusCode::IoError,
                                         "continuous occupancy destination already exists", path.string());
        }
    }
    const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    if (!std::filesystem::exists(parent, error) || !std::filesystem::is_directory(parent, error) || error) {
        return Result<void>::failure(
            StatusCode::IoError, "continuous occupancy destination parent is unavailable", parent.string());
    }
    const auto temporary = unique_sibling(path, ".tmp-");
    const auto document = bundle_document(bundle).dump(true) + "\n";
    auto written = internal::write_text_file(temporary, document);
    if (!written) {
        std::filesystem::remove(temporary, error);
        return written.error();
    }

    ContinuousFleetOccupancyBundleLoadOptions verify;
    verify.maximum_occupancies = std::max<std::size_t>(1, bundle.occupancies().size());
    verify.maximum_input_waypoints = 1;
    verify.maximum_dimension = 1;
    verify.maximum_slices = 1;
    verify.maximum_link_envelopes = 1;
    for (const auto& occupancy : bundle.occupancies()) {
        verify.maximum_input_waypoints += occupancy.trajectory.size();
        verify.maximum_dimension =
            std::max(verify.maximum_dimension, occupancy.trajectory.front().configuration.size());
        verify.maximum_slices += occupancy.slices.size();
        for (const auto& slice : occupancy.slices)
            verify.maximum_link_envelopes += slice.link_envelopes.size();
    }
    verify.maximum_conflicts = std::max<std::size_t>(1, bundle.report().conflicts.size());
    verify.maximum_slice_pair_evaluations = std::max<std::size_t>(1, bundle.report().slice_pair_evaluations);
    verify.maximum_link_pair_evaluations = std::max<std::size_t>(1, bundle.report().link_pair_evaluations);
    verify.maximum_payload_bytes = std::max<std::uintmax_t>(document.size(), 1);
    auto loaded = load_continuous_fleet_occupancy_bundle(temporary, verify);
    if (!loaded || loaded.value().id() != bundle.id()) {
        std::filesystem::remove(temporary, error);
        if (!loaded)
            return loaded.error();
        return Result<void>::failure(StatusCode::CorruptData,
                                     "staged continuous occupancy bundle identity changed");
    }
    auto published = publish_file(temporary, path, destination_exists);
    if (!published)
        std::filesystem::remove(temporary, error);
    return published;
}

Result<ContinuousFleetOccupancyBundle>
load_continuous_fleet_occupancy_bundle(const std::filesystem::path& path,
                                       const ContinuousFleetOccupancyBundleLoadOptions& options) {
    if (path.empty() || options.maximum_occupancies == 0 || options.maximum_input_waypoints < 2 ||
        options.maximum_dimension == 0 || options.maximum_slices == 0 ||
        options.maximum_link_envelopes == 0 || options.maximum_conflicts == 0 ||
        options.maximum_slice_pair_evaluations == 0 || options.maximum_link_pair_evaluations == 0 ||
        options.maximum_payload_bytes == 0) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::InvalidArgument, "continuous occupancy bundle path or load options are invalid");
    }
    auto document = read_bounded_json(path, options);
    if (!document)
        return document.error();
    if (!exact_object(document.value(), 5)) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::CorruptData, "continuous occupancy bundle document fields are invalid");
    }
    auto format = string_field(document.value(), "format");
    auto schema = enum_field(document.value(), "schema", std::numeric_limits<std::uint32_t>::max());
    auto checksum = string_field(document.value(), "checksum");
    auto library_version = string_field(document.value(), "library_version");
    const auto* payload = document.value().find("payload");
    if (!format || !schema || !checksum || !library_version || payload == nullptr ||
        !exact_object(*payload, 4)) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::CorruptData, "continuous occupancy bundle document is incomplete");
    }
    if (format.value() != "rbfsafe-continuous-fleet-occupancy-bundle") {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::IncompatibleFormat, "continuous occupancy bundle format is not recognized",
            format.value());
    }
    if (schema.value() != 1 && schema.value() != 2) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::IncompatibleFormat, "continuous occupancy bundle schema is not supported");
    }
    if (!internal::valid_sha256(checksum.value()) ||
        checksum.value() != internal::sha256(payload->dump(false))) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::CorruptData, "continuous occupancy bundle checksum mismatch", path.string());
    }
    auto storage = decimal_field(*payload, "storage_schema");
    auto stored_id = string_field(*payload, "id");
    auto occupancies_json = array_field(*payload, "occupancies", options.maximum_occupancies);
    const auto* report_json = payload->find("report");
    if (!storage || !stored_id || !occupancies_json || report_json == nullptr) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::CorruptData, "continuous occupancy bundle payload is incomplete");
    }
    if ((storage.value() != 1 && storage.value() != 2) || storage.value() != schema.value()) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::IncompatibleFormat,
            "continuous occupancy bundle document and storage schemas are incompatible");
    }
    if (occupancies_json.value()->empty()) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::CorruptData, "continuous occupancy bundle has no occupancies");
    }
    std::size_t total_waypoints = 0;
    std::size_t total_slices = 0;
    std::size_t total_link_envelopes = 0;
    std::vector<RobotTrajectoryOccupancy> occupancies;
    occupancies.reserve(occupancies_json.value()->size());
    for (const auto& item : *occupancies_json.value()) {
        if (options.cancellation.cancelled()) {
            return Result<ContinuousFleetOccupancyBundle>::failure(
                StatusCode::Cancelled, "continuous occupancy bundle load was cancelled");
        }
        auto occupancy = decode_occupancy(item, options, total_waypoints, total_slices, total_link_envelopes);
        if (!occupancy)
            return occupancy.error();
        occupancies.push_back(std::move(occupancy).value());
    }
    auto report = decode_report(*report_json, options);
    if (!report)
        return report.error();

    ContinuousFleetOccupancyOptions replay_options;
    replay_options.minimum_separation = report.value().minimum_separation;
    replay_options.maximum_occupancies = options.maximum_occupancies;
    replay_options.maximum_conflicts = options.maximum_conflicts;
    replay_options.maximum_slice_pair_evaluations = options.maximum_slice_pair_evaluations;
    replay_options.maximum_link_pair_evaluations = options.maximum_link_pair_evaluations;
    replay_options.cancellation = options.cancellation;
    auto replayed = analyze_continuous_fleet_occupancy(occupancies, replay_options);
    if (!replayed)
        return replayed.error();
    if (replayed.value().id != report.value().id) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::IdentityMismatch, "continuous occupancy report does not match replayed analysis",
            report.value().id);
    }

    ContinuousFleetOccupancyBundle result;
    result.storage_schema_ = static_cast<std::uint32_t>(storage.value());
    result.id_ = std::move(stored_id).value();
    result.occupancies_ = std::move(occupancies);
    result.report_ = std::move(report).value();
    if (!result.valid() || internal::continuous_fleet_occupancy_bundle_identity(result) != result.id_) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::IdentityMismatch, "continuous occupancy bundle identity is invalid", result.id_);
    }
    return result;
}

} // namespace rbfsafe
