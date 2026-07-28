#include <rbfsafe/occupancy.h>

#include "internal/certificate_utils.h"
#include "internal/occupancy.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumSubdivisionDepth = 64;
constexpr const char* kOccupancyAlgorithm = "ifk-aa-piecewise-linear-swept-link-aabb";
constexpr const char* kOccupancyAlgorithmVersion = "1";

bool contains_control_character(const std::string& value) {
    return std::any_of(value.begin(), value.end(),
                       [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_identifier(const std::string& value) {
    return !value.empty() && value.size() <= kMaximumIdentifierBytes && !contains_control_character(value);
}

bool valid_translation(const std::array<double, 3>& translation) {
    return std::all_of(translation.begin(), translation.end(),
                       [](double value) { return std::isfinite(value); });
}

bool valid_build_options(const ContinuousOccupancyBuildOptions& options) {
    return options.maximum_input_waypoints >= 2 && options.maximum_slices > 0 &&
           options.maximum_link_envelopes > 0 &&
           options.maximum_subdivision_depth <= kMaximumSubdivisionDepth &&
           std::isfinite(options.maximum_normalized_joint_width) &&
           options.maximum_normalized_joint_width > 0.0 && std::isfinite(options.link_padding) &&
           options.link_padding >= 0.0;
}

bool valid_replay_options(const ContinuousOccupancyReplayOptions& options) {
    return options.maximum_input_waypoints >= 2 && options.maximum_slices > 0 &&
           options.maximum_link_envelopes > 0;
}

bool valid_analysis_options(const ContinuousFleetOccupancyOptions& options) {
    return std::isfinite(options.minimum_separation) && options.minimum_separation >= 0.0 &&
           options.maximum_occupancies > 0 && options.maximum_conflicts > 0 &&
           options.maximum_slice_pair_evaluations > 0 && options.maximum_link_pair_evaluations > 0;
}

bool configuration_within_limits(const SerialRobotModel& robot, std::span<const double> configuration) {
    if (configuration.size() != robot.dimension())
        return false;
    for (std::size_t axis = 0; axis < configuration.size(); ++axis) {
        if (!robot.joint_limits()[axis].contains(configuration[axis]))
            return false;
    }
    return true;
}

bool checked_increment(std::size_t& value, std::size_t maximum) {
    if (value >= maximum)
        return false;
    ++value;
    return true;
}

struct PendingSlice {
    std::size_t trajectory_segment_index = 0;
    std::uint64_t begin_tick = 0;
    std::uint64_t end_tick = 0;
    Configuration begin_configuration;
    Configuration end_configuration;
    std::size_t depth = 0;
};

CspaceAabb endpoint_domain(std::span<const double> first, std::span<const double> second) {
    std::vector<Interval> axes;
    axes.reserve(first.size());
    for (std::size_t axis = 0; axis < first.size(); ++axis)
        axes.emplace_back(std::min(first[axis], second[axis]), std::max(first[axis], second[axis]));
    return CspaceAabb(std::move(axes));
}

double normalized_joint_width(const SerialRobotModel& robot, const CspaceAabb& domain) {
    double maximum = 0.0;
    for (std::size_t axis = 0; axis < domain.dimension(); ++axis) {
        const double limit_width = robot.joint_limits()[axis].width();
        if (limit_width == 0.0) {
            if (domain.axes()[axis].width() != 0.0)
                return std::numeric_limits<double>::infinity();
            continue;
        }
        maximum = std::max(maximum, domain.axes()[axis].width() / limit_width);
    }
    return maximum;
}

Configuration interpolate(std::span<const double> first, std::span<const double> second, double fraction) {
    Configuration result;
    result.reserve(first.size());
    for (std::size_t axis = 0; axis < first.size(); ++axis)
        result.push_back(first[axis] + fraction * (second[axis] - first[axis]));
    return result;
}

Result<std::vector<WorkspaceAabb>> translated_envelopes(const LinkEnvelope& envelope,
                                                        const std::array<double, 3>& translation) {
    std::vector<WorkspaceAabb> result = envelope.links;
    for (auto& box : result) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            box.lower[axis] =
                std::nextafter(box.lower[axis] + translation[axis], -std::numeric_limits<double>::infinity());
            box.upper[axis] =
                std::nextafter(box.upper[axis] + translation[axis], std::numeric_limits<double>::infinity());
        }
        if (!box.valid()) {
            return Result<std::vector<WorkspaceAabb>>::failure(
                StatusCode::InvalidArgument, "workspace translation produced a non-finite swept envelope");
        }
    }
    return result;
}

bool slice_less(const SweptLinkOccupancySlice& first, const SweptLinkOccupancySlice& second) {
    return std::tie(first.trajectory_segment_index, first.begin_tick, first.end_tick, first.id) <
           std::tie(second.trajectory_segment_index, second.begin_tick, second.end_tick, second.id);
}

bool conflict_less(const ContinuousOccupancyConflict& first, const ContinuousOccupancyConflict& second) {
    return std::tie(first.first_occupancy_id, first.second_occupancy_id, first.overlap_begin_tick,
                    first.overlap_end_tick, first.first_slice_id, first.second_slice_id,
                    first.first_link_index, first.second_link_index, first.reason) <
           std::tie(second.first_occupancy_id, second.second_occupancy_id, second.overlap_begin_tick,
                    second.overlap_end_tick, second.first_slice_id, second.second_slice_id,
                    second.first_link_index, second.second_link_index, second.reason);
}

bool valid_conflict(const ContinuousOccupancyConflict& conflict) {
    return internal::valid_sha256(conflict.first_occupancy_id) &&
           internal::valid_sha256(conflict.second_occupancy_id) &&
           conflict.first_occupancy_id < conflict.second_occupancy_id &&
           internal::valid_sha256(conflict.first_slice_id) &&
           internal::valid_sha256(conflict.second_slice_id) &&
           conflict.overlap_begin_tick < conflict.overlap_end_tick &&
           (conflict.reason == ContinuousOccupancyConflictReason::SweptEnvelopeOverlap ||
            conflict.reason == ContinuousOccupancyConflictReason::SeparationMarginViolated) &&
           std::isfinite(conflict.clearance_lower_bound) && conflict.clearance_lower_bound >= 0.0 &&
           std::isfinite(conflict.required_margin) && conflict.required_margin >= 0.0 &&
           ((conflict.reason == ContinuousOccupancyConflictReason::SweptEnvelopeOverlap &&
             conflict.clearance_lower_bound == 0.0) ||
            (conflict.reason == ContinuousOccupancyConflictReason::SeparationMarginViolated &&
             conflict.clearance_lower_bound < conflict.required_margin));
}

} // namespace

