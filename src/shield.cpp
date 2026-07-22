#include <rbfsafe/shield.h>

#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace rbfsafe {
namespace {

constexpr double kEqualityTolerance = 1e-12;

double squared_distance(std::span<const double> first, std::span<const double> second) {
    double squared = 0.0;
    for (std::size_t axis = 0; axis < first.size(); ++axis) {
        const double difference = first[axis] - second[axis];
        squared += difference * difference;
    }
    return squared;
}

Configuration project_to_box(std::span<const double> point, const CspaceAabb& box) {
    Configuration projected(point.begin(), point.end());
    for (std::size_t axis = 0; axis < box.dimension(); ++axis) {
        projected[axis] = std::clamp(projected[axis], box.axes()[axis].lower, box.axes()[axis].upper);
    }
    return projected;
}

Result<void> validate_options(const ShieldOptions& options) {
    if (!std::isfinite(options.maximum_waypoint_repair_distance) ||
        options.maximum_waypoint_repair_distance < 0.0 ||
        !std::isfinite(options.maximum_total_repair_distance) ||
        options.maximum_total_repair_distance < 0.0 || options.maximum_input_waypoints == 0 ||
        options.maximum_output_waypoints < 2 || options.maximum_repair_region_tests == 0 ||
        options.audit.maximum_region_tests == 0) {
        return Result<void>::failure(StatusCode::InvalidArgument, "runtime shield options are invalid",
                                     "shield");
    }
    return Result<void>::success();
}

internal::Json configuration_json(std::span<const double> configuration) {
    internal::Json::Array values;
    values.reserve(configuration.size());
    for (const double value : configuration)
        values.emplace_back(value);
    return values;
}

std::string decision_id(const ShieldDecision& decision) {
    internal::Json::Array trajectory;
    trajectory.reserve(decision.output_trajectory.size());
    for (const auto& waypoint : decision.output_trajectory)
        trajectory.emplace_back(configuration_json(waypoint));
    internal::Json document(internal::Json::Object{
        {"action_digest", decision.action_digest},
        {"action_type", static_cast<int>(decision.action_type)},
        {"connectivity_certificate_id",
         decision.connectivity_certificate ? decision.connectivity_certificate->id : ""},
        {"evidence", static_cast<int>(decision.evidence)},
        {"outcome", static_cast<int>(decision.outcome)},
        {"reason", static_cast<int>(decision.reason)},
        {"repair_distance", decision.repair_distance},
        {"requested_target", configuration_json(decision.requested_target)},
        {"robot_digest", decision.robot_digest},
        {"scene_digest", decision.scene_digest},
        {"trajectory", std::move(trajectory)},
    });
    return internal::sha256(document.dump(false));
}

std::string canonical_action_digest(const ShieldAction& action) {
    internal::Json document = std::visit(
        [](const auto& value) -> internal::Json {
            using Action = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Action, JointDeltaAction>) {
                return internal::Json::Object{{"delta", configuration_json(value.delta)},
                                              {"type", "joint_delta"}};
            } else if constexpr (std::is_same_v<Action, EndEffectorAction>) {
                internal::Json::Array position;
                internal::Json::Array orientation;
                for (const double coordinate : value.target.position)
                    position.emplace_back(coordinate);
                for (const double coordinate : value.target.orientation)
                    orientation.emplace_back(coordinate);
                return internal::Json::Object{{"orientation", std::move(orientation)},
                                              {"position", std::move(position)},
                                              {"type", "end_effector_pose"}};
            } else {
                internal::Json::Array waypoints;
                waypoints.reserve(value.waypoints.size());
                for (const auto& waypoint : value.waypoints)
                    waypoints.emplace_back(configuration_json(waypoint));
                return internal::Json::Object{{"type", "trajectory"}, {"waypoints", std::move(waypoints)}};
            }
        },
        action);
    return internal::sha256(document.dump(false));
}

ShieldDecision base_decision(ShieldActionType type, const SafeAtlas& atlas) {
    ShieldDecision decision;
    decision.action_type = type;
    decision.robot_digest = atlas.robot_digest();
    decision.scene_digest = atlas.scene_digest();
    return decision;
}

