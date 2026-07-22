#pragma once

#include <rbfsafe/atlas.h>
#include <rbfsafe/model.h>
#include <rbfsafe/safe_ik.h>
#include <rbfsafe/trajectory.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace rbfsafe {

enum class ShieldActionType : std::uint8_t {
    JointDelta = 0,
    EndEffectorPose = 1,
    Trajectory = 2,
};

enum class ShieldOutcome : std::uint8_t {
    Accept = 0,
    Repair = 1,
    Reject = 2,
};

enum class ShieldReason : std::uint8_t {
    Certified = 0,
    JointTargetRepaired = 1,
    SafeIkRoute = 2,
    TrajectoryRepaired = 3,
    CurrentStateNotCertified = 4,
    TargetNotCertified = 5,
    RepairDisabled = 6,
    RepairLimitExceeded = 7,
    NoSafeIkSolution = 8,
    Disconnected = 9,
};

struct JointDeltaAction {
    Configuration delta;
};

struct EndEffectorAction {
    Pose3d target;
};

struct TrajectoryAction {
    std::vector<Configuration> waypoints;
};

using ShieldAction = std::variant<JointDeltaAction, EndEffectorAction, TrajectoryAction>;

struct ShieldOptions {
    bool allow_repair = true;
    double maximum_waypoint_repair_distance = 0.25;
    double maximum_total_repair_distance = 1.0;
    std::size_t maximum_input_waypoints = 10'000;
    std::size_t maximum_output_waypoints = 100'000;
    std::size_t maximum_repair_region_tests = 10'000'000;
    TrajectoryAuditOptions audit;
    SafeIkOptions safe_ik;
    CancellationToken cancellation;
};

struct ShieldDecision {
    ShieldActionType action_type = ShieldActionType::JointDelta;
    ShieldOutcome outcome = ShieldOutcome::Reject;
    ShieldReason reason = ShieldReason::TargetNotCertified;
    std::string id;
    std::string action_digest;
    std::string robot_digest;
    std::string scene_digest;
    Configuration requested_target;
    std::vector<Configuration> output_trajectory;
    std::optional<TrajectoryAuditReport> audit;
    std::optional<Certificate> connectivity_certificate;
    double repair_distance = 0.0;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
};

struct ShieldBatchOptions {
    std::size_t maximum_actions = 256;
    ShieldOptions action;
    CancellationToken cancellation;
};

struct ShieldBatchReport {
    std::vector<ShieldDecision> decisions;
    std::optional<std::size_t> selected_index;
};

struct ShieldTelemetrySnapshot {
    std::uint64_t total_actions = 0;
    std::uint64_t accepted_actions = 0;
    std::uint64_t repaired_actions = 0;
    std::uint64_t rejected_actions = 0;
    std::uint64_t joint_actions = 0;
    std::uint64_t end_effector_actions = 0;
    std::uint64_t trajectory_actions = 0;
    std::uint64_t repair_attempts = 0;
    std::uint64_t successful_repairs = 0;
    std::uint64_t input_waypoints = 0;
    std::uint64_t output_waypoints = 0;
    std::uint64_t batches = 0;
};

class RuntimeShield {
  public:
    Result<ShieldDecision> check_action(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                        const SafeAtlas& atlas, std::span<const double> current,
                                        const ShieldAction& action, const ShieldOptions& options = {});

    Result<ShieldBatchReport> check_actions(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                            const SafeAtlas& atlas, std::span<const double> current,
                                            std::span<const ShieldAction> actions,
                                            const ShieldBatchOptions& options = {});

    ShieldTelemetrySnapshot telemetry() const;
    void reset_telemetry();

  private:
    void record(const ShieldAction& action, const ShieldDecision& decision, bool repair_attempted);

    mutable std::mutex telemetry_mutex_;
    ShieldTelemetrySnapshot telemetry_;
};

enum class MonitorState : std::uint8_t {
    Inactive = 0,
    OnCertifiedPlan = 1,
    CertifiedDeviation = 2,
    UncertifiedState = 3,
};

struct RuntimeMonitorOptions {
    double tracking_tolerance = 0.05;
    std::size_t maximum_plan_waypoints = 100'000;
};

struct MonitorObservation {
    MonitorState state = MonitorState::Inactive;
    std::string decision_id;
    double timestamp = 0.0;
    double distance_to_plan = 0.0;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
};

struct RuntimeMonitorStats {
    std::uint64_t observations = 0;
    std::uint64_t on_plan = 0;
    std::uint64_t certified_deviations = 0;
    std::uint64_t uncertified_states = 0;
};

class RuntimeShieldMonitor {
  public:
    explicit RuntimeShieldMonitor(std::shared_ptr<const SafeAtlas> atlas, RuntimeMonitorOptions options = {});

    Result<void> arm(const ShieldDecision& decision);
    void disarm();
    Result<MonitorObservation> observe(std::span<const double> configuration, double timestamp);
    RuntimeMonitorStats stats() const;

  private:
    std::shared_ptr<const SafeAtlas> atlas_;
    RuntimeMonitorOptions options_;
    mutable std::mutex mutex_;
    std::optional<ShieldDecision> active_;
    std::optional<double> last_timestamp_;
    RuntimeMonitorStats stats_;
};

std::string shield_action_type_name(ShieldActionType type);
std::string shield_outcome_name(ShieldOutcome outcome);
std::string shield_reason_name(ShieldReason reason);
std::string monitor_state_name(MonitorState state);

} // namespace rbfsafe
