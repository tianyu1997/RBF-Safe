#pragma once

#include <rbfsafe/modules/applications.h>

// Continuous fleet and obstacle occupancy.

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

// Persistent safety memory.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class MemoryArtifactType : std::uint8_t {
    SafeAtlas = 0,
    RegionDatabase = 1,
    SafeCorridor = 2,
    TrajectoryAudit = 3,
    PolicyFeedback = 4,
    RuntimeTrace = 5,
    FleetSchedule = 6,
};

enum class MemoryArtifactState : std::uint8_t {
    Active = 0,
    Stale = 1,
    Quarantined = 2,
    Retired = 3,
};

enum class MemoryEventType : std::uint8_t {
    Registered = 0,
    StateTransition = 1,
    ReuseRecorded = 2,
    SceneInvalidated = 3,
};

enum class ReuseDisposition : std::uint8_t {
    Direct = 0,
    RequiresRevalidation = 1,
    Ineligible = 2,
};

struct MemoryArtifactInput {
    MemoryArtifactType type = MemoryArtifactType::SafeAtlas;
    std::string deployment_id;
    std::string robot_digest;
    std::string scene_digest;
    std::string task_id;
    std::string content_digest;
    std::string locator;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
    std::vector<std::string> tags;
};

struct MemoryArtifact {
    std::string id;
    MemoryArtifactType type = MemoryArtifactType::SafeAtlas;
    MemoryArtifactState state = MemoryArtifactState::Active;
    std::string deployment_id;
    std::string robot_digest;
    std::string scene_digest;
    std::string task_id;
    std::string content_digest;
    std::string locator;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
    std::vector<std::string> tags;
    std::uint64_t generation = 0;
    std::uint64_t registered_sequence = 0;
};

struct MemoryEvent {
    std::string id;
    std::uint64_t sequence = 0;
    MemoryEventType type = MemoryEventType::Registered;
    std::string artifact_id;
    MemoryArtifactState previous_state = MemoryArtifactState::Active;
    MemoryArtifactState current_state = MemoryArtifactState::Active;
    std::string task_id;
    std::string detail;
};

struct MemoryReuseQuery {
    std::string deployment_id;
    std::string robot_digest;
    std::string scene_digest;
    std::string target_task_id;
    std::optional<MemoryArtifactType> type;
    EvidenceLevel minimum_evidence = EvidenceLevel::Unknown;
    std::vector<std::string> required_tags;
    bool include_same_task = true;
    bool include_revalidation_candidates = false;
    std::size_t maximum_results = 100'000;
};

struct MemoryReuseCandidate {
    MemoryArtifact artifact;
    ReuseDisposition disposition = ReuseDisposition::Ineligible;
    bool cross_task = false;
    std::string reason;
};

struct SafetyMemorySummary {
    std::uint64_t artifacts = 0;
    std::uint64_t active = 0;
    std::uint64_t stale = 0;
    std::uint64_t quarantined = 0;
    std::uint64_t retired = 0;
    std::uint64_t events = 0;
    std::uint64_t recorded_reuses = 0;
};

struct SafetyMemoryLoadOptions {
    std::size_t maximum_artifacts = 1'000'000;
    std::size_t maximum_events = 4'000'000;
    std::uintmax_t maximum_payload_bytes = 536'870'912ULL;
};

class SafetyMemory;
Result<void> save_safety_memory_directory(const SafetyMemory& memory, const std::filesystem::path& directory,
                                          const SaveOptions& options);
Result<SafetyMemory> load_safety_memory_directory(const std::filesystem::path& directory,
                                                  const SafetyMemoryLoadOptions& options);

class SafetyMemory {
  public:
    SafetyMemory() = default;

    const std::vector<MemoryArtifact>& artifacts() const noexcept { return artifacts_; }
    const std::vector<MemoryEvent>& events() const noexcept { return events_; }
    std::uint64_t next_sequence() const noexcept { return next_sequence_; }
    std::string identity() const;

    Result<MemoryArtifact> register_artifact(MemoryArtifactInput input,
                                             std::size_t maximum_artifacts = 1'000'000,
                                             std::size_t maximum_events = 4'000'000);
    Result<MemoryArtifact> transition(const std::string& artifact_id, std::uint64_t expected_generation,
                                      MemoryArtifactState target_state, std::string detail,
                                      std::size_t maximum_events = 4'000'000);
    Result<std::size_t> invalidate_scene(const std::string& deployment_id, const std::string& scene_digest,
                                         std::string detail, std::size_t maximum_events = 4'000'000);

    Result<std::optional<MemoryArtifact>> artifact(const std::string& artifact_id) const;
    Result<MemoryReuseCandidate> assess_reuse(const std::string& artifact_id,
                                              const MemoryReuseQuery& query) const;
    Result<std::vector<MemoryReuseCandidate>> query_reuse(const MemoryReuseQuery& query) const;
    Result<void> record_reuse(const std::string& artifact_id, const MemoryReuseQuery& query,
                              std::string detail, std::size_t maximum_events = 4'000'000);

    SafetyMemorySummary summary() const noexcept;
    bool valid() const;

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<SafetyMemory> load(const std::filesystem::path& directory,
                                     const SafetyMemoryLoadOptions& options = {});

  private:
    friend Result<void> save_safety_memory_directory(const SafetyMemory&, const std::filesystem::path&,
                                                     const SaveOptions&);
    friend Result<SafetyMemory> load_safety_memory_directory(const std::filesystem::path&,
                                                             const SafetyMemoryLoadOptions&);

    std::vector<MemoryArtifact> artifacts_;
    std::vector<MemoryEvent> events_;
    std::uint64_t next_sequence_ = 1;
};

struct SafetyMemoryRevisionInfo {
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string memory_id;
};

struct SafetyMemoryStoreOpenOptions {
    std::size_t maximum_revisions = 1'000'000;
    std::uintmax_t maximum_metadata_bytes = 65'536ULL;
    SafetyMemoryLoadOptions memory_load;
};

class SafetyMemoryStore {
  public:
    static Result<SafetyMemoryStore> create(const std::filesystem::path& directory,
                                            const SafetyMemory& initial_memory);
    static Result<SafetyMemoryStore> open(const std::filesystem::path& directory,
                                          const SafetyMemoryStoreOpenOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const std::string& current_revision_id() const noexcept { return current_revision_id_; }
    const std::vector<SafetyMemoryRevisionInfo>& revisions() const noexcept { return revisions_; }

    Result<SafetyMemory> load_current() const;
    Result<SafetyMemory> load_revision(const std::string& revision_id) const;
    Result<SafetyMemoryRevisionInfo> publish(const SafetyMemory& memory,
                                             const std::string& expected_current_revision_id,
                                             std::size_t maximum_revisions = 1'000'000);

  private:
    std::filesystem::path directory_;
    std::string current_revision_id_;
    std::vector<SafetyMemoryRevisionInfo> revisions_;
    SafetyMemoryStoreOpenOptions options_;
};

struct FleetMember {
    std::string deployment_id;
    std::string robot_digest;
    WorkspaceAabb operating_envelope;
};

struct FleetSnapshot {
    std::string id;
    std::string fleet_id;
    std::string scene_digest;
    std::vector<FleetMember> members;
};

Result<FleetSnapshot> make_fleet_snapshot(std::string fleet_id, std::string scene_digest,
                                          std::vector<FleetMember> members);

struct FleetReservation {
    std::string id;
    std::string deployment_id;
    std::string source_artifact_id;
    WorkspaceAabb occupancy;
    std::uint64_t begin_tick = 0;
    std::uint64_t end_tick = 0;
    double separation_margin = 0.0;
};

Result<FleetReservation> make_fleet_reservation(const FleetSnapshot& fleet, const SafetyMemory& memory,
                                                std::string deployment_id, std::string source_artifact_id,
                                                WorkspaceAabb occupancy, std::uint64_t begin_tick,
                                                std::uint64_t end_tick, double separation_margin = 0.0);

enum class FleetConflictReason : std::uint8_t {
    DuplicateRobotWindow = 0,
    WorkspaceOverlap = 1,
    SeparationMarginViolated = 2,
};

struct FleetConflict {
    std::string first_reservation_id;
    std::string second_reservation_id;
    FleetConflictReason reason = FleetConflictReason::WorkspaceOverlap;
    double clearance_lower_bound = 0.0;
    double required_margin = 0.0;
};

enum class FleetScheduleStatus : std::uint8_t {
    ConflictFreeUnderDeclaredEnvelopes = 0,
    Conflicted = 1,
};

struct FleetScheduleOptions {
    std::size_t maximum_reservations = 100'000;
    std::size_t maximum_pair_evaluations = 1'000'000;
    CancellationToken cancellation;
};

struct FleetScheduleReport {
    std::string id;
    std::string fleet_snapshot_id;
    FleetScheduleStatus status = FleetScheduleStatus::Conflicted;
    std::vector<FleetReservation> reservations;
    std::vector<FleetConflict> conflicts;
    std::size_t pair_evaluations = 0;
};

Result<FleetScheduleReport> analyze_fleet_schedule(const FleetSnapshot& fleet, const SafetyMemory& memory,
                                                   std::span<const FleetReservation> reservations,
                                                   const FleetScheduleOptions& options = {});

struct FleetScheduleVersion {
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string memory_id;
    FleetSnapshot fleet;
    FleetScheduleReport report;
};

struct FleetScheduleArchiveLoadOptions {
    std::size_t maximum_versions = 100'000;
    std::size_t maximum_members = 1'000'000;
    std::size_t maximum_reservations = 1'000'000;
    std::size_t maximum_conflicts = 1'000'000;
    std::size_t maximum_pair_evaluations = 1'000'000;
    std::uintmax_t maximum_metadata_bytes = 65'536ULL;
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
};

class FleetScheduleArchive {
  public:
    static Result<FleetScheduleArchive> create(std::string fleet_id);

    const std::string& fleet_id() const noexcept { return fleet_id_; }
    const std::string& current_version_id() const noexcept { return current_version_id_; }
    const std::vector<FleetScheduleVersion>& versions() const noexcept { return versions_; }
    bool valid() const;