namespace internal {

Json interval_json(const Interval& interval) {
    return Json::Object{{"lower", interval.lower}, {"upper", interval.upper}};
}

Json cspace_aabb_json(const CspaceAabb& domain) {
    Json::Array axes;
    axes.reserve(domain.dimension());
    for (const auto& axis : domain.axes())
        axes.emplace_back(interval_json(axis));
    return Json::Object{{"axes", std::move(axes)}};
}

Json workspace_aabb_json(const WorkspaceAabb& box) {
    Json::Array lower;
    Json::Array upper;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        lower.emplace_back(box.lower[axis]);
        upper.emplace_back(box.upper[axis]);
    }
    return Json::Object{{"lower", std::move(lower)}, {"upper", std::move(upper)}};
}

Json configuration_json(std::span<const double> configuration) {
    Json::Array values;
    values.reserve(configuration.size());
    for (const double value : configuration)
        values.emplace_back(value);
    return values;
}

Json timed_configuration_json(const TimedConfiguration& waypoint) {
    return Json::Object{{"configuration", configuration_json(waypoint.configuration)},
                        {"tick", std::to_string(waypoint.tick)}};
}

Json swept_slice_json(const SweptLinkOccupancySlice& slice, bool include_id) {
    Json::Array envelopes;
    envelopes.reserve(slice.link_envelopes.size());
    for (const auto& envelope : slice.link_envelopes)
        envelopes.emplace_back(workspace_aabb_json(envelope));
    Json::Object object{
        {"begin_tick", std::to_string(slice.begin_tick)},
        {"configuration_domain", cspace_aabb_json(slice.configuration_domain)},
        {"end_tick", std::to_string(slice.end_tick)},
        {"link_envelopes", std::move(envelopes)},
        {"trajectory_segment_index", std::to_string(slice.trajectory_segment_index)},
    };
    if (include_id)
        object.emplace("id", slice.id);
    return object;
}

Json robot_trajectory_occupancy_json(const RobotTrajectoryOccupancy& occupancy, bool include_id) {
    Json::Array trajectory;
    trajectory.reserve(occupancy.trajectory.size());
    for (const auto& waypoint : occupancy.trajectory)
        trajectory.emplace_back(timed_configuration_json(waypoint));
    Json::Array slices;
    slices.reserve(occupancy.slices.size());
    for (const auto& slice : occupancy.slices)
        slices.emplace_back(swept_slice_json(slice, true));
    Json::Array translation;
    for (const double value : occupancy.workspace_translation)
        translation.emplace_back(value);
    Json::Object object{
        {"algorithm", occupancy.algorithm},
        {"algorithm_version", occupancy.algorithm_version},
        {"deployment_id", occupancy.deployment_id},
        {"link_padding", occupancy.link_padding},
        {"maximum_normalized_joint_width", occupancy.maximum_normalized_joint_width},
        {"maximum_subdivision_depth", std::to_string(occupancy.maximum_subdivision_depth)},
        {"robot_digest", occupancy.robot_digest},
        {"slices", std::move(slices)},
        {"storage_schema", std::to_string(occupancy.storage_schema)},
        {"timeline_id", occupancy.timeline_id},
        {"trajectory", std::move(trajectory)},
        {"workspace_frame_id", occupancy.workspace_frame_id},
        {"workspace_translation", std::move(translation)},
    };
    if (include_id)
        object.emplace("id", occupancy.id);
    return object;
}