ShieldDecision reject_decision(ShieldActionType type, ShieldReason reason, const SafeAtlas& atlas,
                               Configuration requested_target = {}) {
    auto decision = base_decision(type, atlas);
    decision.reason = reason;
    decision.requested_target = std::move(requested_target);
    decision.id = decision_id(decision);
    return decision;
}

Result<TrajectoryAuditReport> audit_trajectory(const SafeAtlas& atlas,
                                               std::span<const Configuration> trajectory,
                                               const ShieldOptions& options) {
    TrajectoryAuditOptions audit_options = options.audit;
    audit_options.cancellation = options.cancellation;
    return TrajectoryAuditor{}.audit(atlas, trajectory, audit_options);
}

Result<std::optional<Certificate>> endpoint_certificate(const SafeAtlas& atlas,
                                                        std::span<const Configuration> trajectory) {
    auto route = atlas.route(trajectory.front(), trajectory.back());
    if (!route)
        return route.error();
    if (!route.value())
        return std::optional<Certificate>{};
    return std::optional<Certificate>{route.value()->certificate};
}

Result<ShieldDecision> certified_decision(ShieldDecision decision, const SafeAtlas& atlas,
                                          std::vector<Configuration> trajectory,
                                          const ShieldOptions& options) {
    if (trajectory.size() > options.maximum_output_waypoints) {
        return Result<ShieldDecision>::failure(StatusCode::ResourceLimit,
                                               "shield output exceeds waypoint budget", "shield");
    }
    auto audit = audit_trajectory(atlas, trajectory, options);
    if (!audit)
        return audit.error();
    if (audit.value().status != TrajectoryAuditStatus::Certified) {
        return Result<ShieldDecision>::failure(
            StatusCode::InternalError, "shield attempted to emit an uncertified trajectory", "shield");
    }
    auto certificate = endpoint_certificate(atlas, trajectory);
    if (!certificate)
        return certificate.error();
    if (!certificate.value()) {
        return Result<ShieldDecision>::failure(
            StatusCode::InternalError, "certified shield trajectory has disconnected endpoints", "shield");
    }
    decision.output_trajectory = std::move(trajectory);
    decision.audit = std::move(audit).value();
    decision.connectivity_certificate = std::move(certificate).value();
    decision.evidence = EvidenceLevel::CertifiedConnectivity;
    decision.id = decision_id(decision);
    return decision;
}

std::set<ComponentId> current_components(const SafeAtlas& atlas, std::span<const double> current) {
    std::set<ComponentId> components;
    for (const auto& region : atlas.regions()) {
        if (region.bounds.contains(current))
            components.insert(region.component);
    }
    return components;
}

struct Projection {
    Configuration configuration;
    double distance = std::numeric_limits<double>::infinity();
    RegionId region_id = 0;
};

Result<std::optional<Projection>> project_to_reachable_region(const SafeAtlas& atlas,
                                                              const std::set<ComponentId>& components,
                                                              std::span<const double> target,
                                                              std::size_t& region_tests,
                                                              const ShieldOptions& options) {
    std::optional<Projection> best;
    for (const auto& region : atlas.regions()) {
        if (region_tests == options.maximum_repair_region_tests) {
            return Result<std::optional<Projection>>::failure(
                StatusCode::ResourceLimit, "shield repair region-test budget exhausted", "shield");
        }
        ++region_tests;
        if ((region_tests & 1023U) == 0U && options.cancellation.cancelled()) {
            return Result<std::optional<Projection>>::failure(StatusCode::Cancelled,
                                                              "shield repair was cancelled", "shield");
        }
        if (!components.contains(region.component))
            continue;
        Configuration projected = project_to_box(target, region.bounds);
        const double distance = std::sqrt(squared_distance(target, projected));
        if (!best || distance < best->distance ||
            (distance == best->distance && region.id < best->region_id)) {
            best = Projection{std::move(projected), distance, region.id};
        }
    }
    return best;
}

void append_route(std::vector<Configuration>& output, const AtlasRoute& route) {
    for (const auto& waypoint : route.waypoints) {
        if (output.empty() ||
            squared_distance(output.back(), waypoint) > kEqualityTolerance * kEqualityTolerance)
            output.push_back(waypoint);
    }
}

