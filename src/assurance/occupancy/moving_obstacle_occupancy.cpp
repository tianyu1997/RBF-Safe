#include <rbfsafe/modules/assurance.h>

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

constexpr std::uint32_t kMovingObstacleStorageSchema = 1;
constexpr std::uint32_t kRobotSceneBundleStorageSchema = 1;
constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr const char* kMovingObstacleAlgorithm = "piecewise-linear-workspace-aabb-swept-union";
constexpr const char* kMovingObstacleAlgorithmVersion = "1";

bool valid_identifier(const std::string& value) {
    return !value.empty() && value.size() <= kMaximumIdentifierBytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_build_options(const MovingObstacleOccupancyBuildOptions& options) {
    return options.maximum_input_waypoints >= 2 && options.maximum_slices > 0 &&
           std::isfinite(options.obstacle_padding) && options.obstacle_padding >= 0.0;
}

bool valid_replay_options(const MovingObstacleOccupancyReplayOptions& options) {
    return options.maximum_input_waypoints >= 2 && options.maximum_slices > 0;
}

bool valid_analysis_options(const ContinuousRobotSceneOccupancyOptions& options) {
    return std::isfinite(options.minimum_separation) && options.minimum_separation >= 0.0 &&
           options.maximum_robot_occupancies > 0 && options.maximum_obstacle_occupancies > 0 &&
           options.maximum_conflicts > 0 && options.maximum_slice_pair_evaluations > 0 &&
           options.maximum_link_evaluations > 0;
}

bool checked_increment(std::size_t& value, std::size_t maximum) {
    if (value >= maximum)
        return false;
    ++value;
    return true;
}

Result<WorkspaceAabb> swept_bounds(const WorkspaceAabb& first, const WorkspaceAabb& second, double padding) {
    if (!first.valid() || !second.valid() || !std::isfinite(padding) || padding < 0.0) {
        return Result<WorkspaceAabb>::failure(StatusCode::InvalidArgument,
                                              "moving-obstacle waypoint bounds or padding are invalid");
    }
    WorkspaceAabb result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double lower = std::min(first.lower[axis], second.lower[axis]) - padding;
        const double upper = std::max(first.upper[axis], second.upper[axis]) + padding;
        if (!std::isfinite(lower) || !std::isfinite(upper)) {
            return Result<WorkspaceAabb>::failure(StatusCode::InvalidArgument,
                                                  "moving-obstacle swept bounds overflowed");
        }
        result.lower[axis] = std::nextafter(lower, -std::numeric_limits<double>::infinity());
        result.upper[axis] = std::nextafter(upper, std::numeric_limits<double>::infinity());
    }
    if (!result.valid()) {
        return Result<WorkspaceAabb>::failure(StatusCode::InvalidArgument,
                                              "moving-obstacle swept bounds are invalid");
    }
    return result;
}

bool slice_less(const MovingObstacleOccupancySlice& first, const MovingObstacleOccupancySlice& second) {
    return std::tie(first.begin_tick, first.end_tick, first.trajectory_segment_index, first.id) <
           std::tie(second.begin_tick, second.end_tick, second.trajectory_segment_index, second.id);
}

bool conflict_less(const ContinuousRobotSceneOccupancyConflict& first,
                   const ContinuousRobotSceneOccupancyConflict& second) {
    return std::tie(first.robot_occupancy_id, first.obstacle_occupancy_id, first.overlap_begin_tick,
                    first.overlap_end_tick, first.robot_slice_id, first.obstacle_slice_id,
                    first.robot_link_index, first.reason) <
           std::tie(second.robot_occupancy_id, second.obstacle_occupancy_id, second.overlap_begin_tick,
                    second.overlap_end_tick, second.robot_slice_id, second.obstacle_slice_id,
                    second.robot_link_index, second.reason);
}

bool valid_conflict(const ContinuousRobotSceneOccupancyConflict& conflict) {
    return internal::valid_sha256(conflict.robot_occupancy_id) &&
           internal::valid_sha256(conflict.obstacle_occupancy_id) &&
           internal::valid_sha256(conflict.robot_slice_id) &&
           internal::valid_sha256(conflict.obstacle_slice_id) &&
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

double conservative_clearance(const WorkspaceAabb& first, const WorkspaceAabb& second) {
    const double raw = first.distance_lower_bound(second);
    if (!std::isfinite(raw))
        return 0.0;
    const double guard = std::numeric_limits<double>::epsilon() * 16.0 * std::max(1.0, raw);
    return std::max(0.0, raw - guard);
}

} // namespace