Json continuous_occupancy_conflict_json(const ContinuousOccupancyConflict& conflict) {
    return Json::Object{
        {"clearance_lower_bound", conflict.clearance_lower_bound},
        {"first_link_index", std::to_string(conflict.first_link_index)},
        {"first_occupancy_id", conflict.first_occupancy_id},
        {"first_slice_id", conflict.first_slice_id},
        {"overlap_begin_tick", std::to_string(conflict.overlap_begin_tick)},
        {"overlap_end_tick", std::to_string(conflict.overlap_end_tick)},
        {"reason", static_cast<int>(conflict.reason)},
        {"required_margin", conflict.required_margin},
        {"second_link_index", std::to_string(conflict.second_link_index)},
        {"second_occupancy_id", conflict.second_occupancy_id},
        {"second_slice_id", conflict.second_slice_id},
    };
}

Json continuous_fleet_occupancy_report_json(const ContinuousFleetOccupancyReport& report, bool include_id) {
    Json::Array occupancy_ids;
    occupancy_ids.reserve(report.occupancy_ids.size());
    for (const auto& id : report.occupancy_ids)
        occupancy_ids.emplace_back(id);
    Json::Array conflicts;
    conflicts.reserve(report.conflicts.size());
    for (const auto& conflict : report.conflicts)
        conflicts.emplace_back(continuous_occupancy_conflict_json(conflict));
    Json::Object object{
        {"conflicts", std::move(conflicts)},
        {"link_pair_evaluations", std::to_string(report.link_pair_evaluations)},
        {"minimum_separation", report.minimum_separation},
        {"occupancy_ids", std::move(occupancy_ids)},
        {"slice_pair_evaluations", std::to_string(report.slice_pair_evaluations)},
        {"status", static_cast<int>(report.status)},
        {"timeline_id", report.timeline_id},
        {"workspace_frame_id", report.workspace_frame_id},
    };
    if (include_id)
        object.emplace("id", report.id);
    return object;
}

Json continuous_fleet_occupancy_bundle_payload_json(const ContinuousFleetOccupancyBundle& bundle,
                                                    bool include_id) {
    Json::Array occupancies;
    occupancies.reserve(bundle.occupancies().size());
    for (const auto& occupancy : bundle.occupancies())
        occupancies.emplace_back(robot_trajectory_occupancy_json(occupancy, true));
    Json::Object object{
        {"occupancies", std::move(occupancies)},
        {"report", continuous_fleet_occupancy_report_json(bundle.report(), true)},
        {"storage_schema", std::to_string(bundle.storage_schema())},
    };
    if (include_id)
        object.emplace("id", bundle.id());
    return object;
}

std::string swept_link_occupancy_slice_identity(const RobotTrajectoryOccupancy& occupancy,
                                                const SweptLinkOccupancySlice& slice) {
    return sha256(Json(Json::Object{
                           {"algorithm", occupancy.algorithm},
                           {"algorithm_version", occupancy.algorithm_version},
                           {"deployment_id", occupancy.deployment_id},
                           {"robot_digest", occupancy.robot_digest},
                           {"slice", swept_slice_json(slice, false)},
                           {"timeline_id", occupancy.timeline_id},
                           {"workspace_frame_id", occupancy.workspace_frame_id},
                       })
                      .dump(false));
}

std::string robot_trajectory_occupancy_identity(const RobotTrajectoryOccupancy& occupancy) {
    return sha256(robot_trajectory_occupancy_json(occupancy, false).dump(false));
}

std::string continuous_fleet_occupancy_report_identity(const ContinuousFleetOccupancyReport& report) {
    return sha256(continuous_fleet_occupancy_report_json(report, false).dump(false));
}

std::string continuous_fleet_occupancy_bundle_identity(const ContinuousFleetOccupancyBundle& bundle) {
    return sha256(continuous_fleet_occupancy_bundle_payload_json(bundle, false).dump(false));
}

} // namespace internal

