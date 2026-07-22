#include "test_support.h"

#include <memory>
#include <string>

namespace {

class SplitValidator final : public rbfsafe::RegionValidator {
  public:
    rbfsafe::Result<rbfsafe::RegionValidation> validate(const rbfsafe::SerialRobotModel& robot,
                                                        const rbfsafe::SceneSnapshot&,
                                                        const rbfsafe::CspaceAabb& domain) const override {
        if (domain.axes().front().width() > 1.0)
            return rbfsafe::RegionValidation{};
        auto envelope = rbfsafe::compute_ifk_aa_link_envelope(robot, domain);
        if (!envelope)
            return envelope.error();
        return rbfsafe::RegionValidation{rbfsafe::ValidationDisposition::CertifiedFree, 0.5,
                                         std::move(envelope).value()};
    }

    std::string algorithm_name() const override { return "shield-test-split"; }
    std::string algorithm_version() const override { return "1"; }
};

rbfsafe::SerialRobotModel robot_model() {
    return rbfsafe::SerialRobotModel("shield-1r", {{0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute}},
                                     {{-1.0, 1.0}}, {0.02});
}

} // namespace

int main() {
    using namespace rbfsafe;
    const auto robot = robot_model();
    const SceneSnapshot scene({}, "shield-empty-v1");
    AtlasBuilder builder(std::make_shared<SplitValidator>());
    auto built = builder.build(robot, scene, {{-0.5}, {0.5}});
    CHECK(built);
    CHECK(built.value().atlas.regions().size() == 2);
    auto atlas = std::make_shared<SafeAtlas>(built.value().atlas);

    RuntimeShield shield;
    const Configuration current{-0.5};
    auto accepted = shield.check_action(robot, scene, *atlas, current, ShieldAction{JointDeltaAction{{0.2}}});
    CHECK(accepted);
    CHECK(accepted.value().outcome == ShieldOutcome::Accept);
    CHECK(accepted.value().reason == ShieldReason::Certified);
    CHECK(accepted.value().audit.has_value());
    CHECK(accepted.value().audit->status == TrajectoryAuditStatus::Certified);
    CHECK(accepted.value().connectivity_certificate.has_value());
    CHECK(accepted.value().action_digest.size() == 64);
    CHECK(accepted.value().evidence == EvidenceLevel::CertifiedConnectivity);
    CHECK(accepted.value().output_trajectory.size() == 2);
    CHECK(close(accepted.value().output_trajectory.back()[0], -0.3));

    auto repeated = shield.check_action(robot, scene, *atlas, current, ShieldAction{JointDeltaAction{{0.2}}});
    CHECK(repeated);
    CHECK(repeated.value().id == accepted.value().id);

    ShieldOptions repair_options;
    repair_options.maximum_waypoint_repair_distance = 0.6;
    repair_options.maximum_total_repair_distance = 2.0;
    auto repaired = shield.check_action(robot, scene, *atlas, current, ShieldAction{JointDeltaAction{{2.0}}},
                                        repair_options);
    CHECK(repaired);
    CHECK(repaired.value().outcome == ShieldOutcome::Repair);
    CHECK(repaired.value().reason == ShieldReason::JointTargetRepaired);
    CHECK(close(repaired.value().requested_target[0], 1.5));
    CHECK(close(repaired.value().output_trajectory.back()[0], 1.0));
    CHECK(close(repaired.value().repair_distance, 0.5));

    ShieldOptions exhausted_repair = repair_options;
    exhausted_repair.maximum_repair_region_tests = 1;
    auto exhausted = shield.check_action(robot, scene, *atlas, current, ShieldAction{JointDeltaAction{{2.0}}},
                                         exhausted_repair);
    CHECK(!exhausted);
    CHECK(exhausted.error().code == StatusCode::ResourceLimit);

    ShieldOptions no_repair;
    no_repair.allow_repair = false;
    auto rejected =
        shield.check_action(robot, scene, *atlas, current, ShieldAction{JointDeltaAction{{2.0}}}, no_repair);
    CHECK(rejected);
    CHECK(rejected.value().outcome == ShieldOutcome::Reject);
    CHECK(rejected.value().reason == ShieldReason::RepairDisabled);
    CHECK(rejected.value().evidence == EvidenceLevel::Unknown);

    TrajectoryAction safe_trajectory{{{-0.25}, {0.5}}};
    auto trajectory_accepted =
        shield.check_action(robot, scene, *atlas, current, ShieldAction{safe_trajectory});
    CHECK(trajectory_accepted);
    CHECK(trajectory_accepted.value().outcome == ShieldOutcome::Accept);

    TrajectoryAction unsafe_trajectory{{{-0.25}, {1.4}}};
    auto trajectory_repaired =
        shield.check_action(robot, scene, *atlas, current, ShieldAction{unsafe_trajectory}, repair_options);
    CHECK(trajectory_repaired);
    CHECK(trajectory_repaired.value().outcome == ShieldOutcome::Repair);
    CHECK(trajectory_repaired.value().reason == ShieldReason::TrajectoryRepaired);
    CHECK(close(trajectory_repaired.value().output_trajectory.back()[0], 1.0));

    auto pose = robot.end_effector_pose(Configuration{0.6});
    CHECK(pose);
    auto ee = shield.check_action(robot, scene, *atlas, current,
                                  ShieldAction{EndEffectorAction{pose.value()}}, repair_options);
    CHECK(ee);
    CHECK(ee.value().outcome == ShieldOutcome::Repair);
    CHECK(ee.value().reason == ShieldReason::SafeIkRoute);
    CHECK(close(ee.value().requested_target[0], 0.6, 2e-4));

    std::vector<ShieldAction> proposals{JointDeltaAction{{2.0}}, JointDeltaAction{{0.1}},
                                        JointDeltaAction{{0.2}}};
    ShieldBatchOptions batch_options;
    batch_options.action = repair_options;
    auto batch = shield.check_actions(robot, scene, *atlas, current, proposals, batch_options);
    CHECK(batch);
    CHECK(batch.value().decisions.size() == 3);
    CHECK(batch.value().selected_index == 1);
    CHECK(batch.value().decisions[0].outcome == ShieldOutcome::Repair);
    CHECK(batch.value().decisions[1].outcome == ShieldOutcome::Accept);

    auto telemetry = shield.telemetry();
    CHECK(telemetry.total_actions == 10);
    CHECK(telemetry.accepted_actions == 5);
    CHECK(telemetry.repaired_actions == 4);
    CHECK(telemetry.rejected_actions == 1);
    CHECK(telemetry.batches == 1);
    CHECK(telemetry.successful_repairs == 4);
    CHECK(telemetry.output_waypoints > 0);
    shield.reset_telemetry();
    CHECK(shield.telemetry().total_actions == 0);

    auto outside =
        shield.check_action(robot, scene, *atlas, Configuration{1.2}, ShieldAction{JointDeltaAction{{-0.1}}});
    CHECK(outside);
    CHECK(outside.value().outcome == ShieldOutcome::Reject);
    CHECK(outside.value().reason == ShieldReason::CurrentStateNotCertified);
    CHECK(!shield.check_action(robot, scene, *atlas, current, ShieldAction{TrajectoryAction{}}));
    CHECK(!shield.check_action(robot, SceneSnapshot({}, "wrong-scene"), *atlas, current,
                               ShieldAction{JointDeltaAction{{0.1}}}));

    ShieldBatchOptions tiny_batch;
    tiny_batch.maximum_actions = 1;
    auto over_budget = shield.check_actions(robot, scene, *atlas, current, proposals, tiny_batch);
    CHECK(!over_budget);
    CHECK(over_budget.error().code == StatusCode::ResourceLimit);

    Pose3d unreachable{{10.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 1.0}};
    auto unsolved = shield.check_action(robot, scene, *atlas, current,
                                        ShieldAction{EndEffectorAction{unreachable}}, repair_options);
    CHECK(unsolved);
    CHECK(unsolved.value().outcome == ShieldOutcome::Reject);
    CHECK(unsolved.value().reason == ShieldReason::NoSafeIkSolution);

    RuntimeShieldMonitor monitor(atlas, RuntimeMonitorOptions{0.05});
    CHECK(monitor.arm(accepted.value()));
    auto tampered = accepted.value();
    tampered.connectivity_certificate->id[0] = tampered.connectivity_certificate->id[0] == '0' ? '1' : '0';
    CHECK(!monitor.arm(tampered));
    auto on_plan = monitor.observe(Configuration{-0.4}, 1.0);
    CHECK(on_plan);
    CHECK(on_plan.value().state == MonitorState::OnCertifiedPlan);
    CHECK(on_plan.value().evidence == EvidenceLevel::CertifiedConnectivity);
    CHECK(on_plan.value().evidence != EvidenceLevel::RuntimeExecutable);
    auto deviation = monitor.observe(Configuration{0.8}, 2.0);
    CHECK(deviation);
    CHECK(deviation.value().state == MonitorState::CertifiedDeviation);
    CHECK(deviation.value().evidence == EvidenceLevel::CertifiedRegion);
    auto uncertified = monitor.observe(Configuration{1.2}, 3.0);
    CHECK(uncertified);
    CHECK(uncertified.value().state == MonitorState::UncertifiedState);
    CHECK(uncertified.value().evidence == EvidenceLevel::Unknown);
    CHECK(!monitor.observe(Configuration{0.0}, 3.0));
    CHECK(monitor.stats().observations == 3);
    monitor.disarm();
    auto inactive = monitor.observe(Configuration{0.0}, 4.0);
    CHECK(inactive);
    CHECK(inactive.value().state == MonitorState::Inactive);
    RuntimeMonitorOptions tiny_monitor_options;
    tiny_monitor_options.maximum_plan_waypoints = 1;
    RuntimeShieldMonitor tiny_monitor(atlas, tiny_monitor_options);
    CHECK(!tiny_monitor.arm(accepted.value()));

    CancellationToken cancelled;
    cancelled.cancel();
    ShieldOptions cancelled_options;
    cancelled_options.cancellation = cancelled;
    auto cancellation = shield.check_action(robot, scene, *atlas, current,
                                            ShieldAction{JointDeltaAction{{0.1}}}, cancelled_options);
    CHECK(!cancellation);
    CHECK(cancellation.error().code == StatusCode::Cancelled);

    CHECK(shield_outcome_name(ShieldOutcome::Accept) == "ACCEPT");
    CHECK(shield_reason_name(ShieldReason::TrajectoryRepaired) == "trajectory_repaired");
    CHECK(monitor_state_name(MonitorState::UncertifiedState) == "uncertified_state");
    return EXIT_SUCCESS;
}