Result<ShieldDecision> check_joint(const SafeAtlas& atlas, std::span<const double> current,
                                   const JointDeltaAction& action, const ShieldOptions& options) {
    auto delta_status = validate_configuration(action.delta, atlas.dimension(), "joint action delta");
    if (!delta_status)
        return delta_status.error();
    Configuration target(current.begin(), current.end());
    for (std::size_t axis = 0; axis < target.size(); ++axis) {
        target[axis] += action.delta[axis];
        if (!std::isfinite(target[axis])) {
            return Result<ShieldDecision>::failure(StatusCode::InvalidArgument,
                                                   "joint action target is not finite", "shield");
        }
    }
    if (!atlas.contains(current)) {
        return reject_decision(ShieldActionType::JointDelta, ShieldReason::CurrentStateNotCertified, atlas,
                               std::move(target));
    }

    std::vector<Configuration> direct{Configuration(current.begin(), current.end()), target};
    auto direct_audit = audit_trajectory(atlas, direct, options);
    if (!direct_audit)
        return direct_audit.error();
    if (direct_audit.value().status == TrajectoryAuditStatus::Certified) {
        auto decision = base_decision(ShieldActionType::JointDelta, atlas);
        decision.outcome = ShieldOutcome::Accept;
        decision.reason = ShieldReason::Certified;
        decision.requested_target = target;
        return certified_decision(std::move(decision), atlas, std::move(direct), options);
    }
    if (!options.allow_repair) {
        return reject_decision(ShieldActionType::JointDelta, ShieldReason::RepairDisabled, atlas,
                               std::move(target));
    }

    std::size_t region_tests = 0;
    auto projection =
        project_to_reachable_region(atlas, current_components(atlas, current), target, region_tests, options);
    if (!projection)
        return projection.error();
    if (!projection.value() || projection.value()->distance > options.maximum_waypoint_repair_distance ||
        projection.value()->distance > options.maximum_total_repair_distance) {
        return reject_decision(ShieldActionType::JointDelta, ShieldReason::RepairLimitExceeded, atlas,
                               std::move(target));
    }
    auto route = atlas.route(current, projection.value()->configuration);
    if (!route)
        return route.error();
    if (!route.value()) {
        return reject_decision(ShieldActionType::JointDelta, ShieldReason::Disconnected, atlas,
                               std::move(target));
    }
    auto decision = base_decision(ShieldActionType::JointDelta, atlas);
    decision.outcome = ShieldOutcome::Repair;
    decision.reason = ShieldReason::JointTargetRepaired;
    decision.requested_target = target;
    decision.repair_distance = projection.value()->distance;
    return certified_decision(std::move(decision), atlas, std::move(route.value()->waypoints), options);
}

Result<ShieldDecision> check_end_effector(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                          const SafeAtlas& atlas, std::span<const double> current,
                                          const EndEffectorAction& action, const ShieldOptions& options) {
    if (!action.target.valid(1e-6)) {
        return Result<ShieldDecision>::failure(StatusCode::InvalidArgument,
                                               "end-effector action pose is invalid", "shield");
    }
    if (!atlas.contains(current)) {
        return reject_decision(ShieldActionType::EndEffectorPose, ShieldReason::CurrentStateNotCertified,
                               atlas);
    }
    if (!options.allow_repair) {
        return reject_decision(ShieldActionType::EndEffectorPose, ShieldReason::RepairDisabled, atlas);
    }

    SafeIkOptions ik_options = options.safe_ik;
    ik_options.require_connectivity = true;
    ik_options.cancellation = options.cancellation;
    auto report = SafeIkSolver{}.solve(robot, scene, atlas, action.target, current, ik_options);
    if (!report)
        return report.error();
    if (report.value().status == SafeIkStatus::NoSolution) {
        return reject_decision(ShieldActionType::EndEffectorPose, ShieldReason::NoSafeIkSolution, atlas);
    }
    if (report.value().status != SafeIkStatus::SafeConnected || !report.value().connectivity_route) {
        return reject_decision(ShieldActionType::EndEffectorPose, ShieldReason::Disconnected, atlas,
                               std::move(report.value().solution));
    }

    const double distance = std::sqrt(squared_distance(current, report.value().solution));
    if (distance > options.maximum_total_repair_distance) {
        return reject_decision(ShieldActionType::EndEffectorPose, ShieldReason::RepairLimitExceeded, atlas,
                               std::move(report.value().solution));
    }
    auto decision = base_decision(ShieldActionType::EndEffectorPose, atlas);
    decision.outcome = distance <= kEqualityTolerance ? ShieldOutcome::Accept : ShieldOutcome::Repair;
    decision.reason = distance <= kEqualityTolerance ? ShieldReason::Certified : ShieldReason::SafeIkRoute;
    decision.requested_target = report.value().solution;
    decision.repair_distance = distance;
    return certified_decision(std::move(decision), atlas,
                              std::move(report.value().connectivity_route->waypoints), options);
}