bool RobotTrajectoryOccupancy::valid() const {
    if (storage_schema != 1 || !internal::valid_sha256(id) || !valid_identifier(timeline_id) ||
        !valid_identifier(workspace_frame_id) || !valid_identifier(deployment_id) ||
        !internal::valid_sha256(robot_digest) || !valid_translation(workspace_translation) ||
        algorithm != kOccupancyAlgorithm || algorithm_version != kOccupancyAlgorithmVersion ||
        maximum_subdivision_depth > kMaximumSubdivisionDepth ||
        !std::isfinite(maximum_normalized_joint_width) || maximum_normalized_joint_width <= 0.0 ||
        !std::isfinite(link_padding) || link_padding < 0.0 || trajectory.size() < 2 || slices.empty()) {
        return false;
    }
    const std::size_t dimension = trajectory.front().configuration.size();
    if (dimension == 0)
        return false;
    for (std::size_t index = 0; index < trajectory.size(); ++index) {
        if (trajectory[index].configuration.size() != dimension ||
            !std::all_of(trajectory[index].configuration.begin(), trajectory[index].configuration.end(),
                         [](double value) { return std::isfinite(value); }) ||
            (index > 0 && trajectory[index - 1].tick >= trajectory[index].tick)) {
            return false;
        }
    }
    const std::size_t link_count = slices.front().link_envelopes.size();
    if (link_count == 0)
        return false;
    std::size_t current_segment = 0;
    std::uint64_t expected_begin = trajectory.front().tick;
    for (std::size_t index = 0; index < slices.size(); ++index) {
        const auto& slice = slices[index];
        if (!internal::valid_sha256(slice.id) || slice.trajectory_segment_index >= trajectory.size() - 1 ||
            slice.begin_tick < trajectory[slice.trajectory_segment_index].tick ||
            slice.end_tick > trajectory[slice.trajectory_segment_index + 1].tick ||
            slice.begin_tick >= slice.end_tick || slice.configuration_domain.dimension() != dimension ||
            !slice.configuration_domain.valid() || slice.link_envelopes.size() != link_count ||
            !std::all_of(slice.link_envelopes.begin(), slice.link_envelopes.end(),
                         [](const WorkspaceAabb& box) { return box.valid(); }) ||
            internal::swept_link_occupancy_slice_identity(*this, slice) != slice.id ||
            (index > 0 && !slice_less(slices[index - 1], slice))) {
            return false;
        }
        if (slice.trajectory_segment_index != current_segment || slice.begin_tick != expected_begin)
            return false;
        const auto& segment_begin = trajectory[current_segment];
        const auto& segment_end = trajectory[current_segment + 1];
        const auto configuration_at = [&](std::uint64_t tick) {
            const double fraction = static_cast<double>(tick - segment_begin.tick) /
                                    static_cast<double>(segment_end.tick - segment_begin.tick);
            return interpolate(segment_begin.configuration, segment_end.configuration, fraction);
        };
        const auto expected_domain =
            endpoint_domain(configuration_at(slice.begin_tick), configuration_at(slice.end_tick));
        if (expected_domain.axes() != slice.configuration_domain.axes())
            return false;
        expected_begin = slice.end_tick;
        if (expected_begin == segment_end.tick) {
            ++current_segment;
            if (current_segment + 1 < trajectory.size())
                expected_begin = trajectory[current_segment].tick;
        }
    }
    if (current_segment + 1 != trajectory.size())
        return false;
    return internal::robot_trajectory_occupancy_identity(*this) == id;
}