namespace internal {

Json timed_workspace_aabb_json(const TimedWorkspaceAabb& waypoint) {
    return Json::Object{{"bounds", workspace_aabb_json(waypoint.bounds)},
                        {"tick", std::to_string(waypoint.tick)}};
}

Json moving_obstacle_occupancy_slice_json(const MovingObstacleOccupancySlice& slice, bool include_id) {
    Json::Object object{
        {"begin_tick", std::to_string(slice.begin_tick)},
        {"end_tick", std::to_string(slice.end_tick)},
        {"swept_bounds", workspace_aabb_json(slice.swept_bounds)},
        {"trajectory_segment_index", std::to_string(slice.trajectory_segment_index)},
    };
    if (include_id)
        object.emplace("id", slice.id);
    return object;
}

Json moving_obstacle_occupancy_json(const MovingObstacleOccupancy& occupancy, bool include_id) {
    Json::Array trajectory;
    trajectory.reserve(occupancy.trajectory.size());
    for (const auto& waypoint : occupancy.trajectory)
        trajectory.emplace_back(timed_workspace_aabb_json(waypoint));
    Json::Array slices;
    slices.reserve(occupancy.slices.size());
    for (const auto& slice : occupancy.slices)
        slices.emplace_back(moving_obstacle_occupancy_slice_json(slice, true));
    Json::Object object{
        {"algorithm", occupancy.algorithm},
        {"algorithm_version", occupancy.algorithm_version},
        {"obstacle_id", occupancy.obstacle_id},
        {"obstacle_padding", occupancy.obstacle_padding},
        {"slices", std::move(slices)},
        {"storage_schema", std::to_string(occupancy.storage_schema)},
        {"timeline_id", occupancy.timeline_id},
        {"trajectory", std::move(trajectory)},
        {"workspace_frame_id", occupancy.workspace_frame_id},
    };
    if (include_id)
        object.emplace("id", occupancy.id);
    return object;
}

Json continuous_robot_scene_occupancy_conflict_json(const ContinuousRobotSceneOccupancyConflict& conflict) {
    return Json::Object{
        {"clearance_lower_bound", conflict.clearance_lower_bound},
        {"obstacle_occupancy_id", conflict.obstacle_occupancy_id},
        {"obstacle_slice_id", conflict.obstacle_slice_id},
        {"overlap_begin_tick", std::to_string(conflict.overlap_begin_tick)},
        {"overlap_end_tick", std::to_string(conflict.overlap_end_tick)},
        {"reason", static_cast<int>(conflict.reason)},
        {"required_margin", conflict.required_margin},
        {"robot_link_index", std::to_string(conflict.robot_link_index)},
        {"robot_occupancy_id", conflict.robot_occupancy_id},
        {"robot_slice_id", conflict.robot_slice_id},
    };
}

Json continuous_robot_scene_occupancy_report_json(const ContinuousRobotSceneOccupancyReport& report,
                                                  bool include_id) {
    Json::Array robot_ids;
    robot_ids.reserve(report.robot_occupancy_ids.size());
    for (const auto& id : report.robot_occupancy_ids)
        robot_ids.emplace_back(id);
    Json::Array obstacle_ids;
    obstacle_ids.reserve(report.obstacle_occupancy_ids.size());
    for (const auto& id : report.obstacle_occupancy_ids)
        obstacle_ids.emplace_back(id);
    Json::Array conflicts;
    conflicts.reserve(report.conflicts.size());
    for (const auto& conflict : report.conflicts)
        conflicts.emplace_back(continuous_robot_scene_occupancy_conflict_json(conflict));
    Json::Object object{
        {"begin_tick", std::to_string(report.begin_tick)},
        {"conflicts", std::move(conflicts)},
        {"end_tick", std::to_string(report.end_tick)},
        {"link_evaluations", std::to_string(report.link_evaluations)},
        {"minimum_separation", report.minimum_separation},
        {"obstacle_occupancy_ids", std::move(obstacle_ids)},
        {"robot_occupancy_ids", std::move(robot_ids)},
        {"slice_pair_evaluations", std::to_string(report.slice_pair_evaluations)},
        {"status", static_cast<int>(report.status)},
        {"timeline_id", report.timeline_id},
        {"workspace_frame_id", report.workspace_frame_id},
    };
    if (include_id)
        object.emplace("id", report.id);
    return object;
}

Json continuous_robot_scene_occupancy_bundle_payload_json(const ContinuousRobotSceneOccupancyBundle& bundle,
                                                          bool include_id) {
    Json::Array robots;
    robots.reserve(bundle.robot_occupancies().size());
    for (const auto& occupancy : bundle.robot_occupancies())
        robots.emplace_back(robot_trajectory_occupancy_json(occupancy, true));
    Json::Array obstacles;
    obstacles.reserve(bundle.obstacle_occupancies().size());
    for (const auto& occupancy : bundle.obstacle_occupancies())
        obstacles.emplace_back(moving_obstacle_occupancy_json(occupancy, true));
    Json::Object object{
        {"obstacle_occupancies", std::move(obstacles)},
        {"report", continuous_robot_scene_occupancy_report_json(bundle.report(), true)},
        {"robot_occupancies", std::move(robots)},
        {"storage_schema", std::to_string(bundle.storage_schema())},
    };
    if (include_id)
        object.emplace("id", bundle.id());
    return object;
}

std::string moving_obstacle_occupancy_slice_identity(const MovingObstacleOccupancy& occupancy,
                                                     const MovingObstacleOccupancySlice& slice) {
    return sha256(Json(Json::Object{
                           {"algorithm", occupancy.algorithm},
                           {"algorithm_version", occupancy.algorithm_version},
                           {"obstacle_id", occupancy.obstacle_id},
                           {"obstacle_padding", occupancy.obstacle_padding},
                           {"slice", moving_obstacle_occupancy_slice_json(slice, false)},
                           {"timeline_id", occupancy.timeline_id},
                           {"workspace_frame_id", occupancy.workspace_frame_id},
                       })
                      .dump(false));
}

std::string moving_obstacle_occupancy_identity(const MovingObstacleOccupancy& occupancy) {
    return sha256(moving_obstacle_occupancy_json(occupancy, false).dump(false));
}

std::string
continuous_robot_scene_occupancy_report_identity(const ContinuousRobotSceneOccupancyReport& report) {
    return sha256(continuous_robot_scene_occupancy_report_json(report, false).dump(false));
}

std::string
continuous_robot_scene_occupancy_bundle_identity(const ContinuousRobotSceneOccupancyBundle& bundle) {
    return sha256(continuous_robot_scene_occupancy_bundle_payload_json(bundle, false).dump(false));
}

} // namespace internal