    Result<FleetScheduleVersion> current_version() const;
    Result<FleetScheduleVersion> version(const std::string& version_id) const;
    Result<FleetScheduleVersion> publish(const FleetSnapshot& fleet, const SafetyMemory& memory,
                                         std::span<const FleetReservation> reservations,
                                         const std::string& expected_current_version_id,
                                         const FleetScheduleOptions& schedule_options = {},
                                         std::size_t maximum_versions = 100'000);
    Result<FleetScheduleReport> verify_version(const std::string& version_id, const FleetSnapshot& fleet,
                                               const SafetyMemory& memory,
                                               const FleetScheduleOptions& options = {}) const;

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<FleetScheduleArchive> load(const std::filesystem::path& directory,
                                             const FleetScheduleArchiveLoadOptions& options = {});

  private:
    std::string fleet_id_;
    std::string current_version_id_;
    std::vector<FleetScheduleVersion> versions_;
};

std::string memory_artifact_type_name(MemoryArtifactType type);
std::string memory_artifact_state_name(MemoryArtifactState state);
std::string memory_event_type_name(MemoryEventType type);
std::string reuse_disposition_name(ReuseDisposition disposition);
std::string fleet_conflict_reason_name(FleetConflictReason reason);
std::string fleet_schedule_status_name(FleetScheduleStatus status);

} // namespace rbfsafe

// Artifact trust and attestation.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace rbfsafe {

enum class ArtifactAuthenticationAlgorithm : std::uint8_t {
    HmacSha256 = 0,
    Ed25519 = 1,
};

struct ArtifactAttestation {
    std::uint64_t sequence = 0;
    std::string id;
    std::string service_id;
    std::string key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::HmacSha256;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    std::string authentication_tag;
};

struct ArtifactVerificationOptions {
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
    CancellationToken cancellation;
};

bool valid_artifact_attestation(const ArtifactAttestation& attestation);

Result<ArtifactAttestation> attest_artifact(const MemoryArtifact& artifact,
                                            std::span<const std::byte> payload, std::string service_id,
                                            std::string key_id, std::span<const std::byte> hmac_key,
                                            std::uint64_t sequence,
                                            std::string media_type = "application/octet-stream");

Result<ArtifactAttestation> attest_artifact_file(const MemoryArtifact& artifact,
                                                 const std::filesystem::path& payload_path,
                                                 std::string service_id, std::string key_id,
                                                 std::span<const std::byte> hmac_key, std::uint64_t sequence,
                                                 std::string media_type = "application/octet-stream",
                                                 const ArtifactVerificationOptions& options = {});

Result<void> verify_artifact(const MemoryArtifact& artifact, std::span<const std::byte> payload,
                             const ArtifactAttestation& attestation, std::string_view expected_service_id,
                             std::string_view expected_key_id, std::span<const std::byte> hmac_key);

Result<void> verify_artifact_file(const MemoryArtifact& artifact, const std::filesystem::path& payload_path,
                                  const ArtifactAttestation& attestation,
                                  std::string_view expected_service_id, std::string_view expected_key_id,
                                  std::span<const std::byte> hmac_key,
                                  const ArtifactVerificationOptions& options = {});

Result<void> save_artifact_attestation(const ArtifactAttestation& attestation,
                                       const std::filesystem::path& path, const SaveOptions& options = {});
Result<ArtifactAttestation> load_artifact_attestation(const std::filesystem::path& path,
                                                      std::uintmax_t maximum_bytes = 65'536ULL);

std::string artifact_authentication_algorithm_name(ArtifactAuthenticationAlgorithm algorithm);

} // namespace rbfsafe

// Transport-neutral artifact exchange.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rbfsafe {

enum class ArtifactTransferOperation : std::uint8_t {
    Fetch = 0,
    Publish = 1,
};

enum class ArtifactTransferAuthentication : std::uint8_t {
    None = 0,
    HmacSha256 = 1,
    Ed25519 = 2,
};

struct ArtifactTransferAttestation {
    std::string id;
    std::string service_id;
    std::string key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::HmacSha256;
    ArtifactTransferOperation operation = ArtifactTransferOperation::Fetch;
    std::string request_id;
    std::string response_id;
    std::string artifact_id;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::uint64_t service_sequence = 0;
    std::string authentication_tag;
};

struct ArtifactFetchRequest {
    std::uint64_t sequence = 0;
    std::string id;
    std::string service_id;
    std::string memory_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string locator;
    std::string media_type;
    std::uint64_t maximum_payload_bytes = 0;
    ArtifactTransferAuthentication response_authentication = ArtifactTransferAuthentication::HmacSha256;
};

struct ArtifactFetchResponse {
    std::string id;
    std::string request_id;
    std::string service_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    std::uint64_t service_sequence = 0;
    std::optional<ArtifactTransferAttestation> service_attestation;
};

struct ArtifactPublishRequest {
    std::uint64_t sequence = 0;
    std::string id;
    std::string service_id;
    std::string memory_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string locator;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    ArtifactTransferAuthentication receipt_authentication = ArtifactTransferAuthentication::HmacSha256;
};

struct ArtifactPublishReceipt {
    std::string id;
    std::string request_id;
    std::string service_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    std::uint64_t service_sequence = 0;
    std::optional<ArtifactTransferAttestation> service_attestation;
};

struct VerifiedArtifactTransfer {
    std::string id;
    ArtifactTransferOperation operation = ArtifactTransferOperation::Fetch;
    std::string request_id;
    std::string response_id;
    std::string service_id;
    std::string memory_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    std::uint64_t service_sequence = 0;
    ArtifactTransferAuthentication authentication = ArtifactTransferAuthentication::None;
    std::string attestation_id;
    std::string verification_key_id;
    std::string trust_bundle_id;
};

struct RemoteArtifactOptions {
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
    bool require_active_artifact = true;
    CancellationToken cancellation;
};

bool valid_artifact_fetch_request(const ArtifactFetchRequest& request);
bool valid_artifact_fetch_response(const ArtifactFetchResponse& response);
bool valid_artifact_publish_request(const ArtifactPublishRequest& request);
bool valid_artifact_publish_receipt(const ArtifactPublishReceipt& receipt);
bool valid_artifact_transfer_attestation(const ArtifactTransferAttestation& attestation);
bool valid_verified_artifact_transfer(const VerifiedArtifactTransfer& transfer);

Result<ArtifactFetchRequest> prepare_artifact_fetch(
    const SafetyMemory& memory, const std::string& artifact_id, std::string service_id,
    std::uint64_t sequence, std::string media_type,
    ArtifactTransferAuthentication response_authentication = ArtifactTransferAuthentication::HmacSha256,
    const RemoteArtifactOptions& options = {});

Result<ArtifactFetchResponse> make_artifact_fetch_response(const ArtifactFetchRequest& request,
                                                           std::span<const std::byte> payload,
                                                           std::uint64_t service_sequence);

Result<ArtifactFetchResponse> authenticate_artifact_fetch_response(ArtifactFetchResponse response,
                                                                   std::string key_id,
                                                                   std::span<const std::byte> hmac_key);

Result<VerifiedArtifactTransfer>
verify_artifact_fetch(const SafetyMemory& memory, const ArtifactFetchRequest& request,
                      const ArtifactFetchResponse& response, std::span<const std::byte> payload,
                      std::string_view expected_key_id = {}, std::span<const std::byte> hmac_key = {},
                      const RemoteArtifactOptions& options = {});

Result<ArtifactPublishRequest> prepare_artifact_publish(
    const SafetyMemory& memory, const std::string& artifact_id, std::span<const std::byte> payload,
    std::string service_id, std::uint64_t sequence, std::string media_type,
    ArtifactTransferAuthentication receipt_authentication = ArtifactTransferAuthentication::HmacSha256,
    const RemoteArtifactOptions& options = {});

Result<ArtifactPublishReceipt> make_artifact_publish_receipt(const ArtifactPublishRequest& request,
                                                             std::uint64_t service_sequence);

Result<ArtifactPublishReceipt> authenticate_artifact_publish_receipt(ArtifactPublishReceipt receipt,
                                                                     std::string key_id,
                                                                     std::span<const std::byte> hmac_key);

Result<VerifiedArtifactTransfer>
verify_artifact_publish(const SafetyMemory& memory, const ArtifactPublishRequest& request,
                        const ArtifactPublishReceipt& receipt, std::span<const std::byte> payload,
                        std::string_view expected_key_id = {}, std::span<const std::byte> hmac_key = {},
                        const RemoteArtifactOptions& options = {});

struct ArtifactTransferRecord {
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    VerifiedArtifactTransfer transfer;
};

struct ArtifactTransferJournalLoadOptions {
    std::size_t maximum_records = 1'000'000;
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
};

class ArtifactTransferJournal;
Result<void> save_artifact_transfer_journal(const ArtifactTransferJournal& journal,
                                            const std::filesystem::path& directory,
                                            const SaveOptions& options);
Result<ArtifactTransferJournal>
load_artifact_transfer_journal(const std::filesystem::path& directory,
                               const ArtifactTransferJournalLoadOptions& options);

class ArtifactTransferJournal {
  public:
    const std::vector<ArtifactTransferRecord>& records() const noexcept { return records_; }
    const std::string& current_record_id() const noexcept { return current_record_id_; }
    std::string identity() const;
    bool valid() const;

    Result<ArtifactTransferRecord> append(VerifiedArtifactTransfer transfer,
                                          const std::string& expected_current_record_id,
                                          std::size_t maximum_records = 1'000'000);

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<ArtifactTransferJournal> load(const std::filesystem::path& directory,
                                                const ArtifactTransferJournalLoadOptions& options = {});

  private:
    friend Result<void> save_artifact_transfer_journal(const ArtifactTransferJournal&,
                                                       const std::filesystem::path&, const SaveOptions&);
    friend Result<ArtifactTransferJournal>
    load_artifact_transfer_journal(const std::filesystem::path&, const ArtifactTransferJournalLoadOptions&);

    std::vector<ArtifactTransferRecord> records_;
    std::string current_record_id_;
};

std::string artifact_transfer_operation_name(ArtifactTransferOperation operation);
std::string artifact_transfer_authentication_name(ArtifactTransferAuthentication authentication);

} // namespace rbfsafe

// Public identity and trust rotation.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

inline constexpr std::size_t kEd25519SeedBytes = 32;
inline constexpr std::size_t kEd25519PublicKeyBytes = 32;
inline constexpr std::size_t kEd25519SecretKeyBytes = 64;
inline constexpr std::size_t kEd25519SignatureBytes = 64;

struct Ed25519KeyPair {
    std::array<std::byte, kEd25519PublicKeyBytes> public_key{};
    std::array<std::byte, kEd25519SecretKeyBytes> secret_key{};
};

Result<Ed25519KeyPair> ed25519_key_pair_from_seed(std::span<const std::byte> seed);
Result<std::array<std::byte, kEd25519SignatureBytes>> ed25519_sign(std::span<const std::byte> message,
                                                                   std::span<const std::byte> secret_key);
Result<void> ed25519_verify(std::span<const std::byte> message, std::span<const std::byte> signature,
                            std::span<const std::byte> public_key);

enum class ServiceKeyState : std::uint8_t {
    Pending = 0,
    Active = 1,
    Retired = 2,
    Revoked = 3,
};

struct ServicePublicKey {
    std::string id;
    std::string service_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::array<std::byte, kEd25519PublicKeyBytes> public_key{};
    std::uint64_t valid_from_sequence = 1;
    std::uint64_t valid_through_sequence = 0;
    ServiceKeyState state = ServiceKeyState::Pending;
    bool allow_fetch = true;
    bool allow_publish = true;
    bool allow_rotate = false;
};

bool valid_service_public_key(const ServicePublicKey& key);

Result<ServicePublicKey>
make_service_public_key(std::string service_id, std::span<const std::byte> public_key,
                        std::uint64_t valid_from_sequence = 1, std::uint64_t valid_through_sequence = 0,
                        ServiceKeyState state = ServiceKeyState::Pending, bool allow_fetch = true,
                        bool allow_publish = true, bool allow_rotate = false);

struct ServiceTrustBundleLoadOptions {
    std::size_t maximum_keys = 100'000;
    std::uintmax_t maximum_payload_bytes = 4'194'304ULL;
};

struct ServiceTrustRotationPolicy {
    std::uint32_t minimum_signatures = 1;
    bool require_distinct_services = false;
};

bool valid_service_trust_rotation_policy(const ServiceTrustRotationPolicy& policy);

class ServiceTrustBundle;
Result<void> save_service_trust_bundle(const ServiceTrustBundle& bundle, const std::filesystem::path& path,
                                       const SaveOptions& options);
Result<ServiceTrustBundle> load_service_trust_bundle(const std::filesystem::path& path,
                                                     const ServiceTrustBundleLoadOptions& options);

class ServiceTrustBundle {
  public:
    static Result<ServiceTrustBundle> create(std::uint64_t sequence, std::string parent_id,
                                             std::vector<ServicePublicKey> keys);
    static Result<ServiceTrustBundle> create_with_rotation_policy(std::uint64_t sequence,
                                                                  std::string parent_id,
                                                                  std::vector<ServicePublicKey> keys,
                                                                  ServiceTrustRotationPolicy rotation_policy);

    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    std::uint64_t sequence() const noexcept { return sequence_; }
    const std::string& id() const noexcept { return id_; }
    const std::string& parent_id() const noexcept { return parent_id_; }
    const std::vector<ServicePublicKey>& keys() const noexcept { return keys_; }
    const ServiceTrustRotationPolicy& rotation_policy() const noexcept { return rotation_policy_; }

    bool valid() const;
    Result<std::optional<ServicePublicKey>> key(const std::string& service_id,
                                                const std::string& key_id) const;

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<ServiceTrustBundle> load(const std::filesystem::path& path,
                                           const ServiceTrustBundleLoadOptions& options = {});

  private:
    friend Result<void> save_service_trust_bundle(const ServiceTrustBundle&, const std::filesystem::path&,
                                                  const SaveOptions&);
    friend Result<ServiceTrustBundle> load_service_trust_bundle(const std::filesystem::path&,
                                                                const ServiceTrustBundleLoadOptions&);

    std::uint64_t sequence_ = 0;
    std::uint32_t storage_schema_ = 2;
    std::string id_;
    std::string parent_id_;
    std::vector<ServicePublicKey> keys_;
    ServiceTrustRotationPolicy rotation_policy_;
};

Result<ServiceTrustBundle> rotate_service_trust_bundle(const ServiceTrustBundle& previous,
                                                       std::vector<ServicePublicKey> keys);

struct ServiceTrustBundleAuthorization {
    std::string id;
    std::string predecessor_bundle_id;
    std::string successor_bundle_id;
    std::uint64_t predecessor_sequence = 0;
    std::uint64_t successor_sequence = 0;
    std::string signer_service_id;
    std::string signer_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_service_trust_bundle_authorization(const ServiceTrustBundleAuthorization& authorization);

Result<ServiceTrustBundleAuthorization> authorize_service_trust_bundle_successor(
    const ServiceTrustBundle& predecessor, const ServiceTrustBundle& successor, std::string signer_service_id,
    std::string signer_key_id, std::span<const std::byte> ed25519_secret_key);

Result<void> verify_service_trust_bundle_successor(const ServiceTrustBundle& predecessor,
                                                   const ServiceTrustBundle& successor,
                                                   const ServiceTrustBundleAuthorization& authorization);

struct ServiceTrustBundleAuthorizationSet {
    std::string id;
    std::string predecessor_bundle_id;
    std::string successor_bundle_id;
    std::uint64_t predecessor_sequence = 0;
    std::uint64_t successor_sequence = 0;
    std::vector<ServiceTrustBundleAuthorization> authorizations;
};

bool valid_service_trust_bundle_authorization_set(
    const ServiceTrustBundleAuthorizationSet& authorization_set);

Result<ServiceTrustBundleAuthorizationSet>
assemble_service_trust_bundle_authorizations(const ServiceTrustBundle& predecessor,
                                             const ServiceTrustBundle& successor,
                                             std::vector<ServiceTrustBundleAuthorization> authorizations);

Result<void>
verify_service_trust_bundle_successor(const ServiceTrustBundle& predecessor,
                                      const ServiceTrustBundle& successor,
                                      const ServiceTrustBundleAuthorizationSet& authorization_set);

enum class ServiceTrustRotationEventType : std::uint8_t {
    RootPinned = 0,
    SuccessorAuthorized = 1,
};

struct ServiceTrustRotationRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    ServiceTrustRotationEventType type = ServiceTrustRotationEventType::RootPinned;
    std::string bundle_id;
    std::optional<ServiceTrustBundleAuthorization> authorization;
    std::optional<ServiceTrustBundleAuthorizationSet> authorization_set;
};