Result<ShieldDecision> check_trajectory(const SafeAtlas& atlas, std::span<const double> current,
                                        const TrajectoryAction& action, const ShieldOptions& options) {
    if (action.waypoints.empty()) {
        return Result<ShieldDecision>::failure(StatusCode::InvalidArgument,
                                               "trajectory action must contain a waypoint", "shield");
    }
    if (action.waypoints.size() > options.maximum_input_waypoints) {
        return Result<ShieldDecision>::failure(StatusCode::ResourceLimit,
                                               "trajectory action exceeds input waypoint budget", "shield");
    }
    for (std::size_t index = 0; index < action.waypoints.size(); ++index) {
        auto status = validate_configuration(action.waypoints[index], atlas.dimension(),
                                             "trajectory action waypoint " + std::to_string(index));
        if (!status)
            return status.error();
    }
    if (!atlas.contains(current)) {
        return reject_decision(ShieldActionType::Trajectory, ShieldReason::CurrentStateNotCertified, atlas,
                               action.waypoints.back());
    }

    std::vector<Configuration> direct;
    direct.reserve(action.waypoints.size() + 1);
    direct.emplace_back(current.begin(), current.end());
    for (const auto& waypoint : action.waypoints) {
        if (squared_distance(direct.back(), waypoint) > kEqualityTolerance * kEqualityTolerance)
            direct.push_back(waypoint);
    }
    if (direct.size() == 1)
        direct.push_back(direct.front());
    auto direct_audit = audit_trajectory(atlas, direct, options);
    if (!direct_audit)
        return direct_audit.error();
    if (direct_audit.value().status == TrajectoryAuditStatus::Certified) {
        auto decision = base_decision(ShieldActionType::Trajectory, atlas);
        decision.outcome = ShieldOutcome::Accept;
        decision.reason = ShieldReason::Certified;
        decision.requested_target = action.waypoints.back();
        return certified_decision(std::move(decision), atlas, std::move(direct), options);
    }
    if (!options.allow_repair) {
        return reject_decision(ShieldActionType::Trajectory, ShieldReason::RepairDisabled, atlas,
                               action.waypoints.back());
    }

    const auto components = current_components(atlas, current);
    std::vector<Configuration> repaired_targets;
    repaired_targets.reserve(action.waypoints.size());
    double total_distance = 0.0;
    std::size_t region_tests = 0;
    for (const auto& waypoint : action.waypoints) {
        if (options.cancellation.cancelled()) {
            return Result<ShieldDecision>::failure(StatusCode::Cancelled, "trajectory repair was cancelled",
                                                   "shield");
        }
        auto projection = project_to_reachable_region(atlas, components, waypoint, region_tests, options);
        if (!projection)
            return projection.error();
        if (!projection.value() || projection.value()->distance > options.maximum_waypoint_repair_distance) {
            return reject_decision(ShieldActionType::Trajectory, ShieldReason::RepairLimitExceeded, atlas,
                                   action.waypoints.back());
        }
        total_distance += projection.value()->distance;
        if (total_distance > options.maximum_total_repair_distance) {
            return reject_decision(ShieldActionType::Trajectory, ShieldReason::RepairLimitExceeded, atlas,
                                   action.waypoints.back());
        }
        repaired_targets.push_back(projection.value()->configuration);
    }

    std::vector<Configuration> output;
    output.emplace_back(current.begin(), current.end());
    Configuration cursor(current.begin(), current.end());
    for (const auto& target : repaired_targets) {
        auto route = atlas.route(cursor, target);
        if (!route)
            return route.error();
        if (!route.value()) {
            return reject_decision(ShieldActionType::Trajectory, ShieldReason::Disconnected, atlas,
                                   action.waypoints.back());
        }
        append_route(output, *route.value());
        if (output.size() > options.maximum_output_waypoints) {
            return Result<ShieldDecision>::failure(
                StatusCode::ResourceLimit, "trajectory repair exceeds output waypoint budget", "shield");
        }
        cursor = target;
    }
    if (output.size() == 1)
        output.push_back(output.front());
    auto decision = base_decision(ShieldActionType::Trajectory, atlas);
    decision.outcome = ShieldOutcome::Repair;
    decision.reason = ShieldReason::TrajectoryRepaired;
    decision.requested_target = action.waypoints.back();
    decision.repair_distance = total_distance;
    return certified_decision(std::move(decision), atlas, std::move(output), options);
}