Result<RobotTrajectoryOccupancy> build_robot_trajectory_occupancy(
    const SerialRobotModel& robot, std::string timeline_id, std::string workspace_frame_id,
    std::string deployment_id, std::array<double, 3> workspace_translation,
    std::span<const TimedConfiguration> trajectory, const ContinuousOccupancyBuildOptions& options) {
    auto robot_status = robot.validate();
    if (!robot_status)
        return robot_status.error();
    if (!valid_build_options(options) || !valid_identifier(timeline_id) ||
        !valid_identifier(workspace_frame_id) || !valid_identifier(deployment_id) ||
        !valid_translation(workspace_translation)) {
        return Result<RobotTrajectoryOccupancy>::failure(
            StatusCode::InvalidArgument, "continuous occupancy metadata or build options are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<RobotTrajectoryOccupancy>::failure(StatusCode::Cancelled,
                                                         "continuous occupancy build was cancelled");
    }
    if (trajectory.size() > options.maximum_input_waypoints) {
        return Result<RobotTrajectoryOccupancy>::failure(StatusCode::ResourceLimit,
                                                         "trajectory exceeds configured waypoint limit");
    }
    if (trajectory.size() < 2) {
        return Result<RobotTrajectoryOccupancy>::failure(StatusCode::InvalidArgument,
                                                         "trajectory must contain at least two waypoints");
    }
    for (std::size_t index = 0; index < trajectory.size(); ++index) {
        auto configuration_status = validate_configuration(trajectory[index].configuration, robot.dimension(),
                                                           "trajectory waypoint " + std::to_string(index));
        if (!configuration_status)
            return configuration_status.error();
        if (!configuration_within_limits(robot, trajectory[index].configuration)) {
            return Result<RobotTrajectoryOccupancy>::failure(
                StatusCode::InvalidArgument, "trajectory waypoint is outside robot joint limits",
                std::to_string(index));
        }
        if (index > 0 && trajectory[index - 1].tick >= trajectory[index].tick) {
            return Result<RobotTrajectoryOccupancy>::failure(StatusCode::InvalidArgument,
                                                             "trajectory ticks must be strictly increasing");
        }
    }

    RobotTrajectoryOccupancy result;
    result.timeline_id = std::move(timeline_id);
    result.workspace_frame_id = std::move(workspace_frame_id);
    result.deployment_id = std::move(deployment_id);
    result.robot_digest = robot.digest();
    result.workspace_translation = workspace_translation;
    result.algorithm = kOccupancyAlgorithm;
    result.algorithm_version = kOccupancyAlgorithmVersion;
    result.maximum_subdivision_depth = options.maximum_subdivision_depth;
    result.maximum_normalized_joint_width = options.maximum_normalized_joint_width;
    result.link_padding = options.link_padding;
    result.trajectory.assign(trajectory.begin(), trajectory.end());

    std::size_t link_envelope_count = 0;
    for (std::size_t segment = 0; segment + 1 < trajectory.size(); ++segment) {
        std::vector<PendingSlice> pending;
        pending.push_back(PendingSlice{
            segment,
            trajectory[segment].tick,
            trajectory[segment + 1].tick,
            trajectory[segment].configuration,
            trajectory[segment + 1].configuration,
            0,
        });
        while (!pending.empty()) {
            if (options.cancellation.cancelled()) {
                return Result<RobotTrajectoryOccupancy>::failure(StatusCode::Cancelled,
                                                                 "continuous occupancy build was cancelled");
            }
            PendingSlice candidate = std::move(pending.back());
            pending.pop_back();
            const auto domain = endpoint_domain(candidate.begin_configuration, candidate.end_configuration);
            const bool has_integer_midpoint = candidate.end_tick - candidate.begin_tick > 1;
            if (normalized_joint_width(robot, domain) > options.maximum_normalized_joint_width &&
                candidate.depth < options.maximum_subdivision_depth && has_integer_midpoint) {
                const std::uint64_t midpoint_tick =
                    candidate.begin_tick + (candidate.end_tick - candidate.begin_tick) / 2;
                const double fraction =
                    static_cast<double>(midpoint_tick - trajectory[segment].tick) /
                    static_cast<double>(trajectory[segment + 1].tick - trajectory[segment].tick);
                auto midpoint = interpolate(trajectory[segment].configuration,
                                            trajectory[segment + 1].configuration, fraction);
                pending.push_back(PendingSlice{
                    segment,
                    midpoint_tick,
                    candidate.end_tick,
                    midpoint,
                    std::move(candidate.end_configuration),
                    candidate.depth + 1,
                });
                pending.push_back(PendingSlice{
                    segment,
                    candidate.begin_tick,
                    midpoint_tick,
                    std::move(candidate.begin_configuration),
                    std::move(midpoint),
                    candidate.depth + 1,
                });
                continue;
            }
            if (result.slices.size() >= options.maximum_slices) {
                return Result<RobotTrajectoryOccupancy>::failure(
                    StatusCode::ResourceLimit, "continuous occupancy exceeds configured slice limit");
            }
            if (robot.link_count() > options.maximum_link_envelopes - link_envelope_count) {
                return Result<RobotTrajectoryOccupancy>::failure(
                    StatusCode::ResourceLimit, "continuous occupancy exceeds configured link-envelope limit");
            }
            auto envelope =
                compute_ifk_aa_link_envelope(robot, domain, EnvelopeOptions{options.link_padding});
            if (!envelope)
                return envelope.error();
            auto translated = translated_envelopes(envelope.value(), workspace_translation);
            if (!translated)
                return translated.error();
            SweptLinkOccupancySlice slice;
            slice.trajectory_segment_index = segment;
            slice.begin_tick = candidate.begin_tick;
            slice.end_tick = candidate.end_tick;
            slice.configuration_domain = domain;
            slice.link_envelopes = std::move(translated).value();
            slice.id = internal::swept_link_occupancy_slice_identity(result, slice);
            result.slices.push_back(std::move(slice));
            link_envelope_count += robot.link_count();
        }
    }
    std::sort(result.slices.begin(), result.slices.end(), slice_less);
    result.id = internal::robot_trajectory_occupancy_identity(result);
    if (!result.valid()) {
        return Result<RobotTrajectoryOccupancy>::failure(
            StatusCode::InternalError, "continuous occupancy builder produced an invalid result");
    }
    return result;
}