bool MovingObstacleOccupancy::valid() const {
    if (storage_schema != kMovingObstacleStorageSchema || !internal::valid_sha256(id) ||
        !valid_identifier(timeline_id) || !valid_identifier(workspace_frame_id) ||
        !valid_identifier(obstacle_id) || algorithm != kMovingObstacleAlgorithm ||
        algorithm_version != kMovingObstacleAlgorithmVersion || !std::isfinite(obstacle_padding) ||
        obstacle_padding < 0.0 || trajectory.size() < 2 || slices.size() != trajectory.size() - 1) {
        return false;
    }
    for (std::size_t index = 0; index < trajectory.size(); ++index) {
        if (!trajectory[index].bounds.valid() ||
            (index > 0 && trajectory[index - 1].tick >= trajectory[index].tick)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < slices.size(); ++index) {
        const auto& slice = slices[index];
        auto expected =
            swept_bounds(trajectory[index].bounds, trajectory[index + 1].bounds, obstacle_padding);
        if (!expected || !internal::valid_sha256(slice.id) || slice.trajectory_segment_index != index ||
            slice.begin_tick != trajectory[index].tick || slice.end_tick != trajectory[index + 1].tick ||
            !slice.swept_bounds.valid() || slice.swept_bounds.lower != expected.value().lower ||
            slice.swept_bounds.upper != expected.value().upper ||
            internal::moving_obstacle_occupancy_slice_identity(*this, slice) != slice.id ||
            (index > 0 && !slice_less(slices[index - 1], slice))) {
            return false;
        }
    }
    return internal::moving_obstacle_occupancy_identity(*this) == id;
}

Result<MovingObstacleOccupancy>
build_moving_obstacle_occupancy(std::string timeline_id, std::string workspace_frame_id,
                                std::string obstacle_id, std::span<const TimedWorkspaceAabb> trajectory,
                                const MovingObstacleOccupancyBuildOptions& options) {
    if (!valid_identifier(timeline_id) || !valid_identifier(workspace_frame_id) ||
        !valid_identifier(obstacle_id) || !valid_build_options(options)) {
        return Result<MovingObstacleOccupancy>::failure(
            StatusCode::InvalidArgument, "moving-obstacle occupancy input or options are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<MovingObstacleOccupancy>::failure(StatusCode::Cancelled,
                                                        "moving-obstacle occupancy build was cancelled");
    }
    if (trajectory.size() < 2) {
        return Result<MovingObstacleOccupancy>::failure(
            StatusCode::InvalidArgument, "moving-obstacle occupancy requires at least two waypoints");
    }
    if (trajectory.size() > options.maximum_input_waypoints ||
        trajectory.size() - 1 > options.maximum_slices) {
        return Result<MovingObstacleOccupancy>::failure(
            StatusCode::ResourceLimit, "moving-obstacle occupancy input exceeds configured limits");
    }

    MovingObstacleOccupancy result;
    result.storage_schema = kMovingObstacleStorageSchema;
    result.timeline_id = std::move(timeline_id);
    result.workspace_frame_id = std::move(workspace_frame_id);
    result.obstacle_id = std::move(obstacle_id);
    result.algorithm = kMovingObstacleAlgorithm;
    result.algorithm_version = kMovingObstacleAlgorithmVersion;
    result.obstacle_padding = options.obstacle_padding;
    result.trajectory.assign(trajectory.begin(), trajectory.end());
    result.slices.reserve(result.trajectory.size() - 1);
    for (std::size_t index = 0; index < result.trajectory.size(); ++index) {
        if (options.cancellation.cancelled()) {
            return Result<MovingObstacleOccupancy>::failure(StatusCode::Cancelled,
                                                            "moving-obstacle occupancy build was cancelled");
        }
        if (!result.trajectory[index].bounds.valid() ||
            (index > 0 && result.trajectory[index - 1].tick >= result.trajectory[index].tick)) {
            return Result<MovingObstacleOccupancy>::failure(
                StatusCode::InvalidArgument,
                "moving-obstacle waypoints must have valid bounds and increasing ticks");
        }
        if (index + 1 == result.trajectory.size())
            continue;
        auto bounds = swept_bounds(result.trajectory[index].bounds, result.trajectory[index + 1].bounds,
                                   result.obstacle_padding);
        if (!bounds)
            return bounds.error();
        MovingObstacleOccupancySlice slice;
        slice.trajectory_segment_index = index;
        slice.begin_tick = result.trajectory[index].tick;
        slice.end_tick = result.trajectory[index + 1].tick;
        slice.swept_bounds = bounds.value();
        slice.id = internal::moving_obstacle_occupancy_slice_identity(result, slice);
        result.slices.push_back(std::move(slice));
    }
    result.id = internal::moving_obstacle_occupancy_identity(result);
    if (!result.valid()) {
        return Result<MovingObstacleOccupancy>::failure(
            StatusCode::InternalError, "moving-obstacle occupancy builder produced an invalid result");
    }
    return result;
}

Result<void> verify_moving_obstacle_occupancy(const MovingObstacleOccupancy& occupancy,
                                              const MovingObstacleOccupancyReplayOptions& options) {
    if (!occupancy.valid() || !valid_replay_options(options)) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "moving-obstacle occupancy replay input or options are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<void>::failure(StatusCode::Cancelled, "moving-obstacle occupancy replay was cancelled");
    }
    if (occupancy.trajectory.size() > options.maximum_input_waypoints ||
        occupancy.slices.size() > options.maximum_slices) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "moving-obstacle occupancy replay exceeds configured limits");
    }
    MovingObstacleOccupancyBuildOptions replay;
    replay.maximum_input_waypoints = options.maximum_input_waypoints;
    replay.maximum_slices = options.maximum_slices;
    replay.obstacle_padding = occupancy.obstacle_padding;
    replay.cancellation = options.cancellation;
    auto rebuilt = build_moving_obstacle_occupancy(occupancy.timeline_id, occupancy.workspace_frame_id,
                                                   occupancy.obstacle_id, occupancy.trajectory, replay);
    if (!rebuilt)
        return rebuilt.error();
    if (rebuilt.value().id != occupancy.id) {
        return Result<void>::failure(
            StatusCode::IdentityMismatch,
            "moving-obstacle occupancy replay identity does not match stored content", occupancy.id);
    }
    return Result<void>::success();
}