std::size_t input_waypoint_count(const ShieldAction& action) {
    return std::visit(
        [](const auto& value) -> std::size_t {
            using Action = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Action, TrajectoryAction>)
                return value.waypoints.size();
            return 1;
        },
        action);
}

double distance_to_segment_squared(std::span<const double> point, std::span<const double> start,
                                   std::span<const double> finish) {
    double length_squared = 0.0;
    double projection = 0.0;
    for (std::size_t axis = 0; axis < point.size(); ++axis) {
        const double direction = finish[axis] - start[axis];
        length_squared += direction * direction;
        projection += (point[axis] - start[axis]) * direction;
    }
    const double fraction = length_squared == 0.0 ? 0.0 : std::clamp(projection / length_squared, 0.0, 1.0);
    double squared = 0.0;
    for (std::size_t axis = 0; axis < point.size(); ++axis) {
        const double difference = point[axis] - (start[axis] + fraction * (finish[axis] - start[axis]));
        squared += difference * difference;
    }
    return squared;
}

double distance_to_trajectory(std::span<const double> point, std::span<const Configuration> trajectory) {
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index + 1 < trajectory.size(); ++index) {
        best = std::min(best, distance_to_segment_squared(point, trajectory[index], trajectory[index + 1]));
    }
    return std::sqrt(best);
}

} // namespace

Result<ShieldDecision> RuntimeShield::check_action(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                                   const SafeAtlas& atlas, std::span<const double> current,
                                                   const ShieldAction& action, const ShieldOptions& options) {
    auto compatibility = atlas.verify_compatible(robot, scene);
    if (!compatibility)
        return compatibility.error();
    auto current_status = validate_configuration(current, atlas.dimension(), "shield current state");
    if (!current_status)
        return current_status.error();
    auto option_status = validate_options(options);
    if (!option_status)
        return option_status.error();
    if (options.cancellation.cancelled()) {
        return Result<ShieldDecision>::failure(StatusCode::Cancelled, "runtime shield check was cancelled",
                                               "shield");
    }

    Result<ShieldDecision> result = std::visit(
        [&](const auto& value) -> Result<ShieldDecision> {
            using Action = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Action, JointDeltaAction>)
                return check_joint(atlas, current, value, options);
            else if constexpr (std::is_same_v<Action, EndEffectorAction>)
                return check_end_effector(robot, scene, atlas, current, value, options);
            else
                return check_trajectory(atlas, current, value, options);
        },
        action);
    if (result) {
        result.value().action_digest = canonical_action_digest(action);
        result.value().id = decision_id(result.value());
        const bool repair_attempted =
            result.value().outcome == ShieldOutcome::Repair ||
            (result.value().outcome == ShieldOutcome::Reject && options.allow_repair &&
             result.value().reason != ShieldReason::CurrentStateNotCertified);
        record(action, result.value(), repair_attempted);
    }
    return result;
}