struct ServiceTrustHistoryLoadOptions {
    std::size_t maximum_bundles = 100'000;
    std::size_t maximum_keys_per_bundle = 100'000;
    std::size_t maximum_total_keys = 1'000'000;
    std::size_t maximum_signatures_per_rotation = 100'000;
    std::uintmax_t maximum_metadata_bytes = 65'536ULL;
    std::uintmax_t maximum_bundle_bytes = 4'194'304ULL;
    CancellationToken cancellation;
};

struct ServiceTrustCheckpointSignature {
    std::string signer_service_id;
    std::string signer_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_service_trust_checkpoint_signature(const ServiceTrustCheckpointSignature& signature);

struct ServiceTrustCheckpointLoadOptions {
    std::size_t maximum_signatures = 100'000;
    std::uintmax_t maximum_payload_bytes = 4'194'304ULL;
};

struct ServiceTrustCheckpoint {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string root_bundle_id;
    std::string head_bundle_id;
    std::uint64_t head_sequence = 0;
    std::string head_record_id;
    std::vector<ServiceTrustCheckpointSignature> signatures;

    bool valid() const;
    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<ServiceTrustCheckpoint> load(const std::filesystem::path& path,
                                               const ServiceTrustCheckpointLoadOptions& options = {});
};

class ServiceTrustHistory {
  public:
    static Result<ServiceTrustHistory> create(const std::filesystem::path& directory,
                                              const ServiceTrustBundle& root_bundle,
                                              const std::string& expected_root_bundle_id);
    static Result<ServiceTrustHistory> open(const std::filesystem::path& directory,
                                            const std::string& expected_root_bundle_id,
                                            const std::string& expected_head_bundle_id,
                                            const ServiceTrustHistoryLoadOptions& options = {});
    static Result<ServiceTrustHistory> open(const std::filesystem::path& directory,
                                            const std::string& expected_root_bundle_id,
                                            const ServiceTrustCheckpoint& checkpoint,
                                            const std::string& expected_checkpoint_id,
                                            const ServiceTrustHistoryLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    const std::string& root_bundle_id() const noexcept { return root_bundle_id_; }
    const std::string& current_bundle_id() const noexcept { return current_bundle_id_; }
    const std::vector<ServiceTrustRotationRecord>& records() const noexcept { return records_; }
    bool valid() const;

    Result<ServiceTrustBundle> current_bundle() const;
    Result<ServiceTrustBundle> bundle(const std::string& bundle_id) const;
    Result<ServiceTrustRotationRecord> publish(const ServiceTrustBundle& successor,
                                               const ServiceTrustBundleAuthorization& authorization,
                                               const std::string& expected_head_bundle_id,
                                               std::size_t maximum_bundles = 100'000);
    Result<ServiceTrustRotationRecord> publish(const ServiceTrustBundle& successor,
                                               const ServiceTrustBundleAuthorizationSet& authorization_set,
                                               const std::string& expected_head_bundle_id,
                                               std::size_t maximum_bundles = 100'000);

  private:
    Result<ServiceTrustRotationRecord>
    publish_impl(const ServiceTrustBundle& successor,
                 std::optional<ServiceTrustBundleAuthorization> authorization,
                 std::optional<ServiceTrustBundleAuthorizationSet> authorization_set,
                 const std::string& expected_head_bundle_id, std::size_t maximum_bundles);

    std::filesystem::path directory_;
    std::uint32_t storage_schema_ = 1;
    std::string root_bundle_id_;
    std::string current_bundle_id_;
    std::vector<ServiceTrustBundle> bundles_;
    std::vector<ServiceTrustRotationRecord> records_;
    ServiceTrustHistoryLoadOptions options_;
};

Result<ServiceTrustCheckpointSignature>
sign_service_trust_checkpoint(const ServiceTrustHistory& history, std::string signer_service_id,
                              std::string signer_key_id, std::span<const std::byte> ed25519_secret_key);

Result<ServiceTrustCheckpoint>
assemble_service_trust_checkpoint(const ServiceTrustHistory& history,
                                  std::vector<ServiceTrustCheckpointSignature> signatures);

Result<void> verify_service_trust_checkpoint(const ServiceTrustHistory& history,
                                             const ServiceTrustCheckpoint& checkpoint,
                                             const std::string& expected_checkpoint_id);

Result<ServicePublicKey> trusted_service_public_key(const ServiceTrustBundle& bundle,
                                                    const std::string& service_id, const std::string& key_id,
                                                    ArtifactTransferOperation operation,
                                                    std::uint64_t service_sequence);

Result<ArtifactFetchResponse> sign_artifact_fetch_response(ArtifactFetchResponse response, std::string key_id,
                                                           std::span<const std::byte> ed25519_secret_key);

Result<ArtifactPublishReceipt> sign_artifact_publish_receipt(ArtifactPublishReceipt receipt,
                                                             std::string key_id,
                                                             std::span<const std::byte> ed25519_secret_key);

Result<VerifiedArtifactTransfer> verify_artifact_fetch_offline(const SafetyMemory& memory,
                                                               const ArtifactFetchRequest& request,
                                                               const ArtifactFetchResponse& response,
                                                               std::span<const std::byte> payload,
                                                               const ServiceTrustBundle& trust_bundle,
                                                               const RemoteArtifactOptions& options = {});

Result<VerifiedArtifactTransfer> verify_artifact_publish_offline(const SafetyMemory& memory,
                                                                 const ArtifactPublishRequest& request,
                                                                 const ArtifactPublishReceipt& receipt,
                                                                 std::span<const std::byte> payload,
                                                                 const ServiceTrustBundle& trust_bundle,
                                                                 const RemoteArtifactOptions& options = {});

std::string service_key_state_name(ServiceKeyState state);
std::string service_trust_rotation_event_type_name(ServiceTrustRotationEventType type);

} // namespace rbfsafe

// Authenticated multi-robot coordination.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rbfsafe {

struct OccupancyPublication {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string stream_id;
    std::uint64_t publisher_sequence = 0;
    std::string parent_publication_id;
    std::string publisher_service_id;
    std::string publisher_key_id;
    std::string trust_bundle_id;
    std::string occupancy_bundle_id;
    std::string timeline_id;
    std::string workspace_frame_id;
    std::uint64_t valid_from_tick = 0;
    std::uint64_t valid_through_tick = 0;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<OccupancyPublication> load(const std::filesystem::path& path,
                                             std::uintmax_t maximum_payload_bytes = 1'048'576ULL);
};

struct VerifiedOccupancyPublication {
    std::string id;
    std::string publication_id;
    std::string stream_id;
    std::uint64_t publisher_sequence = 0;
    std::string publisher_service_id;
    std::string publisher_key_id;
    std::string trust_bundle_id;
    std::string occupancy_bundle_id;
    std::string timeline_id;
    std::string workspace_frame_id;
    std::uint64_t valid_from_tick = 0;
    std::uint64_t valid_through_tick = 0;
    std::uint64_t evaluation_tick = 0;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<OccupancyPublication> sign_continuous_fleet_occupancy_publication(
    const std::filesystem::path& occupancy_payload_path, const ServiceTrustBundle& trust_bundle,
    std::string stream_id, std::string publisher_service_id, std::string publisher_key_id,
    std::span<const std::byte> ed25519_secret_key, std::uint64_t publisher_sequence,
    std::string parent_publication_id, std::uint64_t valid_from_tick, std::uint64_t valid_through_tick,
    const ContinuousFleetOccupancyBundleLoadOptions& options = {});

Result<VerifiedOccupancyPublication> verify_continuous_fleet_occupancy_publication(
    const std::filesystem::path& occupancy_payload_path, const OccupancyPublication& publication,
    const ServiceTrustBundle& trust_bundle, std::string_view expected_stream_id,
    std::string_view expected_publisher_service_id, std::string_view expected_trust_bundle_id,
    std::string_view expected_parent_publication_id, std::uint64_t evaluation_tick,
    const ContinuousFleetOccupancyBundleLoadOptions& options = {});

Result<void> verify_occupancy_publication_successor(const OccupancyPublication& previous,
                                                    const OccupancyPublication& successor);

Result<void> save_occupancy_publication(const OccupancyPublication& publication,
                                        const std::filesystem::path& path, const SaveOptions& options = {});
Result<OccupancyPublication> load_occupancy_publication(const std::filesystem::path& path,
                                                        std::uintmax_t maximum_payload_bytes = 1'048'576ULL);

enum class OccupancyPublicationHistoryRelation : std::uint8_t {
    Identical = 0,
    FirstExtendsSecond = 1,
    SecondExtendsFirst = 2,
    Forked = 3,
};

const char*
occupancy_publication_history_relation_name(OccupancyPublicationHistoryRelation relation) noexcept;

struct OccupancyPublicationHistoryRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_record_id;
    std::string publication_id;
    std::string authentication_tag;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct OccupancyPublicationHistoryAudit {
    std::uint32_t storage_schema = 1;
    std::string id;
    OccupancyPublicationHistoryRelation relation = OccupancyPublicationHistoryRelation::Identical;
    std::string stream_id;
    std::string publisher_service_id;
    std::string trust_bundle_id;
    std::string root_publication_id;
    std::string first_head_publication_id;
    std::string second_head_publication_id;
    std::uint64_t first_publication_count = 0;
    std::uint64_t second_publication_count = 0;
    std::uint64_t common_prefix_count = 0;
    std::string common_publication_id;

    bool valid() const;
    bool fork_detected() const noexcept { return relation == OccupancyPublicationHistoryRelation::Forked; }
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct OccupancyPublicationHistoryLoadOptions {
    std::size_t maximum_publications = 100'000;
    std::uintmax_t maximum_manifest_bytes = 65'536ULL;
    std::uintmax_t maximum_record_bytes = 65'536ULL;
    std::uintmax_t maximum_publication_bytes = 1'048'576ULL;
    std::uintmax_t maximum_trust_bundle_bytes = 4'194'304ULL;
    std::uintmax_t maximum_total_payload_bytes = 4'294'967'296ULL;
    std::size_t maximum_trust_keys = 100'000;
    ContinuousFleetOccupancyBundleLoadOptions occupancy;
};

class OccupancyPublicationHistory {
  public:
    static Result<OccupancyPublicationHistory>
    create(const std::filesystem::path& directory, const OccupancyPublication& root_publication,
           const std::filesystem::path& root_payload_path, const ServiceTrustBundle& trust_bundle,
           std::string_view expected_stream_id, std::string_view expected_publisher_service_id,
           std::string_view expected_trust_bundle_id, std::string_view expected_root_publication_id,
           const OccupancyPublicationHistoryLoadOptions& options = {});

    static Result<OccupancyPublicationHistory>
    open(const std::filesystem::path& directory, std::string_view expected_stream_id,
         std::string_view expected_publisher_service_id, std::string_view expected_trust_bundle_id,
         std::string_view expected_root_publication_id, std::string_view expected_head_publication_id,
         const OccupancyPublicationHistoryLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    const std::string& stream_id() const noexcept { return stream_id_; }
    const std::string& publisher_service_id() const noexcept { return publisher_service_id_; }
    const std::string& trust_bundle_id() const noexcept { return trust_bundle_id_; }
    const std::string& root_publication_id() const noexcept { return root_publication_id_; }
    const std::string& current_publication_id() const noexcept { return current_publication_id_; }
    const std::string& timeline_id() const noexcept { return timeline_id_; }
    const std::string& workspace_frame_id() const noexcept { return workspace_frame_id_; }
    const std::vector<OccupancyPublicationHistoryRecord>& records() const noexcept { return records_; }
    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<ServiceTrustBundle> trust_bundle() const;
    Result<OccupancyPublication> current_publication() const;
    Result<OccupancyPublication> publication(std::string_view publication_id) const;
    Result<VerifiedOccupancyPublication> verify(std::string_view publication_id,
                                                std::uint64_t evaluation_tick) const;
    Result<OccupancyPublicationHistoryRecord> publish(const OccupancyPublication& publication,
                                                      const std::filesystem::path& payload_path,
                                                      std::string_view expected_head_publication_id,
                                                      std::size_t maximum_publications = 100'000);

  private:
    friend Result<OccupancyPublicationHistoryAudit>
    audit_occupancy_publication_histories(const OccupancyPublicationHistory& first,
                                          const OccupancyPublicationHistory& second);

    std::filesystem::path directory_;
    std::uint32_t storage_schema_ = 1;
    std::string stream_id_;
    std::string publisher_service_id_;
    std::string trust_bundle_id_;
    std::string root_publication_id_;
    std::string current_publication_id_;
    std::string timeline_id_;
    std::string workspace_frame_id_;
    std::optional<ServiceTrustBundle> trust_bundle_;
    std::vector<OccupancyPublicationHistoryRecord> records_;
    std::vector<OccupancyPublication> publications_;
    std::vector<std::filesystem::path> payload_paths_;
    OccupancyPublicationHistoryLoadOptions options_;
};

Result<OccupancyPublicationHistoryAudit>
audit_occupancy_publication_histories(const OccupancyPublicationHistory& first,
                                      const OccupancyPublicationHistory& second);

struct RotatingOccupancyPublicationHistoryLoadOptions {
    std::size_t maximum_publications = 100'000;
    std::uintmax_t maximum_manifest_bytes = 65'536ULL;
    std::uintmax_t maximum_record_bytes = 65'536ULL;
    std::uintmax_t maximum_publication_bytes = 1'048'576ULL;
    std::uintmax_t maximum_total_payload_bytes = 4'294'967'296ULL;
    ServiceTrustHistoryLoadOptions trust;
    ContinuousFleetOccupancyBundleLoadOptions occupancy;
};

struct RotatingOccupancyPublicationHistoryAudit {
    std::uint32_t storage_schema = 1;
    std::string id;
    OccupancyPublicationHistoryRelation publication_relation = OccupancyPublicationHistoryRelation::Identical;
    OccupancyPublicationHistoryRelation trust_relation = OccupancyPublicationHistoryRelation::Identical;
    std::string stream_id;
    std::string publisher_service_id;
    std::string trust_root_bundle_id;
    std::string root_publication_id;
    std::string first_trust_head_bundle_id;
    std::string second_trust_head_bundle_id;
    std::string first_head_publication_id;
    std::string second_head_publication_id;
    std::uint64_t first_trust_bundle_count = 0;
    std::uint64_t second_trust_bundle_count = 0;
    std::uint64_t common_trust_prefix_count = 0;
    std::string common_trust_bundle_id;
    std::uint64_t first_publication_count = 0;
    std::uint64_t second_publication_count = 0;
    std::uint64_t common_publication_prefix_count = 0;
    std::string common_publication_id;

    bool valid() const;
    bool fork_detected() const noexcept {
        return publication_relation == OccupancyPublicationHistoryRelation::Forked ||
               trust_relation == OccupancyPublicationHistoryRelation::Forked;
    }
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

class RotatingOccupancyPublicationHistory {
  public:
    static Result<RotatingOccupancyPublicationHistory>
    create(const std::filesystem::path& directory, const OccupancyPublication& root_publication,
           const std::filesystem::path& root_payload_path, const ServiceTrustHistory& trust_history,
           std::string_view expected_stream_id, std::string_view expected_publisher_service_id,
           std::string_view expected_trust_root_bundle_id, std::string_view expected_trust_head_bundle_id,
           std::string_view expected_root_publication_id,
           const RotatingOccupancyPublicationHistoryLoadOptions& options = {});

    static Result<RotatingOccupancyPublicationHistory>
    open(const std::filesystem::path& directory, std::string_view expected_stream_id,
         std::string_view expected_publisher_service_id, std::string_view expected_trust_root_bundle_id,
         std::string_view expected_trust_head_bundle_id, std::string_view expected_root_publication_id,
         std::string_view expected_head_publication_id,
         const RotatingOccupancyPublicationHistoryLoadOptions& options = {});

    static Result<RotatingOccupancyPublicationHistory>
    open(const std::filesystem::path& directory, std::string_view expected_stream_id,
         std::string_view expected_publisher_service_id, std::string_view expected_trust_root_bundle_id,
         const ServiceTrustCheckpoint& checkpoint, std::string_view expected_checkpoint_id,
         std::string_view expected_root_publication_id, std::string_view expected_head_publication_id,
         const RotatingOccupancyPublicationHistoryLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    const std::string& stream_id() const noexcept { return stream_id_; }
    const std::string& publisher_service_id() const noexcept { return publisher_service_id_; }
    const std::string& trust_root_bundle_id() const noexcept { return trust_root_bundle_id_; }
    const std::string& current_trust_bundle_id() const noexcept { return current_trust_bundle_id_; }
    const std::string& root_publication_id() const noexcept { return root_publication_id_; }
    const std::string& current_publication_id() const noexcept { return current_publication_id_; }
    const std::string& timeline_id() const noexcept { return timeline_id_; }
    const std::string& workspace_frame_id() const noexcept { return workspace_frame_id_; }
    const std::vector<OccupancyPublicationHistoryRecord>& records() const noexcept { return records_; }

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<ServiceTrustHistory> trust_history() const;
    Result<ServiceTrustBundle> current_trust_bundle() const;
    Result<OccupancyPublication> current_publication() const;
    Result<OccupancyPublication> publication(std::string_view publication_id) const;
    Result<VerifiedOccupancyPublication> verify(std::string_view publication_id,
                                                std::uint64_t evaluation_tick) const;

    Result<ServiceTrustRotationRecord> rotate_trust(const ServiceTrustBundle& successor,
                                                    const ServiceTrustBundleAuthorization& authorization,
                                                    std::string_view expected_trust_head_bundle_id,
                                                    std::size_t maximum_trust_bundles = 100'000);
    Result<ServiceTrustRotationRecord>
    rotate_trust(const ServiceTrustBundle& successor,
                 const ServiceTrustBundleAuthorizationSet& authorization_set,
                 std::string_view expected_trust_head_bundle_id, std::size_t maximum_trust_bundles = 100'000);
    Result<OccupancyPublicationHistoryRecord> publish(const OccupancyPublication& publication,
                                                      const std::filesystem::path& payload_path,
                                                      std::string_view expected_head_publication_id,
                                                      std::string_view expected_trust_head_bundle_id,
                                                      std::size_t maximum_publications = 100'000);

  private:
    friend Result<RotatingOccupancyPublicationHistoryAudit>
    audit_rotating_occupancy_publication_histories(const RotatingOccupancyPublicationHistory& first,
                                                   const RotatingOccupancyPublicationHistory& second);

    Result<ServiceTrustRotationRecord>
    rotate_trust_impl(const ServiceTrustBundle& successor,
                      std::optional<ServiceTrustBundleAuthorization> authorization,
                      std::optional<ServiceTrustBundleAuthorizationSet> authorization_set,
                      std::string_view expected_trust_head_bundle_id, std::size_t maximum_trust_bundles);

    std::filesystem::path directory_;
    std::uint32_t storage_schema_ = 1;
    std::string stream_id_;
    std::string publisher_service_id_;
    std::string trust_root_bundle_id_;
    std::string current_trust_bundle_id_;
    std::string root_publication_id_;
    std::string current_publication_id_;
    std::string timeline_id_;
    std::string workspace_frame_id_;
    std::optional<ServiceTrustHistory> trust_history_;
    std::vector<OccupancyPublicationHistoryRecord> records_;
    std::vector<OccupancyPublication> publications_;
    std::vector<std::filesystem::path> payload_paths_;
    RotatingOccupancyPublicationHistoryLoadOptions options_;
};

Result<RotatingOccupancyPublicationHistoryAudit>
audit_rotating_occupancy_publication_histories(const RotatingOccupancyPublicationHistory& first,
                                               const RotatingOccupancyPublicationHistory& second);

struct CoordinatedReservationParticipant {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string deployment_id;
    std::string occupancy_id;
    std::string stream_id;
    std::string publisher_service_id;
    std::string publisher_key_id;
    std::uint64_t publisher_sequence = 0;
    std::string trust_root_bundle_id;
    std::string trust_head_bundle_id;
    std::string publication_trust_bundle_id;
    std::string publication_root_id;
    std::string publication_head_id;
    std::string verified_publication_id;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::uint64_t valid_from_tick = 0;
    std::uint64_t valid_through_tick = 0;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct CoordinatedReservationAgreementLoadOptions {
    std::size_t maximum_participants = 10'000;
    std::uintmax_t maximum_payload_bytes = 16'777'216ULL;
    CancellationToken cancellation;
};

struct CoordinatedReservationAgreement {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string protocol_id;
    std::uint64_t round = 0;
    std::string parent_agreement_id;
    std::uint64_t evaluation_tick = 0;
    std::uint64_t valid_from_tick = 0;
    std::uint64_t valid_through_tick = 0;
    std::string occupancy_bundle_id;
    std::string occupancy_report_id;
    std::string timeline_id;
    std::string workspace_frame_id;
    double minimum_separation = 0.0;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::vector<CoordinatedReservationParticipant> participants;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<CoordinatedReservationAgreement>
    load(const std::filesystem::path& path, const CoordinatedReservationAgreementLoadOptions& options = {});
};

Result<CoordinatedReservationAgreement>
make_coordinated_reservation_agreement(std::string protocol_id, std::uint64_t round,
                                       std::string parent_agreement_id, std::uint64_t evaluation_tick,
                                       const ContinuousFleetOccupancyBundle& occupancy_bundle,
                                       std::span<const std::string> deployment_ids,
                                       std::span<const RotatingOccupancyPublicationHistory> histories,
                                       const CoordinatedReservationAgreementLoadOptions& options = {});

Result<void>
verify_coordinated_reservation_agreement(const CoordinatedReservationAgreement& agreement,
                                         const ContinuousFleetOccupancyBundle& occupancy_bundle,
                                         std::span<const std::string> deployment_ids,
                                         std::span<const RotatingOccupancyPublicationHistory> histories,
                                         const CoordinatedReservationAgreementLoadOptions& options = {});

Result<void>
verify_coordinated_reservation_successor(const CoordinatedReservationAgreement& previous,
                                         const CoordinatedReservationAgreement& successor,
                                         std::span<const std::string> deployment_ids,
                                         std::span<const RotatingOccupancyPublicationHistory> histories,
                                         const CoordinatedReservationAgreementLoadOptions& options = {});

} // namespace rbfsafe

// Reviewed deployment profiles.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class DeploymentReviewRole : std::uint8_t {
    Safety = 0,
    Controls = 1,
    Operations = 2,
    Security = 3,
};

struct DeploymentReviewPolicy {
    std::uint32_t minimum_approvals = 1;
    bool require_distinct_services = false;
    std::vector<DeploymentReviewRole> required_roles;
};

struct DeploymentRuntimeConstraints {
    std::uint64_t maximum_observation_age_ns = 100'000'000;
    std::uint64_t maximum_command_latency_ns = 100'000'000;
    std::uint64_t maximum_control_period_ns = 100'000'000;
    std::uint32_t maximum_consecutive_missed_cycles = 0;
    bool require_runtime_monitor = true;
    bool require_fail_closed_transport = true;
    bool require_authenticated_artifacts = true;
};

bool valid_deployment_review_policy(const DeploymentReviewPolicy& policy);
bool valid_deployment_runtime_constraints(const DeploymentRuntimeConstraints& constraints);

struct DeploymentProfileInput {
    std::string deployment_id;
    std::string robot_digest;
    std::string controller_digest;
    std::string platform_digest;
    std::string runtime_digest;
    std::string trust_root_bundle_id;
    std::string trust_checkpoint_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    DeploymentRuntimeConstraints runtime_constraints;
    DeploymentReviewPolicy review_policy;
};

struct DeploymentProfile {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string deployment_id;
    std::string robot_digest;
    std::string controller_digest;
    std::string platform_digest;
    std::string runtime_digest;
    std::string trust_root_bundle_id;
    std::string trust_checkpoint_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    DeploymentRuntimeConstraints runtime_constraints;
    DeploymentReviewPolicy review_policy;

    static Result<DeploymentProfile> create(DeploymentProfileInput input);
    bool valid() const;
};

struct DeploymentProfileApproval {
    std::string id;
    std::string profile_id;
    std::string signer_service_id;
    std::string signer_key_id;
    DeploymentReviewRole role = DeploymentReviewRole::Safety;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_deployment_profile_approval(const DeploymentProfileApproval& approval);

struct DeploymentProfileApprovalSet {
    std::string id;
    std::string profile_id;
    std::vector<DeploymentProfileApproval> approvals;
};

bool valid_deployment_profile_approval_set(const DeploymentProfileApprovalSet& approval_set);

Result<DeploymentProfileApproval>
sign_deployment_profile_approval(const DeploymentProfile& profile, std::string signer_service_id,
                                 std::string signer_key_id, DeploymentReviewRole role,
                                 std::span<const std::byte> ed25519_secret_key);

Result<DeploymentProfileApprovalSet>
assemble_deployment_profile_approvals(const DeploymentProfile& profile,
                                      std::vector<DeploymentProfileApproval> approvals);

Result<void> verify_deployment_profile_approvals(const DeploymentProfile& profile,
                                                 const DeploymentProfileApprovalSet& approval_set,
                                                 const ServiceTrustBundle& trust_bundle);

struct DeploymentRuntimeSnapshot {
    std::string deployment_id;
    std::string robot_digest;
    std::string controller_digest;
    std::string platform_digest;
    std::string runtime_digest;
    std::uint64_t observation_age_ns = 0;
    std::uint64_t command_latency_ns = 0;
    std::uint64_t control_period_ns = 0;
    std::uint32_t consecutive_missed_cycles = 0;
    bool runtime_monitor_active = false;
    bool fail_closed_transport_active = false;
    bool authenticated_artifacts = false;
};

bool valid_deployment_runtime_snapshot(const DeploymentRuntimeSnapshot& snapshot);

enum class DeploymentConstraintViolation : std::uint8_t {
    DeploymentIdentityMismatch = 0,
    RobotIdentityMismatch = 1,
    ControllerIdentityMismatch = 2,
    PlatformIdentityMismatch = 3,
    RuntimeIdentityMismatch = 4,
    ObservationAgeExceeded = 5,
    CommandLatencyExceeded = 6,
    ControlPeriodExceeded = 7,
    MissedCycleLimitExceeded = 8,
    RuntimeMonitorRequired = 9,
    FailClosedTransportRequired = 10,
    AuthenticatedArtifactsRequired = 11,
};

enum class DeploymentProfileAssessmentStatus : std::uint8_t {
    Conformant = 0,
    Nonconformant = 1,
};

struct DeploymentProfileAssessment {
    std::string id;
    std::string profile_id;
    std::string approval_set_id;
    std::string runtime_snapshot_id;
    DeploymentProfileAssessmentStatus status = DeploymentProfileAssessmentStatus::Nonconformant;
    std::vector<DeploymentConstraintViolation> violations;
    EvidenceLevel evidence = EvidenceLevel::Unknown;

    bool valid() const;
    bool authorizes_execution() const noexcept { return false; }
};

struct ReviewedDeploymentProfileLoadOptions {
    std::size_t maximum_approvals = 100'000;
    std::size_t maximum_required_roles = 32;
    std::uintmax_t maximum_payload_bytes = 4'194'304ULL;
};

class ReviewedDeploymentProfile;
Result<void> save_reviewed_deployment_profile(const ReviewedDeploymentProfile& reviewed,
                                              const std::filesystem::path& path, const SaveOptions& options);
Result<ReviewedDeploymentProfile>
load_reviewed_deployment_profile(const std::filesystem::path& path, const ServiceTrustHistory& trust_history,
                                 const ServiceTrustCheckpoint& trust_checkpoint,
                                 const std::string& expected_checkpoint_id,
                                 const ReviewedDeploymentProfileLoadOptions& options);

class ReviewedDeploymentProfile {
  public:
    static Result<ReviewedDeploymentProfile> create(DeploymentProfile profile,
                                                    DeploymentProfileApprovalSet approval_set,
                                                    const ServiceTrustHistory& trust_history,
                                                    const ServiceTrustCheckpoint& trust_checkpoint,
                                                    const std::string& expected_checkpoint_id);

    const DeploymentProfile& profile() const noexcept { return profile_; }
    const DeploymentProfileApprovalSet& approval_set() const noexcept { return approval_set_; }
    bool valid() const;
    bool authorizes_execution() const noexcept { return false; }

    Result<DeploymentProfileAssessment> assess(const DeploymentRuntimeSnapshot& snapshot) const;

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<ReviewedDeploymentProfile> load(const std::filesystem::path& path,
                                                  const ServiceTrustHistory& trust_history,
                                                  const ServiceTrustCheckpoint& trust_checkpoint,
                                                  const std::string& expected_checkpoint_id,
                                                  const ReviewedDeploymentProfileLoadOptions& options = {});

  private:
    friend Result<void> save_reviewed_deployment_profile(const ReviewedDeploymentProfile&,
                                                         const std::filesystem::path&, const SaveOptions&);
    friend Result<ReviewedDeploymentProfile>
    load_reviewed_deployment_profile(const std::filesystem::path&, const ServiceTrustHistory&,
                                     const ServiceTrustCheckpoint&, const std::string&,
                                     const ReviewedDeploymentProfileLoadOptions&);

    DeploymentProfile profile_;
    DeploymentProfileApprovalSet approval_set_;
};

std::string deployment_review_role_name(DeploymentReviewRole role);
std::string deployment_constraint_violation_name(DeploymentConstraintViolation violation);
std::string deployment_profile_assessment_status_name(DeploymentProfileAssessmentStatus status);

} // namespace rbfsafe

// Bounded execution sessions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class ExecutionEndpointRole : std::uint8_t {
    Controller = 0,
    RuntimeMonitor = 1,
};

struct ExecutionEndpointKey {
    std::string id;
    std::string service_id;
    ExecutionEndpointRole role = ExecutionEndpointRole::Controller;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::array<std::byte, kEd25519PublicKeyBytes> public_key{};
};

bool valid_execution_endpoint_key(const ExecutionEndpointKey& key);
Result<ExecutionEndpointKey> make_execution_endpoint_key(std::string service_id, ExecutionEndpointRole role,
                                                         std::span<const std::byte> ed25519_public_key);

struct ExecutionCommand {
    std::uint64_t index = 0;
    std::uint64_t scheduled_offset_ns = 0;
    Configuration configuration;
};

struct ExecutionCommandSequence {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string atlas_id;
    std::string robot_digest;
    std::string scene_digest;
    std::string connectivity_certificate_id;
    std::size_t dimension = 0;
    std::vector<ExecutionCommand> commands;
    std::vector<RegionId> region_sequence;

    static Result<ExecutionCommandSequence> create(const SafeAtlas& atlas,
                                                   std::vector<Configuration> configurations,
                                                   std::vector<std::uint64_t> scheduled_offsets_ns,
                                                   const TrajectoryAuditOptions& options = {});

    bool valid() const;
    Result<void> verify_compatible(const SafeAtlas& atlas, const TrajectoryAuditOptions& options = {}) const;
};

struct ExecutionSessionLimits {
    std::uint64_t maximum_start_delay_ns = 10'000'000;
    std::uint64_t maximum_duration_ns = 100'000'000;
    std::size_t maximum_commands = 100'000;
};

bool valid_execution_session_limits(const ExecutionSessionLimits& limits);

struct ExecutionSessionRequestInput {
    std::string session_nonce;
    ExecutionEndpointKey controller;
    ExecutionEndpointKey runtime_monitor;
    ExecutionSessionLimits limits;
};

struct ExecutionSessionRequest {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string session_nonce;
    std::string reviewed_profile_id;
    std::string reviewed_profile_approval_set_id;
    std::string trust_root_bundle_id;
    std::string trust_checkpoint_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string command_sequence_id;
    std::string atlas_id;
    std::string robot_digest;
    std::string scene_digest;
    std::size_t command_count = 0;
    ExecutionEndpointKey controller;
    ExecutionEndpointKey runtime_monitor;
    ExecutionSessionLimits limits;

    static Result<ExecutionSessionRequest> create(const ReviewedDeploymentProfile& reviewed,
                                                  const ExecutionCommandSequence& command_sequence,
                                                  ExecutionSessionRequestInput input);

    bool valid() const;
};

struct ExecutionSessionApproval {
    std::string id;
    std::string request_id;
    std::string signer_service_id;
    std::string signer_key_id;
    DeploymentReviewRole role = DeploymentReviewRole::Safety;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_execution_session_approval(const ExecutionSessionApproval& approval);

struct ExecutionSessionApprovalSet {
    std::string id;
    std::string request_id;
    std::vector<ExecutionSessionApproval> approvals;
};

bool valid_execution_session_approval_set(const ExecutionSessionApprovalSet& approval_set);

Result<ExecutionSessionApproval>
sign_execution_session_approval(const ExecutionSessionRequest& request,
                                const DeploymentProfileApproval& profile_approval,
                                std::span<const std::byte> ed25519_secret_key);

Result<ExecutionSessionApprovalSet>
assemble_execution_session_approvals(const ExecutionSessionRequest& request,
                                     const ReviewedDeploymentProfile& reviewed,
                                     std::vector<ExecutionSessionApproval> approvals);

Result<void> verify_execution_session_approvals(const ExecutionSessionRequest& request,
                                                const ReviewedDeploymentProfile& reviewed,
                                                const ExecutionSessionApprovalSet& approval_set,
                                                const ServiceTrustBundle& trust_bundle);

struct ExecutionControllerAcknowledgement {
    std::string id;
    std::string request_id;
    std::string command_sequence_id;
    std::string controller_service_id;
    std::string controller_key_id;
    std::size_t accepted_command_count = 0;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_execution_controller_acknowledgement(const ExecutionControllerAcknowledgement& acknowledgement);

Result<ExecutionControllerAcknowledgement>
sign_execution_controller_acknowledgement(const ExecutionSessionRequest& request,
                                          std::span<const std::byte> ed25519_secret_key);

Result<void>
verify_execution_controller_acknowledgement(const ExecutionSessionRequest& request,
                                            const ExecutionControllerAcknowledgement& acknowledgement);

enum class ExecutionMonitorState : std::uint8_t {
    ArmedCertifiedSequence = 0,
    Disarmed = 1,
    Fault = 2,
};

struct ExecutionRuntimeObservationInput {
    DeploymentRuntimeSnapshot runtime;
    std::uint64_t observation_sequence = 1;
    std::uint64_t observed_monotonic_ns = 0;
    ExecutionMonitorState monitor_state = ExecutionMonitorState::Disarmed;
};

struct ExecutionRuntimeObservation {
    std::string id;
    std::string request_id;
    std::string command_sequence_id;
    DeploymentRuntimeSnapshot runtime;
    std::uint64_t observation_sequence = 0;
    std::uint64_t observed_monotonic_ns = 0;
    ExecutionMonitorState monitor_state = ExecutionMonitorState::Disarmed;

    static Result<ExecutionRuntimeObservation> create(const ExecutionSessionRequest& request,
                                                      ExecutionRuntimeObservationInput input);

    bool valid() const;
};

struct ExecutionMonitorAcknowledgement {
    std::string id;
    ExecutionRuntimeObservation observation;
    std::string monitor_service_id;
    std::string monitor_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_execution_monitor_acknowledgement(const ExecutionMonitorAcknowledgement& acknowledgement);

Result<ExecutionMonitorAcknowledgement>
sign_execution_monitor_acknowledgement(const ExecutionSessionRequest& request,
                                       ExecutionRuntimeObservation observation,
                                       std::span<const std::byte> ed25519_secret_key);

Result<void> verify_execution_monitor_acknowledgement(const ExecutionSessionRequest& request,
                                                      const ExecutionMonitorAcknowledgement& acknowledgement);

struct ExecutionCommandAuthorization {
    std::string id;
    std::string session_id;
    std::string command_sequence_id;
    std::uint64_t command_index = 0;
    std::string command_digest;
    std::uint64_t dispatch_monotonic_ns = 0;
    std::uint64_t valid_from_monotonic_ns = 0;
    std::uint64_t valid_through_monotonic_ns = 0;
    EvidenceLevel evidence = EvidenceLevel::RuntimeExecutable;

    bool valid() const;
    bool open_ended() const noexcept { return false; }
};

struct BoundedExecutionSessionLoadOptions {
    std::size_t maximum_commands = 100'000;
    std::size_t maximum_dimension = 1'000;
    std::size_t maximum_region_sequence = 1'000'000;
    std::size_t maximum_approvals = 100'000;
    std::uintmax_t maximum_payload_bytes = 67'108'864ULL;
};

class BoundedExecutionSession;
Result<void> save_bounded_execution_session(const BoundedExecutionSession& session,
                                            const std::filesystem::path& path, const SaveOptions& options);
Result<BoundedExecutionSession>
load_bounded_execution_session(const std::filesystem::path& path, const ReviewedDeploymentProfile& reviewed,
                               const ServiceTrustHistory& trust_history,
                               const ServiceTrustCheckpoint& trust_checkpoint,
                               const std::string& expected_checkpoint_id, const SafeAtlas& atlas,
                               const BoundedExecutionSessionLoadOptions& options);

class BoundedExecutionSession {
  public:
    static Result<BoundedExecutionSession>
    create(ExecutionSessionRequest request, ExecutionCommandSequence command_sequence,
           ExecutionSessionApprovalSet approval_set,
           ExecutionControllerAcknowledgement controller_acknowledgement,
           ExecutionMonitorAcknowledgement monitor_acknowledgement, const ReviewedDeploymentProfile& reviewed,
           const ServiceTrustBundle& trust_bundle, const SafeAtlas& atlas);

    const std::string& id() const noexcept { return id_; }
    const ExecutionSessionRequest& request() const noexcept { return request_; }
    const ExecutionCommandSequence& command_sequence() const noexcept { return command_sequence_; }
    const ExecutionSessionApprovalSet& approval_set() const noexcept { return approval_set_; }
    const ExecutionControllerAcknowledgement& controller_acknowledgement() const noexcept {
        return controller_acknowledgement_;
    }
    const ExecutionMonitorAcknowledgement& monitor_acknowledgement() const noexcept {
        return monitor_acknowledgement_;
    }
    std::uint64_t valid_from_monotonic_ns() const noexcept { return valid_from_monotonic_ns_; }
    std::uint64_t start_deadline_monotonic_ns() const noexcept { return start_deadline_monotonic_ns_; }
    std::uint64_t valid_through_monotonic_ns() const noexcept { return valid_through_monotonic_ns_; }

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<std::optional<ExecutionCommandAuthorization>>
    authorize_command(std::uint64_t command_index, std::span<const double> configuration,
                      std::uint64_t dispatch_monotonic_ns) const;

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<BoundedExecutionSession>
    load(const std::filesystem::path& path, const ReviewedDeploymentProfile& reviewed,
         const ServiceTrustHistory& trust_history, const ServiceTrustCheckpoint& trust_checkpoint,
         const std::string& expected_checkpoint_id, const SafeAtlas& atlas,
         const BoundedExecutionSessionLoadOptions& options = {});

  private:
    friend Result<void> save_bounded_execution_session(const BoundedExecutionSession&,
                                                       const std::filesystem::path&, const SaveOptions&);
    friend Result<BoundedExecutionSession>
    load_bounded_execution_session(const std::filesystem::path&, const ReviewedDeploymentProfile&,
                                   const ServiceTrustHistory&, const ServiceTrustCheckpoint&,
                                   const std::string&, const SafeAtlas&,
                                   const BoundedExecutionSessionLoadOptions&);

    std::string id_;
    ExecutionSessionRequest request_;
    ExecutionCommandSequence command_sequence_;
    ExecutionSessionApprovalSet approval_set_;
    ExecutionControllerAcknowledgement controller_acknowledgement_;
    ExecutionMonitorAcknowledgement monitor_acknowledgement_;
    std::uint64_t valid_from_monotonic_ns_ = 0;
    std::uint64_t start_deadline_monotonic_ns_ = 0;
    std::uint64_t valid_through_monotonic_ns_ = 0;
};

std::string execution_endpoint_role_name(ExecutionEndpointRole role);
std::string execution_monitor_state_name(ExecutionMonitorState state);

} // namespace rbfsafe

// Execution authorization ledger.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class ExecutionCompletionOutcome : std::uint8_t {
    Completed = 0,
    Failed = 1,
    Rejected = 2,
};

struct ExecutionControllerCompletionInput {
    ExecutionCompletionOutcome outcome = ExecutionCompletionOutcome::Failed;
    std::uint64_t completed_monotonic_ns = 0;
    std::string result_digest;
};

struct ExecutionControllerCompletion {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string session_id;
    std::string authorization_id;
    std::string command_sequence_id;
    std::uint64_t command_index = 0;
    std::string command_digest;
    std::string controller_service_id;
    std::string controller_key_id;
    ExecutionCompletionOutcome outcome = ExecutionCompletionOutcome::Failed;
    std::uint64_t completed_monotonic_ns = 0;
    std::string result_digest;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
};

Result<ExecutionControllerCompletion> sign_execution_controller_completion(
    const BoundedExecutionSession& session, const ExecutionCommandAuthorization& authorization,
    ExecutionControllerCompletionInput input, std::span<const std::byte> ed25519_secret_key);

Result<void> verify_execution_controller_completion(const BoundedExecutionSession& session,
                                                    const ExecutionCommandAuthorization& authorization,
                                                    const ExecutionControllerCompletion& completion);

enum class ExecutionDependencyKind : std::uint8_t {
    ReviewedProfile = 0,
    Atlas = 1,
    Scene = 2,
    ControllerKey = 3,
    RuntimeMonitorKey = 4,
    ReviewerKey = 5,
    TrustCheckpoint = 6,
};

struct ExecutionDependencyRevocation {
    ExecutionDependencyKind kind = ExecutionDependencyKind::ReviewedProfile;
    std::string subject_id;
    std::string detail;
};

bool valid_execution_dependency_revocation(const ExecutionDependencyRevocation& revocation);

enum class ExecutionLedgerRecordType : std::uint8_t {
    SessionOpened = 0,
    CommandAuthorized = 1,
    ControllerCompletion = 2,
    SessionCancelled = 3,
    SessionExpired = 4,
    DependencyRevoked = 5,
};

enum class ExecutionLedgerStatus : std::uint8_t {
    Open = 0,
    AwaitingCompletion = 1,
    Completed = 2,
    Cancelled = 3,
    Expired = 4,
    Revoked = 5,
    Failed = 6,
};

struct ExecutionLedgerRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string ledger_id;
    std::string session_id;
    ExecutionLedgerRecordType type = ExecutionLedgerRecordType::SessionOpened;
    std::uint64_t observed_monotonic_ns = 0;
    std::optional<ExecutionCommandAuthorization> authorization;
    std::optional<ExecutionControllerCompletion> completion;
    std::optional<ServiceTrustCheckpoint> trust_checkpoint;
    std::optional<ExecutionDependencyRevocation> revocation;
    std::string detail;

    bool valid() const;
};

struct ExecutionLedgerSummary {
    std::string id;
    std::string ledger_id;
    std::string session_id;
    std::string current_record_id;
    ExecutionLedgerStatus status = ExecutionLedgerStatus::Open;
    std::size_t record_count = 0;
    std::size_t authorization_count = 0;
    std::size_t completion_count = 0;
    std::size_t next_command_index = 0;
    std::optional<std::uint64_t> outstanding_command_index;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct ExecutionLedgerCommandDecision {
    std::string id;
    std::string ledger_id;
    std::string current_record_id;
    ExecutionLedgerStatus status = ExecutionLedgerStatus::Open;
    std::optional<ExecutionCommandAuthorization> authorization;

    bool valid() const;
    EvidenceLevel evidence() const noexcept {
        return authorization ? EvidenceLevel::RuntimeExecutable : EvidenceLevel::Unknown;
    }
    bool authorizes_execution() const noexcept { return authorization.has_value(); }
    bool open_ended() const noexcept { return false; }
};

struct ExecutionLedgerAuditReport {
    std::string id;
    std::string ledger_id;
    std::string session_id;
    std::string current_record_id;
    ExecutionLedgerStatus status = ExecutionLedgerStatus::Open;
    std::size_t verified_records = 0;
    std::size_t verified_checkpoints = 0;
    std::size_t authorization_count = 0;
    std::size_t completion_count = 0;
    std::string latest_checkpoint_id;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct ExecutionLedgerLoadOptions {
    std::size_t maximum_records = 1'000'000;
    std::size_t maximum_signatures_per_checkpoint = 100'000;
    std::size_t maximum_total_checkpoint_signatures = 1'000'000;
    std::uintmax_t maximum_manifest_bytes = 65'536ULL;
    std::uintmax_t maximum_record_bytes = 8'388'608ULL;
    CancellationToken cancellation;
};

class ExecutionLedger {
  public:
    static Result<ExecutionLedger> create(const std::filesystem::path& directory,
                                          const BoundedExecutionSession& session);
    static Result<ExecutionLedger> open(const std::filesystem::path& directory,
                                        const BoundedExecutionSession& session,
                                        const ReviewedDeploymentProfile& reviewed,
                                        const ServiceTrustHistory& trust_history, const SafeAtlas& atlas,
                                        const ExecutionLedgerLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const std::string& id() const noexcept { return id_; }
    const std::string& session_id() const noexcept { return session_id_; }
    const std::string& current_record_id() const noexcept { return current_record_id_; }
    const std::vector<ExecutionLedgerRecord>& records() const noexcept { return records_; }
    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    ExecutionLedgerSummary summary() const;
    Result<ExecutionLedgerAuditReport> audit(const BoundedExecutionSession& session,
                                             const ReviewedDeploymentProfile& reviewed,
                                             const ServiceTrustHistory& trust_history,
                                             const SafeAtlas& atlas) const;

    Result<ExecutionLedgerCommandDecision>
    authorize_command(const BoundedExecutionSession& session, const ReviewedDeploymentProfile& reviewed,
                      const ServiceTrustHistory& current_trust_history,
                      const ServiceTrustCheckpoint& current_trust_checkpoint,
                      const std::string& expected_current_checkpoint_id, const SafeAtlas& atlas,
                      std::uint64_t command_index, std::span<const double> configuration,
                      std::uint64_t dispatch_monotonic_ns, const std::string& expected_current_record_id);

    Result<ExecutionLedgerRecord> record_completion(const BoundedExecutionSession& session,
                                                    const ReviewedDeploymentProfile& reviewed,
                                                    const ServiceTrustHistory& trust_history,
                                                    const SafeAtlas& atlas,
                                                    const ExecutionControllerCompletion& completion,
                                                    const std::string& expected_current_record_id);

    Result<ExecutionLedgerRecord> cancel(const BoundedExecutionSession& session,
                                         const ReviewedDeploymentProfile& reviewed,
                                         const ServiceTrustHistory& trust_history, const SafeAtlas& atlas,
                                         std::uint64_t observed_monotonic_ns, std::string detail,
                                         const std::string& expected_current_record_id);

    Result<ExecutionLedgerRecord> expire(const BoundedExecutionSession& session,
                                         const ReviewedDeploymentProfile& reviewed,
                                         const ServiceTrustHistory& trust_history, const SafeAtlas& atlas,
                                         std::uint64_t observed_monotonic_ns,
                                         const std::string& expected_current_record_id);

    Result<ExecutionLedgerRecord> revoke_dependency(const BoundedExecutionSession& session,
                                                    const ReviewedDeploymentProfile& reviewed,
                                                    const ServiceTrustHistory& trust_history,
                                                    const SafeAtlas& atlas, ExecutionDependencyKind kind,
                                                    std::string subject_id,
                                                    std::uint64_t observed_monotonic_ns, std::string detail,
                                                    const std::string& expected_current_record_id);

  private:
    Result<ExecutionLedgerRecord> append_record_unlocked(ExecutionLedger fresh, ExecutionLedgerRecord record);

    std::filesystem::path directory_;
    std::string id_;
    std::string session_id_;
    std::string current_record_id_;
    std::size_t command_count_ = 0;
    std::uint64_t valid_from_monotonic_ns_ = 0;
    std::uint64_t valid_through_monotonic_ns_ = 0;
    std::vector<ExecutionLedgerRecord> records_;
    ExecutionLedgerLoadOptions options_;
};

std::string execution_completion_outcome_name(ExecutionCompletionOutcome outcome);
std::string execution_dependency_kind_name(ExecutionDependencyKind kind);
std::string execution_ledger_record_type_name(ExecutionLedgerRecordType type);
std::string execution_ledger_status_name(ExecutionLedgerStatus status);

} // namespace rbfsafe

// Transparency logs and proofs.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

struct DeploymentTransparencyAnchor {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string deployment_id;
    std::string reviewed_profile_id;
    std::string approval_set_id;
    std::string robot_digest;
    std::string controller_digest;
    std::string platform_digest;
    std::string runtime_digest;
    std::string trust_root_bundle_id;
    std::string trust_checkpoint_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string trust_head_record_id;

    static Result<DeploymentTransparencyAnchor> create(const ReviewedDeploymentProfile& reviewed,
                                                       const ServiceTrustHistory& trust_history,
                                                       const ServiceTrustCheckpoint& trust_checkpoint,
                                                       const std::string& expected_checkpoint_id);

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct IndependentRuntimeObservationInput {
    DeploymentRuntimeSnapshot runtime;
    std::uint64_t observation_sequence = 1;
    std::uint64_t observed_monotonic_ns = 0;
    ExecutionMonitorState monitor_state = ExecutionMonitorState::Disarmed;
    std::string configuration_digest;
};

struct IndependentRuntimeObservation {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string session_id;
    std::string ledger_id;
    std::string ledger_record_id;
    std::string authorization_id;
    std::string command_sequence_id;
    std::uint64_t command_index = 0;
    std::string command_digest;
    DeploymentRuntimeSnapshot runtime;
    std::uint64_t observation_sequence = 0;
    std::uint64_t observed_monotonic_ns = 0;
    ExecutionMonitorState monitor_state = ExecutionMonitorState::Disarmed;
    std::string configuration_digest;

    static Result<IndependentRuntimeObservation> create(const BoundedExecutionSession& session,
                                                        const ExecutionLedger& ledger,
                                                        const ExecutionCommandAuthorization& authorization,
                                                        IndependentRuntimeObservationInput input);

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct RuntimeObservationAttestation {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string observation_id;
    std::string source_service_id;
    std::string source_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
};

struct RuntimeObservationPolicy {
    std::uint32_t minimum_attestations = 1;
    bool require_distinct_services = true;
    bool exclude_controller_service = true;
};

bool valid_runtime_observation_policy(const RuntimeObservationPolicy& policy);

struct RuntimeObservationAttestationSet {
    std::uint32_t storage_schema = 1;
    std::string id;
    IndependentRuntimeObservation observation;
    RuntimeObservationPolicy policy;
    std::vector<RuntimeObservationAttestation> attestations;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<RuntimeObservationAttestation>
sign_runtime_observation(const IndependentRuntimeObservation& observation, std::string source_service_id,
                         std::string source_key_id, std::span<const std::byte> ed25519_secret_key);

Result<void> verify_runtime_observation_attestation(const IndependentRuntimeObservation& observation,
                                                    const RuntimeObservationAttestation& attestation,
                                                    const ServiceTrustBundle& trust_bundle);

Result<RuntimeObservationAttestationSet> assemble_runtime_observation_attestations(
    const BoundedExecutionSession& session, IndependentRuntimeObservation observation,
    RuntimeObservationPolicy policy, std::vector<RuntimeObservationAttestation> attestations,
    const ServiceTrustBundle& trust_bundle);

Result<void> verify_runtime_observation_attestations(const BoundedExecutionSession& session,
                                                     const RuntimeObservationAttestationSet& attestation_set,
                                                     const ServiceTrustBundle& trust_bundle);

struct TransparencyLogIdentity {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_namespace;
    std::string signer_service_id;
    std::string signer_key_id;
    std::array<std::byte, kEd25519PublicKeyBytes> signer_public_key{};

    static Result<TransparencyLogIdentity> create(std::string log_namespace, std::string signer_service_id,
                                                  std::string signer_key_id,
                                                  std::span<const std::byte> signer_public_key);
    bool valid() const;
};

enum class TransparencyLeafKind : std::uint8_t {
    DeploymentAnchor = 0,
    RuntimeObservation = 1,
};

struct TransparencyLogLeaf {
    std::uint32_t storage_schema = 1;
    std::uint64_t index = 0;
    std::string id;
    std::string log_id;
    TransparencyLeafKind kind = TransparencyLeafKind::DeploymentAnchor;
    std::optional<DeploymentTransparencyAnchor> deployment_anchor;
    std::optional<RuntimeObservationAttestationSet> runtime_observation;

    bool valid() const;
};

struct TransparencyLogCheckpoint {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::uint64_t tree_size = 0;
    std::string root_hash;
    std::string previous_checkpoint_id;
    std::string signer_service_id;
    std::string signer_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
};

struct TransparencyLogRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string log_id;
    TransparencyLogLeaf leaf;
    TransparencyLogCheckpoint checkpoint;

    bool valid() const;
};

struct TransparencyInclusionProof {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string checkpoint_id;
    std::string leaf_id;
    std::uint64_t leaf_index = 0;
    std::uint64_t tree_size = 0;
    std::string root_hash;
    std::vector<std::string> sibling_hashes;

    bool valid() const;
};

struct TransparencyConsistencyWitness {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string old_checkpoint_id;
    std::string new_checkpoint_id;
    std::uint64_t old_tree_size = 0;
    std::uint64_t new_tree_size = 0;
    std::string old_root_hash;
    std::string new_root_hash;
    std::vector<std::string> ordered_leaf_ids;

    bool valid() const;
};

struct TransparencyMerkleSubtree {
    std::uint8_t level = 0;
    std::string hash;

    bool valid() const;
};

struct TransparencyCompactConsistencyProof {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string old_checkpoint_id;
    std::string new_checkpoint_id;
    std::uint64_t old_tree_size = 0;
    std::uint64_t new_tree_size = 0;
    std::string old_root_hash;
    std::string new_root_hash;
    std::vector<TransparencyMerkleSubtree> old_frontier;
    std::vector<TransparencyMerkleSubtree> appended_subtrees;

    bool valid() const;
};

struct TransparencyLogAuditReport {
    std::string id;
    std::string log_id;
    std::string current_checkpoint_id;
    std::string current_root_hash;
    std::size_t verified_records = 0;
    std::size_t deployment_anchor_count = 0;
    std::size_t runtime_observation_count = 0;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct TransparencyLogLoadOptions {
    std::size_t maximum_records = 1'000'000;
    std::size_t maximum_attestations_per_observation = 100'000;
    std::size_t maximum_total_attestations = 1'000'000;
    std::uintmax_t maximum_manifest_bytes = 65'536ULL;
    std::uintmax_t maximum_record_bytes = 16'777'216ULL;
    CancellationToken cancellation;
};

class TransparencyLog {
  public:
    static Result<TransparencyLog> create(const std::filesystem::path& directory,
                                          TransparencyLogIdentity identity);
    static Result<TransparencyLog> open(const std::filesystem::path& directory,
                                        const TransparencyLogIdentity& expected_identity,
                                        const std::string& expected_checkpoint_id,
                                        const TransparencyLogLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const TransparencyLogIdentity& identity() const noexcept { return identity_; }
    const std::vector<TransparencyLogRecord>& records() const noexcept { return records_; }
    const std::string& current_checkpoint_id() const noexcept { return current_checkpoint_id_; }
    const std::string& current_root_hash() const noexcept { return current_root_hash_; }
    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<TransparencyLogRecord>
    publish_deployment_anchor(DeploymentTransparencyAnchor anchor,
                              std::span<const std::byte> signer_secret_key,
                              const std::string& expected_current_checkpoint_id);
    Result<TransparencyLogRecord>
    publish_runtime_observation(RuntimeObservationAttestationSet observation,
                                std::span<const std::byte> signer_secret_key,
                                const std::string& expected_current_checkpoint_id);

    Result<TransparencyInclusionProof> inclusion_proof(std::uint64_t leaf_index) const;
    Result<TransparencyConsistencyWitness> consistency_witness(std::uint64_t old_tree_size) const;
    Result<TransparencyCompactConsistencyProof> compact_consistency_proof(std::uint64_t old_tree_size) const;
    Result<TransparencyLogAuditReport> audit() const;

  private:
    Result<TransparencyLogRecord> publish_leaf(TransparencyLogLeaf leaf,
                                               std::span<const std::byte> signer_secret_key,
                                               const std::string& expected_current_checkpoint_id);

    std::filesystem::path directory_;
    TransparencyLogIdentity identity_;
    std::vector<TransparencyLogRecord> records_;
    std::string current_checkpoint_id_;
    std::string current_root_hash_;
    std::array<std::string, 64> merkle_frontier_{};
    TransparencyLogLoadOptions options_;
};

Result<void> verify_transparency_log_checkpoint(const TransparencyLogIdentity& identity,
                                                const TransparencyLogCheckpoint& checkpoint);

Result<void> verify_transparency_inclusion(const TransparencyLogIdentity& identity,
                                           const TransparencyLogCheckpoint& checkpoint,
                                           const TransparencyLogLeaf& leaf,
                                           const TransparencyInclusionProof& proof);

Result<void> verify_transparency_consistency(const TransparencyLogIdentity& identity,
                                             const TransparencyLogCheckpoint& old_checkpoint,
                                             const TransparencyLogCheckpoint& new_checkpoint,
                                             const TransparencyConsistencyWitness& witness);

Result<void> verify_transparency_compact_consistency(const TransparencyLogIdentity& identity,
                                                     const TransparencyLogCheckpoint& old_checkpoint,
                                                     const TransparencyLogCheckpoint& new_checkpoint,
                                                     const TransparencyCompactConsistencyProof& proof);

std::string transparency_leaf_kind_name(TransparencyLeafKind kind);

} // namespace rbfsafe

// Witness quorum and gossip.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

struct TransparencyCheckpointWitnessPolicy {
    std::uint32_t minimum_witnesses = 2;
    bool require_distinct_services = true;
    bool exclude_log_signer = true;
};

bool valid_transparency_checkpoint_witness_policy(const TransparencyCheckpointWitnessPolicy& policy);

struct TransparencyCheckpointCosignature {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string checkpoint_id;
    std::uint64_t tree_size = 0;
    std::string root_hash;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string witness_service_id;
    std::string witness_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
};

Result<TransparencyCheckpointCosignature> sign_transparency_checkpoint_witness(
    const TransparencyLogIdentity& identity, const TransparencyLogCheckpoint& checkpoint,
    const ServiceTrustBundle& trust_bundle, std::string witness_service_id, std::string witness_key_id,
    std::span<const std::byte> ed25519_secret_key);

Result<void> verify_transparency_checkpoint_witness(const TransparencyLogIdentity& identity,
                                                    const TransparencyLogCheckpoint& checkpoint,
                                                    const TransparencyCheckpointCosignature& cosignature,
                                                    const ServiceTrustBundle& trust_bundle);

struct WitnessedTransparencyCheckpoint {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    TransparencyCheckpointWitnessPolicy policy;
    TransparencyLogCheckpoint checkpoint;
    std::vector<TransparencyCheckpointCosignature> cosignatures;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<WitnessedTransparencyCheckpoint> assemble_witnessed_transparency_checkpoint(
    const TransparencyLogIdentity& identity, TransparencyLogCheckpoint checkpoint,
    TransparencyCheckpointWitnessPolicy policy, std::vector<TransparencyCheckpointCosignature> cosignatures,
    const ServiceTrustBundle& trust_bundle);

Result<void>
verify_witnessed_transparency_checkpoint(const TransparencyLogIdentity& identity,
                                         const WitnessedTransparencyCheckpoint& witnessed_checkpoint,
                                         const ServiceTrustBundle& trust_bundle);

struct TransparencyCheckpointGossip {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::uint64_t sender_sequence = 0;
    std::string parent_gossip_id;
    std::string recipient_service_id;
    std::string sender_service_id;
    std::string sender_key_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    WitnessedTransparencyCheckpoint witnessed_checkpoint;
    std::optional<TransparencyCompactConsistencyProof> consistency_proof;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<TransparencyCheckpointGossip> sign_transparency_checkpoint_gossip(
    const TransparencyLogIdentity& identity, WitnessedTransparencyCheckpoint witnessed_checkpoint,
    std::optional<TransparencyCompactConsistencyProof> consistency_proof, std::string recipient_service_id,
    std::uint64_t sender_sequence, std::string parent_gossip_id, const ServiceTrustBundle& trust_bundle,
    std::string sender_service_id, std::string sender_key_id, std::span<const std::byte> ed25519_secret_key);

Result<void> verify_transparency_checkpoint_gossip(const TransparencyLogIdentity& identity,
                                                   const TransparencyCheckpointGossip& gossip,
                                                   const ServiceTrustBundle& trust_bundle);

enum class TransparencyGossipConflictType : std::uint8_t {
    SameSizeEquivocation = 0,
    InvalidConsistencyProof = 1,
};

struct TransparencyGossipConflict {
    std::string id;
    TransparencyGossipConflictType type = TransparencyGossipConflictType::SameSizeEquivocation;
    std::string first_gossip_id;
    std::string second_gossip_id;
    std::string first_checkpoint_id;
    std::string second_checkpoint_id;
    std::uint64_t first_tree_size = 0;
    std::uint64_t second_tree_size = 0;
    std::string consistency_proof_id;

    bool valid() const;
};

enum class TransparencyGossipStatus : std::uint8_t {
    Consistent = 0,
    Incomplete = 1,
    SplitView = 2,
};

struct TransparencyGossipAuditReport {
    std::string id;
    std::string log_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    TransparencyGossipStatus status = TransparencyGossipStatus::Incomplete;
    std::size_t authenticated_gossip_count = 0;
    std::size_t unique_checkpoint_count = 0;
    std::size_t linked_checkpoint_pairs = 0;
    std::size_t unlinked_checkpoint_pairs = 0;
    std::vector<TransparencyGossipConflict> conflicts;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct TransparencyGossipAuditOptions {
    std::size_t maximum_gossip_messages = 100'000;
    std::size_t maximum_unique_checkpoints = 10'000;
    std::size_t maximum_pair_checks = 1'000'000;
    std::size_t maximum_graph_steps = 10'000'000;
    CancellationToken cancellation;
};

struct TransparencyGossipRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string log_id;
    std::string trust_bundle_id;
    TransparencyCheckpointGossip gossip;

    bool valid() const;
};

struct TransparencyGossipArchiveLoadOptions {
    std::size_t maximum_records = 100'000;
    std::size_t maximum_witnesses_per_checkpoint = 100'000;
    std::size_t maximum_total_witnesses = 1'000'000;
    std::size_t maximum_proof_subtrees = 256;
    std::size_t maximum_total_proof_subtrees = 1'000'000;
    std::size_t maximum_unique_checkpoints = 10'000;
    std::size_t maximum_pair_checks = 1'000'000;
    std::size_t maximum_graph_steps = 10'000'000;
    std::uintmax_t maximum_manifest_bytes = 65'536ULL;
    std::uintmax_t maximum_record_bytes = 16'777'216ULL;
    CancellationToken cancellation;
};

class TransparencyGossipArchive {
  public:
    static Result<TransparencyGossipArchive> create(const std::filesystem::path& directory,
                                                    TransparencyLogIdentity log_identity,
                                                    const ServiceTrustBundle& trust_bundle);
    static Result<TransparencyGossipArchive> open(const std::filesystem::path& directory,
                                                  const TransparencyLogIdentity& expected_log_identity,
                                                  const ServiceTrustBundle& trust_bundle,
                                                  const std::string& expected_trust_bundle_id,
                                                  const std::string& expected_head_record_id,
                                                  const TransparencyGossipArchiveLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const TransparencyLogIdentity& log_identity() const noexcept { return log_identity_; }
    const std::string& trust_bundle_id() const noexcept { return trust_bundle_id_; }
    std::uint64_t trust_bundle_sequence() const noexcept { return trust_bundle_sequence_; }
    const std::vector<TransparencyGossipRecord>& records() const noexcept { return records_; }
    const std::string& current_record_id() const noexcept { return current_record_id_; }
    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<TransparencyGossipRecord> publish(TransparencyCheckpointGossip gossip,
                                             const std::string& expected_head_record_id);
    Result<TransparencyGossipAuditReport> audit() const;

  private:
    std::filesystem::path directory_;
    TransparencyLogIdentity log_identity_;
    ServiceTrustBundle trust_bundle_;
    std::string trust_bundle_id_;
    std::uint64_t trust_bundle_sequence_ = 0;
    std::vector<TransparencyGossipRecord> records_;
    std::string current_record_id_;
    TransparencyGossipArchiveLoadOptions options_;
};

Result<TransparencyGossipAuditReport> audit_transparency_checkpoint_gossip(
    const TransparencyLogIdentity& identity, std::span<const TransparencyCheckpointGossip> gossip,
    const ServiceTrustBundle& trust_bundle, const TransparencyGossipAuditOptions& options = {});

std::string transparency_gossip_conflict_type_name(TransparencyGossipConflictType type);
std::string transparency_gossip_status_name(TransparencyGossipStatus status);

} // namespace rbfsafe

// Verifiable hardware and time provenance.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class HardwareAttestationScope : std::uint8_t {
    ArtifactFetch = 0,
    ArtifactPublish = 1,
    TrustRotation = 2,
    DeploymentReview = 3,
    ExecutionControl = 4,
    RuntimeObservation = 5,
    TransparencyLog = 6,
    TransparencyWitness = 7,
    ExternalTime = 8,
};

struct HardwareAttestationAdapterPin {
    std::string adapter_id;
    std::string adapter_version;
    std::string statement_format;

    bool valid() const;
};

struct HardwareAttestationAuthority {
    std::string service_id;
    std::string key_id;

    bool valid() const;
};

struct HardwareKeyAttestationInput {
    std::uint64_t sequence = 1;
    std::string parent_statement_id;
    std::string subject_service_id;
    std::string subject_key_id;
    std::array<std::byte, kEd25519PublicKeyBytes> subject_public_key{};
    HardwareAttestationAdapterPin adapter;
    std::string vendor_id;
    std::string product_id;
    std::string evidence_digest;
    std::string nonce_digest;
    std::vector<HardwareAttestationScope> scopes;
};

struct HardwareKeyAttestationStatement {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_statement_id;
    std::string subject_service_id;
    std::string subject_key_id;
    std::array<std::byte, kEd25519PublicKeyBytes> subject_public_key{};
    HardwareAttestationAdapterPin adapter;
    std::string vendor_id;
    std::string product_id;
    std::string evidence_digest;
    std::string nonce_digest;
    std::vector<HardwareAttestationScope> scopes;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string attester_service_id;
    std::string attester_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<HardwareKeyAttestationStatement>
sign_hardware_key_attestation_statement(HardwareKeyAttestationInput input,
                                        const ServiceTrustBundle& trust_bundle,
                                        std::string attester_service_id, std::string attester_key_id,
                                        std::span<const std::byte> ed25519_secret_key);

Result<void> verify_hardware_key_attestation_statement(const HardwareKeyAttestationStatement& statement,
                                                       const ServiceTrustBundle& trust_bundle);

struct HardwareKeyProvenancePolicy {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::uint32_t minimum_statements = 1;
    bool require_distinct_attesters = true;
    std::size_t maximum_chain_length = 1'000;
    std::vector<HardwareAttestationScope> required_scopes;
    std::vector<HardwareAttestationAdapterPin> allowed_adapters;
    std::vector<HardwareAttestationAuthority> allowed_authorities;
    std::vector<std::string> allowed_vendor_ids;

    static Result<HardwareKeyProvenancePolicy>
    create(std::uint32_t minimum_statements, bool require_distinct_attesters,
           std::size_t maximum_chain_length, std::vector<HardwareAttestationScope> required_scopes,
           std::vector<HardwareAttestationAdapterPin> allowed_adapters,
           std::vector<HardwareAttestationAuthority> allowed_authorities,
           std::vector<std::string> allowed_vendor_ids);

    bool valid() const;
};

enum class HardwareProvenanceStatus : std::uint8_t {
    Satisfied = 0,
    Incomplete = 1,
};

struct HardwareKeyProvenanceReport {
    std::string id;
    std::string subject_service_id;
    std::string subject_key_id;
    std::string trust_bundle_id;
    std::string policy_id;
    HardwareProvenanceStatus status = HardwareProvenanceStatus::Incomplete;
    std::size_t authenticated_statement_count = 0;
    std::size_t distinct_attester_count = 0;
    std::string head_statement_id;
    std::vector<std::string> statement_ids;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct ProvenanceReplayOptions {
    std::size_t maximum_statements = 1'000;
    std::size_t maximum_time_assertions = 10'000;
    CancellationToken cancellation;
};

Result<HardwareKeyProvenanceReport> replay_hardware_key_provenance(
    std::span<const HardwareKeyAttestationStatement> statements, const ServicePublicKey& expected_subject_key,
    const ServiceTrustBundle& trust_bundle, const HardwareKeyProvenancePolicy& policy,
    const ProvenanceReplayOptions& options = {});

struct ExternalTimeAssertionInput {
    std::uint64_t source_sequence = 1;
    std::string parent_assertion_id;
    std::string subject_id;
    std::string clock_id;
    std::uint64_t asserted_time_ns = 0;
    std::uint64_t uncertainty_ns = 0;
};

struct ExternalTimeAssertion {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::uint64_t source_sequence = 0;
    std::string parent_assertion_id;
    std::string subject_id;
    std::string clock_id;
    std::uint64_t asserted_time_ns = 0;
    std::uint64_t uncertainty_ns = 0;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string source_service_id;
    std::string source_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<ExternalTimeAssertion> sign_external_time_assertion(ExternalTimeAssertionInput input,
                                                           const ServiceTrustBundle& trust_bundle,
                                                           std::string source_service_id,
                                                           std::string source_key_id,
                                                           std::span<const std::byte> ed25519_secret_key);

Result<void> verify_external_time_assertion(const ExternalTimeAssertion& assertion,
                                            const ServiceTrustBundle& trust_bundle);

struct ExternalTimeSource {
    std::string service_id;
    std::string key_id;

    bool valid() const;
};

struct ExternalTimeFreshnessPolicy {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string clock_id;
    std::uint64_t maximum_age_ns = 0;
    std::uint64_t maximum_future_skew_ns = 0;
    std::uint64_t maximum_uncertainty_ns = 0;
    std::uint32_t minimum_sources = 1;
    bool require_distinct_services = true;
    std::size_t maximum_assertions = 10'000;
    std::vector<ExternalTimeSource> allowed_sources;

    static Result<ExternalTimeFreshnessPolicy>
    create(std::string clock_id, std::uint64_t maximum_age_ns, std::uint64_t maximum_future_skew_ns,
           std::uint64_t maximum_uncertainty_ns, std::uint32_t minimum_sources,
           bool require_distinct_services, std::size_t maximum_assertions,
           std::vector<ExternalTimeSource> allowed_sources);

    bool valid() const;
};

enum class ExternalTimeFreshnessStatus : std::uint8_t {
    Fresh = 0,
    Incomplete = 1,
    Stale = 2,
    Future = 3,
    Inconsistent = 4,
};

struct ExternalTimeFreshnessReport {
    std::string id;
    std::string subject_id;
    std::string trust_bundle_id;
    std::string policy_id;
    std::string clock_id;
    ExternalTimeFreshnessStatus status = ExternalTimeFreshnessStatus::Incomplete;
    std::uint64_t evaluated_at_ns = 0;
    std::uint64_t intersection_lower_ns = 0;
    std::uint64_t intersection_upper_ns = 0;
    std::size_t authenticated_source_count = 0;
    std::vector<std::string> assertion_ids;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<ExternalTimeFreshnessReport>
evaluate_external_time_freshness(std::string subject_id, std::span<const ExternalTimeAssertion> assertions,
                                 const ServiceTrustBundle& trust_bundle,
                                 const ExternalTimeFreshnessPolicy& policy, std::uint64_t evaluated_at_ns,
                                 const ProvenanceReplayOptions& options = {});

struct VerifiableProvenanceBundleLoadOptions {
    std::size_t maximum_statements = 1'000;
    std::size_t maximum_time_assertions = 10'000;
    std::size_t maximum_policy_entries = 10'000;
    std::uintmax_t maximum_payload_bytes = 16'777'216ULL;
    CancellationToken cancellation;
};

class VerifiableProvenanceBundle {
  public:
    static Result<VerifiableProvenanceBundle>
    create(ServicePublicKey subject_key, HardwareKeyProvenancePolicy hardware_policy,
           ExternalTimeFreshnessPolicy freshness_policy,
           std::vector<HardwareKeyAttestationStatement> hardware_statements,
           std::vector<ExternalTimeAssertion> time_assertions, const ServiceTrustBundle& trust_bundle);

    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    const std::string& id() const noexcept { return id_; }
    const std::string& trust_bundle_id() const noexcept { return trust_bundle_id_; }
    std::uint64_t trust_bundle_sequence() const noexcept { return trust_bundle_sequence_; }
    const ServicePublicKey& subject_key() const noexcept { return subject_key_; }
    const HardwareKeyProvenancePolicy& hardware_policy() const noexcept { return hardware_policy_; }
    const ExternalTimeFreshnessPolicy& freshness_policy() const noexcept { return freshness_policy_; }
    const std::vector<HardwareKeyAttestationStatement>& hardware_statements() const noexcept {
        return hardware_statements_;
    }
    const std::vector<ExternalTimeAssertion>& time_assertions() const noexcept { return time_assertions_; }

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<VerifiableProvenanceBundle> load(const std::filesystem::path& path,
                                                   const VerifiableProvenanceBundleLoadOptions& options = {});

  private:
    friend Result<void> save_verifiable_provenance_bundle(const VerifiableProvenanceBundle&,
                                                          const std::filesystem::path&, const SaveOptions&);
    friend Result<VerifiableProvenanceBundle>
    load_verifiable_provenance_bundle(const std::filesystem::path&,
                                      const VerifiableProvenanceBundleLoadOptions&);

    std::uint32_t storage_schema_ = 1;
    std::string id_;
    std::string trust_bundle_id_;
    std::uint64_t trust_bundle_sequence_ = 0;
    ServicePublicKey subject_key_;
    HardwareKeyProvenancePolicy hardware_policy_;
    ExternalTimeFreshnessPolicy freshness_policy_;
    std::vector<HardwareKeyAttestationStatement> hardware_statements_;
    std::vector<ExternalTimeAssertion> time_assertions_;
};

struct VerifiableProvenanceAuditReport {
    std::string id;
    std::string bundle_id;
    HardwareKeyProvenanceReport hardware;
    ExternalTimeFreshnessReport freshness;

    bool valid() const;
    bool ready() const noexcept;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<VerifiableProvenanceAuditReport>
replay_verifiable_provenance(const VerifiableProvenanceBundle& bundle, const ServiceTrustBundle& trust_bundle,
                             std::uint64_t evaluated_at_ns, const ProvenanceReplayOptions& options = {});

Result<void> save_verifiable_provenance_bundle(const VerifiableProvenanceBundle& bundle,
                                               const std::filesystem::path& path, const SaveOptions& options);
Result<VerifiableProvenanceBundle>
load_verifiable_provenance_bundle(const std::filesystem::path& path,
                                  const VerifiableProvenanceBundleLoadOptions& options);

std::string hardware_attestation_scope_name(HardwareAttestationScope scope);
std::string hardware_provenance_status_name(HardwareProvenanceStatus status);
std::string external_time_freshness_status_name(ExternalTimeFreshnessStatus status);

} // namespace rbfsafe
