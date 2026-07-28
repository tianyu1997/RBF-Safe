#pragma once

#include <rbfsafe/atlas.h>
#include <rbfsafe/geometry.h>
#include <rbfsafe/result.h>
#include <rbfsafe/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

struct TimedConfiguration {
    std::uint64_t tick = 0;
    Configuration configuration;
};

struct TimedWorkspaceAabb {
    std::uint64_t tick = 0;
    WorkspaceAabb bounds;
};

struct DeploymentFrameBounds {
    DeploymentFrameBounds() = default;

    // Row-major rotation from the robot-local frame into workspace_frame_id.
    std::array<double, 9> rotation{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    std::array<double, 3> translation{};
    std::array<double, 3> translation_uncertainty{};
    // Maximum geodesic rotation error about any axis, in radians.
    double angular_uncertainty_radians = 0.0;

    bool valid() const;
    bool exact() const;
};

struct SweptLinkOccupancySlice {
    std::string id;
    std::size_t trajectory_segment_index = 0;
    std::uint64_t begin_tick = 0;
    std::uint64_t end_tick = 0;
    CspaceAabb configuration_domain;
    std::vector<WorkspaceAabb> link_envelopes;
};

struct ContinuousOccupancyBuildOptions {
    std::size_t maximum_input_waypoints = 100'000;
    std::size_t maximum_slices = 1'000'000;
    std::size_t maximum_link_envelopes = 10'000'000;
    std::size_t maximum_subdivision_depth = 16;
    double maximum_normalized_joint_width = 0.05;
    double link_padding = 0.0;
    CancellationToken cancellation;
};

struct ContinuousOccupancyReplayOptions {
    std::size_t maximum_input_waypoints = 100'000;
    std::size_t maximum_slices = 1'000'000;
    std::size_t maximum_link_envelopes = 10'000'000;
    CancellationToken cancellation;
};

struct RobotTrajectoryOccupancy {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string timeline_id;
    std::string workspace_frame_id;
    std::string deployment_id;
    std::string robot_digest;
    std::array<double, 3> workspace_translation{};
    std::array<double, 9> workspace_rotation{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    std::array<double, 3> workspace_translation_uncertainty{};
    double workspace_angular_uncertainty_radians = 0.0;
    std::string algorithm;
    std::string algorithm_version;
    std::size_t maximum_subdivision_depth = 0;
    double maximum_normalized_joint_width = 0.0;
    double link_padding = 0.0;
    std::vector<TimedConfiguration> trajectory;
    std::vector<SweptLinkOccupancySlice> slices;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<RobotTrajectoryOccupancy> build_robot_trajectory_occupancy(
    const SerialRobotModel& robot, std::string timeline_id, std::string workspace_frame_id,
    std::string deployment_id, std::array<double, 3> workspace_translation,
    std::span<const TimedConfiguration> trajectory, const ContinuousOccupancyBuildOptions& options = {});

Result<RobotTrajectoryOccupancy> build_robot_trajectory_occupancy_in_frame(
    const SerialRobotModel& robot, std::string timeline_id, std::string workspace_frame_id,
    std::string deployment_id, const DeploymentFrameBounds& deployment_frame,
    std::span<const TimedConfiguration> trajectory, const ContinuousOccupancyBuildOptions& options = {});

Result<void> verify_robot_trajectory_occupancy(const SerialRobotModel& robot,
                                               const RobotTrajectoryOccupancy& occupancy,
                                               const ContinuousOccupancyReplayOptions& options = {});

struct MovingObstacleOccupancySlice {
    std::string id;
    std::size_t trajectory_segment_index = 0;
    std::uint64_t begin_tick = 0;
    std::uint64_t end_tick = 0;
    WorkspaceAabb swept_bounds;
};

struct MovingObstacleOccupancyBuildOptions {
    std::size_t maximum_input_waypoints = 100'000;
    std::size_t maximum_slices = 1'000'000;
    double obstacle_padding = 0.0;
    CancellationToken cancellation;
};

struct MovingObstacleOccupancyReplayOptions {
    std::size_t maximum_input_waypoints = 100'000;
    std::size_t maximum_slices = 1'000'000;
    CancellationToken cancellation;
};

struct MovingObstacleOccupancy {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string timeline_id;
    std::string workspace_frame_id;
    std::string obstacle_id;
    std::string algorithm;
    std::string algorithm_version;
    double obstacle_padding = 0.0;
    std::vector<TimedWorkspaceAabb> trajectory;
    std::vector<MovingObstacleOccupancySlice> slices;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<MovingObstacleOccupancy>
build_moving_obstacle_occupancy(std::string timeline_id, std::string workspace_frame_id,
                                std::string obstacle_id, std::span<const TimedWorkspaceAabb> trajectory,
                                const MovingObstacleOccupancyBuildOptions& options = {});

Result<void> verify_moving_obstacle_occupancy(const MovingObstacleOccupancy& occupancy,
                                              const MovingObstacleOccupancyReplayOptions& options = {});

enum class ContinuousOccupancyConflictReason : std::uint8_t {
    SweptEnvelopeOverlap = 0,
    SeparationMarginViolated = 1,
};

struct ContinuousOccupancyConflict {
    std::string first_occupancy_id;
    std::string second_occupancy_id;
    std::string first_slice_id;
    std::string second_slice_id;
    std::size_t first_link_index = 0;
    std::size_t second_link_index = 0;
    std::uint64_t overlap_begin_tick = 0;
    std::uint64_t overlap_end_tick = 0;
    ContinuousOccupancyConflictReason reason = ContinuousOccupancyConflictReason::SweptEnvelopeOverlap;
    double clearance_lower_bound = 0.0;
    double required_margin = 0.0;
};

enum class ContinuousFleetOccupancyStatus : std::uint8_t {
    CertifiedSeparatedUnderSweptEnvelopes = 0,
    PotentialConflict = 1,
};

struct ContinuousFleetOccupancyOptions {
    double minimum_separation = 0.0;
    std::size_t maximum_occupancies = 10'000;
    std::size_t maximum_conflicts = 1'000'000;
    std::size_t maximum_slice_pair_evaluations = 10'000'000;
    std::size_t maximum_link_pair_evaluations = 100'000'000;
    CancellationToken cancellation;
};

struct ContinuousFleetOccupancyReport {
    std::string id;
    std::string timeline_id;
    std::string workspace_frame_id;
    ContinuousFleetOccupancyStatus status = ContinuousFleetOccupancyStatus::PotentialConflict;
    double minimum_separation = 0.0;
    std::vector<std::string> occupancy_ids;
    std::vector<ContinuousOccupancyConflict> conflicts;
    std::size_t slice_pair_evaluations = 0;
    std::size_t link_pair_evaluations = 0;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<ContinuousFleetOccupancyReport>
analyze_continuous_fleet_occupancy(std::span<const RobotTrajectoryOccupancy> occupancies,
                                   const ContinuousFleetOccupancyOptions& options = {});

struct ContinuousRobotSceneOccupancyConflict {
    std::string robot_occupancy_id;
    std::string obstacle_occupancy_id;
    std::string robot_slice_id;
    std::string obstacle_slice_id;
    std::size_t robot_link_index = 0;
    std::uint64_t overlap_begin_tick = 0;
    std::uint64_t overlap_end_tick = 0;
    ContinuousOccupancyConflictReason reason = ContinuousOccupancyConflictReason::SweptEnvelopeOverlap;
    double clearance_lower_bound = 0.0;
    double required_margin = 0.0;
};

enum class ContinuousRobotSceneOccupancyStatus : std::uint8_t {
    CertifiedSeparatedUnderSweptEnvelopes = 0,
    PotentialConflict = 1,
};

struct ContinuousRobotSceneOccupancyOptions {
    double minimum_separation = 0.0;
    std::size_t maximum_robot_occupancies = 10'000;
    std::size_t maximum_obstacle_occupancies = 100'000;
    std::size_t maximum_conflicts = 1'000'000;
    std::size_t maximum_slice_pair_evaluations = 100'000'000;
    std::size_t maximum_link_evaluations = 1'000'000'000;
    CancellationToken cancellation;
};

struct ContinuousRobotSceneOccupancyReport {
    std::string id;
    std::string timeline_id;
    std::string workspace_frame_id;
    std::uint64_t begin_tick = 0;
    std::uint64_t end_tick = 0;
    ContinuousRobotSceneOccupancyStatus status = ContinuousRobotSceneOccupancyStatus::PotentialConflict;
    double minimum_separation = 0.0;
    std::vector<std::string> robot_occupancy_ids;
    std::vector<std::string> obstacle_occupancy_ids;
    std::vector<ContinuousRobotSceneOccupancyConflict> conflicts;
    std::size_t slice_pair_evaluations = 0;
    std::size_t link_evaluations = 0;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<ContinuousRobotSceneOccupancyReport>
analyze_continuous_robot_scene_occupancy(std::span<const RobotTrajectoryOccupancy> robot_occupancies,
                                         std::span<const MovingObstacleOccupancy> obstacle_occupancies,
                                         const ContinuousRobotSceneOccupancyOptions& options = {});

struct ContinuousFleetOccupancyBundleLoadOptions {
    std::size_t maximum_occupancies = 10'000;
    std::size_t maximum_input_waypoints = 1'000'000;
    std::size_t maximum_dimension = 1'024;
    std::size_t maximum_slices = 10'000'000;
    std::size_t maximum_link_envelopes = 100'000'000;
    std::size_t maximum_conflicts = 10'000'000;
    std::size_t maximum_slice_pair_evaluations = 10'000'000;
    std::size_t maximum_link_pair_evaluations = 100'000'000;
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
    CancellationToken cancellation;
};

class ContinuousFleetOccupancyBundle {
  public:
    static Result<ContinuousFleetOccupancyBundle> create(std::vector<RobotTrajectoryOccupancy> occupancies,
                                                         const ContinuousFleetOccupancyOptions& options = {});

    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    const std::string& id() const noexcept { return id_; }
    const std::vector<RobotTrajectoryOccupancy>& occupancies() const noexcept { return occupancies_; }
    const ContinuousFleetOccupancyReport& report() const noexcept { return report_; }

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<ContinuousFleetOccupancyBundle>
    load(const std::filesystem::path& path, const ContinuousFleetOccupancyBundleLoadOptions& options = {});

  private:
    friend Result<void> save_continuous_fleet_occupancy_bundle(const ContinuousFleetOccupancyBundle&,
                                                               const std::filesystem::path&,
                                                               const SaveOptions&);
    friend Result<ContinuousFleetOccupancyBundle>
    load_continuous_fleet_occupancy_bundle(const std::filesystem::path&,
                                           const ContinuousFleetOccupancyBundleLoadOptions&);
    friend Result<ContinuousFleetOccupancyBundle>
    load_continuous_fleet_occupancy_bundle(std::span<const std::byte>,
                                           const ContinuousFleetOccupancyBundleLoadOptions&);

    std::uint32_t storage_schema_ = 1;
    std::string id_;
    std::vector<RobotTrajectoryOccupancy> occupancies_;
    ContinuousFleetOccupancyReport report_;
};

Result<void> save_continuous_fleet_occupancy_bundle(const ContinuousFleetOccupancyBundle& bundle,
                                                    const std::filesystem::path& path,
                                                    const SaveOptions& options);
Result<ContinuousFleetOccupancyBundle>
load_continuous_fleet_occupancy_bundle(const std::filesystem::path& path,
                                       const ContinuousFleetOccupancyBundleLoadOptions& options);
Result<ContinuousFleetOccupancyBundle>
load_continuous_fleet_occupancy_bundle(std::span<const std::byte> payload,
                                       const ContinuousFleetOccupancyBundleLoadOptions& options = {});

std::string continuous_occupancy_conflict_reason_name(ContinuousOccupancyConflictReason reason);
std::string continuous_fleet_occupancy_status_name(ContinuousFleetOccupancyStatus status);

struct ContinuousRobotSceneOccupancyBundleLoadOptions {
    std::size_t maximum_robot_occupancies = 10'000;
    std::size_t maximum_obstacle_occupancies = 100'000;
    std::size_t maximum_robot_input_waypoints = 1'000'000;
    std::size_t maximum_obstacle_input_waypoints = 10'000'000;
    std::size_t maximum_dimension = 1'024;
    std::size_t maximum_robot_slices = 10'000'000;
    std::size_t maximum_obstacle_slices = 10'000'000;
    std::size_t maximum_link_envelopes = 100'000'000;
    std::size_t maximum_conflicts = 10'000'000;
    std::size_t maximum_slice_pair_evaluations = 100'000'000;
    std::size_t maximum_link_evaluations = 1'000'000'000;
    std::uintmax_t maximum_payload_bytes = 536'870'912ULL;
    CancellationToken cancellation;
};

class ContinuousRobotSceneOccupancyBundle {
  public:
    static Result<ContinuousRobotSceneOccupancyBundle>
    create(std::vector<RobotTrajectoryOccupancy> robot_occupancies,
           std::vector<MovingObstacleOccupancy> obstacle_occupancies,
           const ContinuousRobotSceneOccupancyOptions& options = {});

    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    const std::string& id() const noexcept { return id_; }
    const std::vector<RobotTrajectoryOccupancy>& robot_occupancies() const noexcept {
        return robot_occupancies_;
    }
    const std::vector<MovingObstacleOccupancy>& obstacle_occupancies() const noexcept {
        return obstacle_occupancies_;
    }
    const ContinuousRobotSceneOccupancyReport& report() const noexcept { return report_; }

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<ContinuousRobotSceneOccupancyBundle>
    load(const std::filesystem::path& path,
         const ContinuousRobotSceneOccupancyBundleLoadOptions& options = {});

  private:
    friend Result<void>
    save_continuous_robot_scene_occupancy_bundle(const ContinuousRobotSceneOccupancyBundle&,
                                                 const std::filesystem::path&, const SaveOptions&);
    friend Result<ContinuousRobotSceneOccupancyBundle>
    load_continuous_robot_scene_occupancy_bundle(const std::filesystem::path&,
                                                 const ContinuousRobotSceneOccupancyBundleLoadOptions&);
    friend Result<ContinuousRobotSceneOccupancyBundle>
    load_continuous_robot_scene_occupancy_bundle(std::span<const std::byte>,
                                                 const ContinuousRobotSceneOccupancyBundleLoadOptions&);

    std::uint32_t storage_schema_ = 1;
    std::string id_;
    std::vector<RobotTrajectoryOccupancy> robot_occupancies_;
    std::vector<MovingObstacleOccupancy> obstacle_occupancies_;
    ContinuousRobotSceneOccupancyReport report_;
};

Result<void> save_continuous_robot_scene_occupancy_bundle(const ContinuousRobotSceneOccupancyBundle& bundle,
                                                          const std::filesystem::path& path,
                                                          const SaveOptions& options);
Result<ContinuousRobotSceneOccupancyBundle>
load_continuous_robot_scene_occupancy_bundle(const std::filesystem::path& path,
                                             const ContinuousRobotSceneOccupancyBundleLoadOptions& options);
Result<ContinuousRobotSceneOccupancyBundle> load_continuous_robot_scene_occupancy_bundle(
    std::span<const std::byte> payload, const ContinuousRobotSceneOccupancyBundleLoadOptions& options = {});

std::string continuous_robot_scene_occupancy_status_name(ContinuousRobotSceneOccupancyStatus status);

} // namespace rbfsafe