Result<ShieldBatchReport> RuntimeShield::check_actions(const SerialRobotModel& robot,
                                                       const SceneSnapshot& scene, const SafeAtlas& atlas,
                                                       std::span<const double> current,
                                                       std::span<const ShieldAction> actions,
                                                       const ShieldBatchOptions& options) {
    if (actions.empty()) {
        return Result<ShieldBatchReport>::failure(StatusCode::InvalidArgument,
                                                  "shield batch must contain an action", "shield batch");
    }
    if (options.maximum_actions == 0) {
        return Result<ShieldBatchReport>::failure(
            StatusCode::InvalidArgument, "shield batch action budget must be positive", "shield batch");
    }
    if (actions.size() > options.maximum_actions) {
        return Result<ShieldBatchReport>::failure(StatusCode::ResourceLimit,
                                                  "shield batch exceeds action budget", "shield batch");
    }
    ShieldBatchReport report;
    report.decisions.reserve(actions.size());
    std::optional<std::size_t> first_repair;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (options.cancellation.cancelled()) {
            return Result<ShieldBatchReport>::failure(StatusCode::Cancelled, "shield batch was cancelled",
                                                      "shield batch");
        }
        ShieldOptions action_options = options.action;
        action_options.cancellation = options.cancellation;
        auto decision = check_action(robot, scene, atlas, current, actions[index], action_options);
        if (!decision)
            return decision.error();
        if (!report.selected_index && decision.value().outcome == ShieldOutcome::Accept)
            report.selected_index = index;
        if (!first_repair && decision.value().outcome == ShieldOutcome::Repair)
            first_repair = index;
        report.decisions.push_back(std::move(decision).value());
    }
    if (!report.selected_index)
        report.selected_index = first_repair;
    {
        std::lock_guard lock(telemetry_mutex_);
        ++telemetry_.batches;
    }
    return report;
}

void RuntimeShield::record(const ShieldAction& action, const ShieldDecision& decision,
                           bool repair_attempted) {
    std::lock_guard lock(telemetry_mutex_);
    ++telemetry_.total_actions;
    switch (decision.action_type) {
    case ShieldActionType::JointDelta:
        ++telemetry_.joint_actions;
        break;
    case ShieldActionType::EndEffectorPose:
        ++telemetry_.end_effector_actions;
        break;
    case ShieldActionType::Trajectory:
        ++telemetry_.trajectory_actions;
        break;
    }
    switch (decision.outcome) {
    case ShieldOutcome::Accept:
        ++telemetry_.accepted_actions;
        break;
    case ShieldOutcome::Repair:
        ++telemetry_.repaired_actions;
        break;
    case ShieldOutcome::Reject:
        ++telemetry_.rejected_actions;
        break;
    }
    if (repair_attempted)
        ++telemetry_.repair_attempts;
    if (decision.outcome == ShieldOutcome::Repair)
        ++telemetry_.successful_repairs;
    telemetry_.input_waypoints += input_waypoint_count(action);
    telemetry_.output_waypoints += decision.output_trajectory.size();
}

ShieldTelemetrySnapshot RuntimeShield::telemetry() const {
    std::lock_guard lock(telemetry_mutex_);
    return telemetry_;
}

void RuntimeShield::reset_telemetry() {
    std::lock_guard lock(telemetry_mutex_);
    telemetry_ = {};
}

RuntimeShieldMonitor::RuntimeShieldMonitor(std::shared_ptr<const SafeAtlas> atlas,
                                           RuntimeMonitorOptions options)
    : atlas_(std::move(atlas)), options_(options) {}

Result<void> RuntimeShieldMonitor::arm(const ShieldDecision& decision) {
    std::lock_guard lock(mutex_);
    if (!atlas_) {
        return Result<void>::failure(StatusCode::InvalidArgument, "runtime monitor Atlas is null",
                                     "runtime monitor");
    }
    if (!std::isfinite(options_.tracking_tolerance) || options_.tracking_tolerance < 0.0 ||
        options_.maximum_plan_waypoints < 2) {
        return Result<void>::failure(StatusCode::InvalidArgument, "runtime monitor tolerance is invalid",
                                     "runtime monitor");
    }
    if (decision.outcome == ShieldOutcome::Reject || decision.output_trajectory.size() < 2 ||
        decision.output_trajectory.size() > options_.maximum_plan_waypoints || !decision.audit ||
        decision.audit->status != TrajectoryAuditStatus::Certified || !decision.connectivity_certificate ||
        decision.connectivity_certificate->level != EvidenceLevel::CertifiedConnectivity ||
        decision.evidence != EvidenceLevel::CertifiedConnectivity || decision.action_digest.size() != 64 ||
        decision.id != decision_id(decision) || decision.robot_digest != atlas_->robot_digest() ||
        decision.scene_digest != atlas_->scene_digest() ||
        decision.connectivity_certificate->robot_digest != atlas_->robot_digest() ||
        decision.connectivity_certificate->scene_digest != atlas_->scene_digest()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "runtime monitor requires a compatible certified shield decision",
                                     "runtime monitor");
    }
    auto audit = TrajectoryAuditor{}.audit(*atlas_, decision.output_trajectory);
    if (!audit)
        return audit.error();
    if (audit.value().status != TrajectoryAuditStatus::Certified) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "runtime monitor decision trajectory is not certified",
                                     "runtime monitor");
    }
    active_ = decision;
    last_timestamp_.reset();
    return Result<void>::success();
}