Result<void> verify_robot_trajectory_occupancy(const SerialRobotModel& robot,
                                               const RobotTrajectoryOccupancy& occupancy,
                                               const ContinuousOccupancyReplayOptions& options) {
    if (!valid_replay_options(options)) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "continuous occupancy replay options are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<void>::failure(StatusCode::Cancelled, "continuous occupancy replay was cancelled");
    }
    if (!occupancy.valid()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "continuous occupancy content or identity is invalid");
    }
    if (occupancy.robot_digest != robot.digest()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "continuous occupancy robot identity does not match");
    }
    if (occupancy.trajectory.size() > options.maximum_input_waypoints ||
        occupancy.slices.size() > options.maximum_slices) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "continuous occupancy exceeds replay resource limits");
    }
    std::size_t envelope_count = 0;
    for (const auto& slice : occupancy.slices) {
        if (slice.link_envelopes.size() > options.maximum_link_envelopes - envelope_count) {
            return Result<void>::failure(StatusCode::ResourceLimit,
                                         "continuous occupancy exceeds replay link-envelope limit");
        }
        envelope_count += slice.link_envelopes.size();
    }
    ContinuousOccupancyBuildOptions replay;
    replay.maximum_input_waypoints = options.maximum_input_waypoints;
    replay.maximum_slices = options.maximum_slices;
    replay.maximum_link_envelopes = options.maximum_link_envelopes;
    replay.maximum_subdivision_depth = occupancy.maximum_subdivision_depth;
    replay.maximum_normalized_joint_width = occupancy.maximum_normalized_joint_width;
    replay.link_padding = occupancy.link_padding;
    replay.cancellation = options.cancellation;
    auto rebuilt = build_robot_trajectory_occupancy(
        robot, occupancy.timeline_id, occupancy.workspace_frame_id, occupancy.deployment_id,
        occupancy.workspace_translation, occupancy.trajectory, replay);
    if (!rebuilt)
        return rebuilt.error();
    if (rebuilt.value().id != occupancy.id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "continuous occupancy replay identity does not match stored content",
                                     occupancy.id);
    }
    return Result<void>::success();
}

bool ContinuousFleetOccupancyReport::valid() const {
    if (!internal::valid_sha256(id) || !valid_identifier(timeline_id) ||
        !valid_identifier(workspace_frame_id) ||
        (status != ContinuousFleetOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes &&
         status != ContinuousFleetOccupancyStatus::PotentialConflict) ||
        !std::isfinite(minimum_separation) || minimum_separation < 0.0 || occupancy_ids.size() < 2 ||
        !std::is_sorted(occupancy_ids.begin(), occupancy_ids.end()) ||
        std::adjacent_find(occupancy_ids.begin(), occupancy_ids.end()) != occupancy_ids.end() ||
        !std::all_of(occupancy_ids.begin(), occupancy_ids.end(), internal::valid_sha256) ||
        !std::is_sorted(conflicts.begin(), conflicts.end(), conflict_less) ||
        !std::all_of(conflicts.begin(), conflicts.end(), valid_conflict) ||
        conflicts.size() > slice_pair_evaluations || conflicts.size() > link_pair_evaluations ||
        (status == ContinuousFleetOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes &&
         !conflicts.empty()) ||
        (status == ContinuousFleetOccupancyStatus::PotentialConflict && conflicts.empty())) {
        return false;
    }
    for (std::size_t index = 0; index < conflicts.size(); ++index) {
        const auto& conflict = conflicts[index];
        if (!std::binary_search(occupancy_ids.begin(), occupancy_ids.end(), conflict.first_occupancy_id) ||
            !std::binary_search(occupancy_ids.begin(), occupancy_ids.end(), conflict.second_occupancy_id) ||
            conflict.required_margin != minimum_separation ||
            (index > 0 && !conflict_less(conflicts[index - 1], conflict))) {
            return false;
        }
    }
    return internal::continuous_fleet_occupancy_report_identity(*this) == id;
}