bool ContinuousRobotSceneOccupancyReport::valid() const {
    if (!internal::valid_sha256(id) || !valid_identifier(timeline_id) ||
        !valid_identifier(workspace_frame_id) || begin_tick >= end_tick ||
        (status != ContinuousRobotSceneOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes &&
         status != ContinuousRobotSceneOccupancyStatus::PotentialConflict) ||
        !std::isfinite(minimum_separation) || minimum_separation < 0.0 || robot_occupancy_ids.empty() ||
        obstacle_occupancy_ids.empty() ||
        !std::is_sorted(robot_occupancy_ids.begin(), robot_occupancy_ids.end()) ||
        std::adjacent_find(robot_occupancy_ids.begin(), robot_occupancy_ids.end()) !=
            robot_occupancy_ids.end() ||
        !std::all_of(robot_occupancy_ids.begin(), robot_occupancy_ids.end(), internal::valid_sha256) ||
        !std::is_sorted(obstacle_occupancy_ids.begin(), obstacle_occupancy_ids.end()) ||
        std::adjacent_find(obstacle_occupancy_ids.begin(), obstacle_occupancy_ids.end()) !=
            obstacle_occupancy_ids.end() ||
        !std::all_of(obstacle_occupancy_ids.begin(), obstacle_occupancy_ids.end(), internal::valid_sha256) ||
        !std::is_sorted(conflicts.begin(), conflicts.end(), conflict_less) ||
        !std::all_of(conflicts.begin(), conflicts.end(), valid_conflict) ||
        conflicts.size() > slice_pair_evaluations || conflicts.size() > link_evaluations ||
        (status == ContinuousRobotSceneOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes &&
         !conflicts.empty()) ||
        (status == ContinuousRobotSceneOccupancyStatus::PotentialConflict && conflicts.empty())) {
        return false;
    }
    for (std::size_t index = 0; index < conflicts.size(); ++index) {
        const auto& conflict = conflicts[index];
        if (!std::binary_search(robot_occupancy_ids.begin(), robot_occupancy_ids.end(),
                                conflict.robot_occupancy_id) ||
            !std::binary_search(obstacle_occupancy_ids.begin(), obstacle_occupancy_ids.end(),
                                conflict.obstacle_occupancy_id) ||
            conflict.overlap_begin_tick < begin_tick || conflict.overlap_end_tick > end_tick ||
            conflict.required_margin != minimum_separation ||
            (index > 0 && !conflict_less(conflicts[index - 1], conflict))) {
            return false;
        }
    }
    return internal::continuous_robot_scene_occupancy_report_identity(*this) == id;
}