void RuntimeShieldMonitor::disarm() {
    std::lock_guard lock(mutex_);
    active_.reset();
    last_timestamp_.reset();
}

Result<MonitorObservation> RuntimeShieldMonitor::observe(std::span<const double> configuration,
                                                         double timestamp) {
    std::lock_guard lock(mutex_);
    if (!atlas_) {
        return Result<MonitorObservation>::failure(StatusCode::InvalidArgument,
                                                   "runtime monitor Atlas is null", "runtime monitor");
    }
    auto status = validate_configuration(configuration, atlas_->dimension(), "runtime monitor observation");
    if (!status)
        return status.error();
    if (!std::isfinite(timestamp) || (last_timestamp_ && timestamp <= *last_timestamp_)) {
        return Result<MonitorObservation>::failure(
            StatusCode::InvalidArgument, "runtime monitor timestamps must increase", "runtime monitor");
    }
    last_timestamp_ = timestamp;
    MonitorObservation observation;
    observation.timestamp = timestamp;
    ++stats_.observations;
    if (!active_) {
        observation.state = MonitorState::Inactive;
        return observation;
    }
    observation.decision_id = active_->id;
    observation.distance_to_plan = distance_to_trajectory(configuration, active_->output_trajectory);
    if (!atlas_->contains(configuration)) {
        observation.state = MonitorState::UncertifiedState;
        observation.evidence = EvidenceLevel::Unknown;
        ++stats_.uncertified_states;
    } else if (observation.distance_to_plan <= options_.tracking_tolerance) {
        observation.state = MonitorState::OnCertifiedPlan;
        observation.evidence = EvidenceLevel::CertifiedConnectivity;
        ++stats_.on_plan;
    } else {
        observation.state = MonitorState::CertifiedDeviation;
        observation.evidence = EvidenceLevel::CertifiedRegion;
        ++stats_.certified_deviations;
    }
    return observation;
}

RuntimeMonitorStats RuntimeShieldMonitor::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

std::string shield_action_type_name(ShieldActionType type) {
    switch (type) {
    case ShieldActionType::JointDelta:
        return "joint_delta";
    case ShieldActionType::EndEffectorPose:
        return "end_effector_pose";
    case ShieldActionType::Trajectory:
        return "trajectory";
    }
    return "unknown";
}

std::string shield_outcome_name(ShieldOutcome outcome) {
    switch (outcome) {
    case ShieldOutcome::Accept:
        return "ACCEPT";
    case ShieldOutcome::Repair:
        return "REPAIR";
    case ShieldOutcome::Reject:
        return "REJECT";
    }
    return "REJECT";
}

std::string shield_reason_name(ShieldReason reason) {
    switch (reason) {
    case ShieldReason::Certified:
        return "certified";
    case ShieldReason::JointTargetRepaired:
        return "joint_target_repaired";
    case ShieldReason::SafeIkRoute:
        return "safe_ik_route";
    case ShieldReason::TrajectoryRepaired:
        return "trajectory_repaired";
    case ShieldReason::CurrentStateNotCertified:
        return "current_state_not_certified";
    case ShieldReason::TargetNotCertified:
        return "target_not_certified";
    case ShieldReason::RepairDisabled:
        return "repair_disabled";
    case ShieldReason::RepairLimitExceeded:
        return "repair_limit_exceeded";
    case ShieldReason::NoSafeIkSolution:
        return "no_safe_ik_solution";
    case ShieldReason::Disconnected:
        return "disconnected";
    }
    return "unknown";
}

std::string monitor_state_name(MonitorState state) {
    switch (state) {
    case MonitorState::Inactive:
        return "inactive";
    case MonitorState::OnCertifiedPlan:
        return "on_certified_plan";
    case MonitorState::CertifiedDeviation:
        return "certified_deviation";
    case MonitorState::UncertifiedState:
        return "uncertified_state";
    }
    return "inactive";
}

std::string shield_action_digest(const ShieldAction& action) { return canonical_action_digest(action); }

} // namespace rbfsafe