Result<ContinuousFleetOccupancyReport>
analyze_continuous_fleet_occupancy(std::span<const RobotTrajectoryOccupancy> occupancies,
                                   const ContinuousFleetOccupancyOptions& options) {
    if (!valid_analysis_options(options)) {
        return Result<ContinuousFleetOccupancyReport>::failure(
            StatusCode::InvalidArgument, "continuous fleet occupancy analysis options are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<ContinuousFleetOccupancyReport>::failure(
            StatusCode::Cancelled, "continuous fleet occupancy analysis was cancelled");
    }
    if (occupancies.size() < 2) {
        return Result<ContinuousFleetOccupancyReport>::failure(
            StatusCode::InvalidArgument,
            "continuous fleet occupancy analysis requires at least two occupancies");
    }
    if (occupancies.size() > options.maximum_occupancies) {
        return Result<ContinuousFleetOccupancyReport>::failure(
            StatusCode::ResourceLimit, "continuous fleet occupancy count exceeds configured limit");
    }
    std::vector<const RobotTrajectoryOccupancy*> ordered;
    ordered.reserve(occupancies.size());
    for (const auto& occupancy : occupancies) {
        if (!occupancy.valid()) {
            return Result<ContinuousFleetOccupancyReport>::failure(
                StatusCode::IdentityMismatch, "continuous fleet occupancy input is invalid", occupancy.id);
        }
        ordered.push_back(&occupancy);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* first, const auto* second) {
        return std::tie(first->deployment_id, first->id) < std::tie(second->deployment_id, second->id);
    });
    const auto& timeline_id = ordered.front()->timeline_id;
    const auto& workspace_frame_id = ordered.front()->workspace_frame_id;
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        if (ordered[index]->timeline_id != timeline_id ||
            ordered[index]->workspace_frame_id != workspace_frame_id) {
            return Result<ContinuousFleetOccupancyReport>::failure(
                StatusCode::IdentityMismatch,
                "continuous occupancies do not share a timeline and workspace frame");
        }
        if (index > 0 && ordered[index - 1]->deployment_id == ordered[index]->deployment_id) {
            return Result<ContinuousFleetOccupancyReport>::failure(
                StatusCode::InvalidArgument, "continuous occupancy deployment identifiers must be unique",
                ordered[index]->deployment_id);
        }
    }

    ContinuousFleetOccupancyReport result;
    result.timeline_id = timeline_id;
    result.workspace_frame_id = workspace_frame_id;
    result.minimum_separation = options.minimum_separation;
    for (const auto* occupancy : ordered)
        result.occupancy_ids.push_back(occupancy->id);
    std::sort(result.occupancy_ids.begin(), result.occupancy_ids.end());

    for (std::size_t first_index = 0; first_index < ordered.size(); ++first_index) {
        for (std::size_t second_index = first_index + 1; second_index < ordered.size(); ++second_index) {
            const RobotTrajectoryOccupancy* first = ordered[first_index];
            const RobotTrajectoryOccupancy* second = ordered[second_index];
            if (first->id > second->id)
                std::swap(first, second);
            std::size_t first_slice_index = 0;
            std::size_t second_slice_index = 0;
            while (first_slice_index < first->slices.size() && second_slice_index < second->slices.size()) {
                if (options.cancellation.cancelled()) {
                    return Result<ContinuousFleetOccupancyReport>::failure(
                        StatusCode::Cancelled, "continuous fleet occupancy analysis was cancelled");
                }
                const auto& first_slice = first->slices[first_slice_index];
                const auto& second_slice = second->slices[second_slice_index];
                const auto overlap_begin = std::max(first_slice.begin_tick, second_slice.begin_tick);
                const auto overlap_end = std::min(first_slice.end_tick, second_slice.end_tick);
                if (!checked_increment(result.slice_pair_evaluations,
                                       options.maximum_slice_pair_evaluations)) {
                    return Result<ContinuousFleetOccupancyReport>::failure(
                        StatusCode::ResourceLimit, "continuous occupancy slice-pair budget was exhausted");
                }
                if (overlap_begin < overlap_end) {
                    double best_clearance = std::numeric_limits<double>::infinity();
                    std::size_t best_first_link = 0;
                    std::size_t best_second_link = 0;
                    bool found_overlap = false;
                    for (std::size_t first_link = 0; first_link < first_slice.link_envelopes.size();
                         ++first_link) {
                        for (std::size_t second_link = 0; second_link < second_slice.link_envelopes.size();
                             ++second_link) {
                            if (options.cancellation.cancelled()) {
                                return Result<ContinuousFleetOccupancyReport>::failure(
                                    StatusCode::Cancelled,
                                    "continuous fleet occupancy analysis was cancelled");
                            }
                            if (!checked_increment(result.link_pair_evaluations,
                                                   options.maximum_link_pair_evaluations)) {
                                return Result<ContinuousFleetOccupancyReport>::failure(
                                    StatusCode::ResourceLimit,
                                    "continuous occupancy link-pair budget was exhausted");
                            }
                            const double raw_clearance =
                                first_slice.link_envelopes[first_link].distance_lower_bound(
                                    second_slice.link_envelopes[second_link]);
                            double clearance = 0.0;
                            if (std::isfinite(raw_clearance)) {
                                const double rounding_guard = std::numeric_limits<double>::epsilon() * 16.0 *
                                                              std::max(1.0, raw_clearance);
                                clearance = std::max(0.0, raw_clearance - rounding_guard);
                            }
                            const bool overlaps = first_slice.link_envelopes[first_link].overlaps(
                                second_slice.link_envelopes[second_link]);
                            if ((overlaps && !found_overlap) ||
                                (overlaps == found_overlap && clearance < best_clearance)) {
                                found_overlap = overlaps;
                                best_clearance = clearance;
                                best_first_link = first_link;
                                best_second_link = second_link;
                            }
                        }
                    }
                    if (found_overlap || best_clearance < options.minimum_separation) {
                        if (result.conflicts.size() >= options.maximum_conflicts) {
                            return Result<ContinuousFleetOccupancyReport>::failure(
                                StatusCode::ResourceLimit,
                                "continuous occupancy conflict budget was exhausted");
                        }
                        ContinuousOccupancyConflict conflict;
                        conflict.first_occupancy_id = first->id;
                        conflict.second_occupancy_id = second->id;
                        conflict.first_slice_id = first_slice.id;
                        conflict.second_slice_id = second_slice.id;
                        conflict.first_link_index = best_first_link;
                        conflict.second_link_index = best_second_link;
                        conflict.overlap_begin_tick = overlap_begin;
                        conflict.overlap_end_tick = overlap_end;
                        conflict.reason = found_overlap
                                              ? ContinuousOccupancyConflictReason::SweptEnvelopeOverlap
                                              : ContinuousOccupancyConflictReason::SeparationMarginViolated;
                        conflict.clearance_lower_bound = found_overlap ? 0.0 : best_clearance;
                        conflict.required_margin = options.minimum_separation;
                        result.conflicts.push_back(std::move(conflict));
                    }
                }
                if (first_slice.end_tick <= second_slice.end_tick)
                    ++first_slice_index;
                if (second_slice.end_tick <= first_slice.end_tick)
                    ++second_slice_index;
            }
        }
    }
    std::sort(result.conflicts.begin(), result.conflicts.end(), conflict_less);
    result.status = result.conflicts.empty()
                        ? ContinuousFleetOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes
                        : ContinuousFleetOccupancyStatus::PotentialConflict;
    result.id = internal::continuous_fleet_occupancy_report_identity(result);
    if (!result.valid()) {
        return Result<ContinuousFleetOccupancyReport>::failure(
            StatusCode::InternalError, "continuous fleet occupancy analysis produced an invalid report");
    }
    return result;
}