Result<ContinuousRobotSceneOccupancyReport>
analyze_continuous_robot_scene_occupancy(std::span<const RobotTrajectoryOccupancy> robot_occupancies,
                                         std::span<const MovingObstacleOccupancy> obstacle_occupancies,
                                         const ContinuousRobotSceneOccupancyOptions& options) {
    if (!valid_analysis_options(options)) {
        return Result<ContinuousRobotSceneOccupancyReport>::failure(
            StatusCode::InvalidArgument, "continuous robot-scene occupancy analysis options are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<ContinuousRobotSceneOccupancyReport>::failure(
            StatusCode::Cancelled, "continuous robot-scene occupancy analysis was cancelled");
    }
    if (robot_occupancies.empty() || obstacle_occupancies.empty()) {
        return Result<ContinuousRobotSceneOccupancyReport>::failure(
            StatusCode::InvalidArgument, "continuous robot-scene analysis requires robots and obstacles");
    }
    if (robot_occupancies.size() > options.maximum_robot_occupancies ||
        obstacle_occupancies.size() > options.maximum_obstacle_occupancies) {
        return Result<ContinuousRobotSceneOccupancyReport>::failure(
            StatusCode::ResourceLimit, "continuous robot-scene occupancy count exceeds configured limits");
    }

    std::vector<const RobotTrajectoryOccupancy*> robots;
    robots.reserve(robot_occupancies.size());
    for (const auto& occupancy : robot_occupancies) {
        if (!occupancy.valid()) {
            return Result<ContinuousRobotSceneOccupancyReport>::failure(
                StatusCode::IdentityMismatch, "continuous robot-scene robot occupancy is invalid",
                occupancy.id);
        }
        robots.push_back(&occupancy);
    }
    std::sort(robots.begin(), robots.end(), [](const auto* first, const auto* second) {
        return std::tie(first->deployment_id, first->id) < std::tie(second->deployment_id, second->id);
    });
    std::vector<const MovingObstacleOccupancy*> obstacles;
    obstacles.reserve(obstacle_occupancies.size());
    for (const auto& occupancy : obstacle_occupancies) {
        if (!occupancy.valid()) {
            return Result<ContinuousRobotSceneOccupancyReport>::failure(
                StatusCode::IdentityMismatch, "continuous robot-scene obstacle occupancy is invalid",
                occupancy.id);
        }
        obstacles.push_back(&occupancy);
    }
    std::sort(obstacles.begin(), obstacles.end(), [](const auto* first, const auto* second) {
        return std::tie(first->obstacle_id, first->id) < std::tie(second->obstacle_id, second->id);
    });

    const auto& timeline_id = robots.front()->timeline_id;
    const auto& workspace_frame_id = robots.front()->workspace_frame_id;
    const std::uint64_t begin_tick = robots.front()->trajectory.front().tick;
    const std::uint64_t end_tick = robots.front()->trajectory.back().tick;
    for (std::size_t index = 0; index < robots.size(); ++index) {
        const auto& occupancy = *robots[index];
        if (occupancy.timeline_id != timeline_id || occupancy.workspace_frame_id != workspace_frame_id ||
            occupancy.trajectory.front().tick != begin_tick || occupancy.trajectory.back().tick != end_tick) {
            return Result<ContinuousRobotSceneOccupancyReport>::failure(
                StatusCode::IdentityMismatch,
                "robot occupancies do not share one complete timeline window and frame");
        }
        if (index > 0 && robots[index - 1]->deployment_id == occupancy.deployment_id) {
            return Result<ContinuousRobotSceneOccupancyReport>::failure(
                StatusCode::InvalidArgument, "robot occupancy deployment identifiers must be unique",
                occupancy.deployment_id);
        }
    }
    for (std::size_t index = 0; index < obstacles.size(); ++index) {
        const auto& occupancy = *obstacles[index];
        if (occupancy.timeline_id != timeline_id || occupancy.workspace_frame_id != workspace_frame_id ||
            occupancy.trajectory.front().tick != begin_tick || occupancy.trajectory.back().tick != end_tick) {
            return Result<ContinuousRobotSceneOccupancyReport>::failure(
                StatusCode::IdentityMismatch,
                "obstacle occupancies do not cover the complete robot timeline window and frame");
        }
        if (index > 0 && obstacles[index - 1]->obstacle_id == occupancy.obstacle_id) {
            return Result<ContinuousRobotSceneOccupancyReport>::failure(
                StatusCode::InvalidArgument, "moving-obstacle identifiers must be unique",
                occupancy.obstacle_id);
        }
    }

    ContinuousRobotSceneOccupancyReport result;
    result.timeline_id = timeline_id;
    result.workspace_frame_id = workspace_frame_id;
    result.begin_tick = begin_tick;
    result.end_tick = end_tick;
    result.minimum_separation = options.minimum_separation;
    for (const auto* occupancy : robots)
        result.robot_occupancy_ids.push_back(occupancy->id);
    for (const auto* occupancy : obstacles)
        result.obstacle_occupancy_ids.push_back(occupancy->id);
    std::sort(result.robot_occupancy_ids.begin(), result.robot_occupancy_ids.end());
    std::sort(result.obstacle_occupancy_ids.begin(), result.obstacle_occupancy_ids.end());

    for (const auto* robot : robots) {
        for (const auto* obstacle : obstacles) {
            std::size_t robot_slice_index = 0;
            std::size_t obstacle_slice_index = 0;
            while (robot_slice_index < robot->slices.size() &&
                   obstacle_slice_index < obstacle->slices.size()) {
                if (options.cancellation.cancelled()) {
                    return Result<ContinuousRobotSceneOccupancyReport>::failure(
                        StatusCode::Cancelled, "continuous robot-scene occupancy analysis was cancelled");
                }
                const auto& robot_slice = robot->slices[robot_slice_index];
                const auto& obstacle_slice = obstacle->slices[obstacle_slice_index];
                const auto overlap_begin = std::max(robot_slice.begin_tick, obstacle_slice.begin_tick);
                const auto overlap_end = std::min(robot_slice.end_tick, obstacle_slice.end_tick);
                if (!checked_increment(result.slice_pair_evaluations,
                                       options.maximum_slice_pair_evaluations)) {
                    return Result<ContinuousRobotSceneOccupancyReport>::failure(
                        StatusCode::ResourceLimit, "continuous robot-scene slice-pair budget was exhausted");
                }
                if (overlap_begin < overlap_end) {
                    double best_clearance = std::numeric_limits<double>::infinity();
                    std::size_t best_link = 0;
                    bool found_overlap = false;
                    for (std::size_t link = 0; link < robot_slice.link_envelopes.size(); ++link) {
                        if (options.cancellation.cancelled()) {
                            return Result<ContinuousRobotSceneOccupancyReport>::failure(
                                StatusCode::Cancelled,
                                "continuous robot-scene occupancy analysis was cancelled");
                        }
                        if (!checked_increment(result.link_evaluations, options.maximum_link_evaluations)) {
                            return Result<ContinuousRobotSceneOccupancyReport>::failure(
                                StatusCode::ResourceLimit,
                                "continuous robot-scene link budget was exhausted");
                        }
                        const auto& link_bounds = robot_slice.link_envelopes[link];
                        const bool overlaps = link_bounds.overlaps(obstacle_slice.swept_bounds);
                        const double clearance =
                            conservative_clearance(link_bounds, obstacle_slice.swept_bounds);
                        if ((overlaps && !found_overlap) ||
                            (overlaps == found_overlap && clearance < best_clearance)) {
                            found_overlap = overlaps;
                            best_clearance = clearance;
                            best_link = link;
                        }
                    }
                    if (found_overlap || best_clearance < options.minimum_separation) {
                        if (result.conflicts.size() >= options.maximum_conflicts) {
                            return Result<ContinuousRobotSceneOccupancyReport>::failure(
                                StatusCode::ResourceLimit,
                                "continuous robot-scene conflict budget was exhausted");
                        }
                        ContinuousRobotSceneOccupancyConflict conflict;
                        conflict.robot_occupancy_id = robot->id;
                        conflict.obstacle_occupancy_id = obstacle->id;
                        conflict.robot_slice_id = robot_slice.id;
                        conflict.obstacle_slice_id = obstacle_slice.id;
                        conflict.robot_link_index = best_link;
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
                if (robot_slice.end_tick <= obstacle_slice.end_tick)
                    ++robot_slice_index;
                if (obstacle_slice.end_tick <= robot_slice.end_tick)
                    ++obstacle_slice_index;
            }
        }
    }
    std::sort(result.conflicts.begin(), result.conflicts.end(), conflict_less);
    result.status = result.conflicts.empty()
                        ? ContinuousRobotSceneOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes
                        : ContinuousRobotSceneOccupancyStatus::PotentialConflict;
    result.id = internal::continuous_robot_scene_occupancy_report_identity(result);
    if (!result.valid()) {
        return Result<ContinuousRobotSceneOccupancyReport>::failure(
            StatusCode::InternalError, "continuous robot-scene analysis produced an invalid report");
    }
    return result;
}

Result<ContinuousRobotSceneOccupancyBundle>
ContinuousRobotSceneOccupancyBundle::create(std::vector<RobotTrajectoryOccupancy> robot_occupancies,
                                            std::vector<MovingObstacleOccupancy> obstacle_occupancies,
                                            const ContinuousRobotSceneOccupancyOptions& options) {
    std::sort(robot_occupancies.begin(), robot_occupancies.end(), [](const auto& first, const auto& second) {
        return std::tie(first.deployment_id, first.id) < std::tie(second.deployment_id, second.id);
    });
    std::sort(obstacle_occupancies.begin(), obstacle_occupancies.end(),
              [](const auto& first, const auto& second) {
                  return std::tie(first.obstacle_id, first.id) < std::tie(second.obstacle_id, second.id);
              });
    auto report = analyze_continuous_robot_scene_occupancy(robot_occupancies, obstacle_occupancies, options);
    if (!report)
        return report.error();
    ContinuousRobotSceneOccupancyBundle result;
    result.storage_schema_ = kRobotSceneBundleStorageSchema;
    result.robot_occupancies_ = std::move(robot_occupancies);
    result.obstacle_occupancies_ = std::move(obstacle_occupancies);
    result.report_ = std::move(report).value();
    result.id_ = internal::continuous_robot_scene_occupancy_bundle_identity(result);
    if (!result.valid()) {
        return Result<ContinuousRobotSceneOccupancyBundle>::failure(
            StatusCode::InternalError, "continuous robot-scene bundle builder produced an invalid result");
    }
    return result;
}

bool ContinuousRobotSceneOccupancyBundle::valid() const {
    if (storage_schema_ != kRobotSceneBundleStorageSchema || !internal::valid_sha256(id_) ||
        robot_occupancies_.empty() || obstacle_occupancies_.empty() || !report_.valid() ||
        !std::is_sorted(robot_occupancies_.begin(), robot_occupancies_.end(),
                        [](const auto& first, const auto& second) {
                            return std::tie(first.deployment_id, first.id) <
                                   std::tie(second.deployment_id, second.id);
                        }) ||
        !std::is_sorted(obstacle_occupancies_.begin(), obstacle_occupancies_.end(),
                        [](const auto& first, const auto& second) {
                            return std::tie(first.obstacle_id, first.id) <
                                   std::tie(second.obstacle_id, second.id);
                        }) ||
        !std::all_of(robot_occupancies_.begin(), robot_occupancies_.end(),
                     [](const auto& occupancy) { return occupancy.valid(); }) ||
        !std::all_of(obstacle_occupancies_.begin(), obstacle_occupancies_.end(),
                     [](const auto& occupancy) { return occupancy.valid(); })) {
        return false;
    }
    std::vector<std::string> robot_ids;
    robot_ids.reserve(robot_occupancies_.size());
    for (std::size_t index = 0; index < robot_occupancies_.size(); ++index) {
        const auto& occupancy = robot_occupancies_[index];
        if (occupancy.timeline_id != report_.timeline_id ||
            occupancy.workspace_frame_id != report_.workspace_frame_id ||
            occupancy.trajectory.front().tick != report_.begin_tick ||
            occupancy.trajectory.back().tick != report_.end_tick ||
            (index > 0 && robot_occupancies_[index - 1].deployment_id == occupancy.deployment_id)) {
            return false;
        }
        robot_ids.push_back(occupancy.id);
    }
    std::vector<std::string> obstacle_ids;
    obstacle_ids.reserve(obstacle_occupancies_.size());
    for (std::size_t index = 0; index < obstacle_occupancies_.size(); ++index) {
        const auto& occupancy = obstacle_occupancies_[index];
        if (occupancy.timeline_id != report_.timeline_id ||
            occupancy.workspace_frame_id != report_.workspace_frame_id ||
            occupancy.trajectory.front().tick != report_.begin_tick ||
            occupancy.trajectory.back().tick != report_.end_tick ||
            (index > 0 && obstacle_occupancies_[index - 1].obstacle_id == occupancy.obstacle_id)) {
            return false;
        }
        obstacle_ids.push_back(occupancy.id);
    }
    std::sort(robot_ids.begin(), robot_ids.end());
    std::sort(obstacle_ids.begin(), obstacle_ids.end());
    return robot_ids == report_.robot_occupancy_ids && obstacle_ids == report_.obstacle_occupancy_ids &&
           internal::continuous_robot_scene_occupancy_bundle_identity(*this) == id_;
}

Result<void> ContinuousRobotSceneOccupancyBundle::save(const std::filesystem::path& path,
                                                       const SaveOptions& options) const {
    return save_continuous_robot_scene_occupancy_bundle(*this, path, options);
}

Result<ContinuousRobotSceneOccupancyBundle>
ContinuousRobotSceneOccupancyBundle::load(const std::filesystem::path& path,
                                          const ContinuousRobotSceneOccupancyBundleLoadOptions& options) {
    return load_continuous_robot_scene_occupancy_bundle(path, options);
}

std::string continuous_robot_scene_occupancy_status_name(ContinuousRobotSceneOccupancyStatus status) {
    switch (status) {
    case ContinuousRobotSceneOccupancyStatus::CertifiedSeparatedUnderSweptEnvelopes:
        return "CERTIFIED_SEPARATED_UNDER_SWEPT_ENVELOPES";
    case ContinuousRobotSceneOccupancyStatus::PotentialConflict:
        return "POTENTIAL_CONFLICT";
    }
    return "UNKNOWN";
}

} // namespace rbfsafe