Result<ContinuousFleetOccupancyBundle>
ContinuousFleetOccupancyBundle::create(std::vector<RobotTrajectoryOccupancy> occupancies,
                                       const ContinuousFleetOccupancyOptions& options) {
    std::sort(occupancies.begin(), occupancies.end(), [](const auto& first, const auto& second) {
        return std::tie(first.deployment_id, first.id) < std::tie(second.deployment_id, second.id);
    });
    auto report = analyze_continuous_fleet_occupancy(occupancies, options);
    if (!report)
        return report.error();
    ContinuousFleetOccupancyBundle result;
    result.occupancies_ = std::move(occupancies);
    result.report_ = std::move(report).value();
    result.id_ = internal::continuous_fleet_occupancy_bundle_identity(result);
    if (!result.valid()) {
        return Result<ContinuousFleetOccupancyBundle>::failure(
            StatusCode::InternalError,
            "continuous fleet occupancy bundle builder produced an invalid result");
    }
    return result;
}

bool ContinuousFleetOccupancyBundle::valid() const {
    if (storage_schema_ != 1 || !internal::valid_sha256(id_) || occupancies_.size() < 2 || !report_.valid() ||
        !std::is_sorted(occupancies_.begin(), occupancies_.end(),
                        [](const auto& first, const auto& second) {
                            return std::tie(first.deployment_id, first.id) <
                                   std::tie(second.deployment_id, second.id);
                        }) ||
        !std::all_of(occupancies_.begin(), occupancies_.end(),
                     [](const auto& occupancy) { return occupancy.valid(); })) {
        return false;
    }
    std::vector<std::string> ids;
    ids.reserve(occupancies_.size());
    for (std::size_t index = 0; index < occupancies_.size(); ++index) {
        const auto& occupancy = occupancies_[index];
        if (occupancy.timeline_id != report_.timeline_id ||
            occupancy.workspace_frame_id != report_.workspace_frame_id ||
            (index > 0 && occupancies_[index - 1].deployment_id == occupancy.deployment_id)) {
            return false;
        }
        ids.push_back(occupancy.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids == report_.occupancy_ids && internal::continuous_fleet_occupancy_bundle_identity(*this) == id_;
}

Result<void> ContinuousFleetOccupancyBundle::save(const std::filesystem::path& path,
                                                  const SaveOptions& options) const {
    return save_continuous_fleet_occupancy_bundle(*this, path, options);
}

Result<ContinuousFleetOccupancyBundle>
ContinuousFleetOccupancyBundle::load(const std::filesystem::path& path,
                                     const ContinuousFleetOccupancyBundleLoadOptions& options) {
    return load_continuous_fleet_occupancy_bundle(path, options);
}

std::string continuous_occupancy_conflict_reason_name(ContinuousOccupancyConflictReason reason) {
    switch (reason) {
    case ContinuousOccupancyConflictReason::SweptEnvelopeOverlap:
        return "SWEPT_ENVELOPE_OVERLAP";
    case ContinuousOccupancyConflictReason::SeparationMarginViolated:
        return "SEPARATION_MARGIN_VIOLATED";
    }
    return "UNKNOWN";
}

std::string continuous_fleet_occupancy_status_name(ContinuousFleetOccupancyStatus status) {
    switch (status) {
    case ContinuousFleetOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes:
        return "CERTIFIED_SEPARATED_UNDER_SWEPT_ENVELOPES";
    case ContinuousFleetOccupancyStatus::PotentialConflict:
        return "POTENTIAL_CONFLICT";
    }
    return "UNKNOWN";
}

} // namespace rbfsafe
