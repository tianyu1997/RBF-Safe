#include <rbfsafe/rbfsafe.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

rbfsafe::PolicyCalibrationProfileInput profile_input() {
    rbfsafe::PolicyCalibrationProfileInput input;
    input.policy_id = "vla-policy-a";
    input.policy_model_digest = std::string(64, 'a');
    input.scope_id = "factory-cell-a";
    input.task_id = "shelf-pick";
    input.dataset_digest = std::string(64, 'b');
    input.method = "held-out-reliability-bins";
    input.method_version = "1";
    input.outcome_definition = "shield accepted or repaired proposal";
    input.state_uncertainty_unit = "normalized-joint-range-rms";
    input.action_uncertainty_unit = "normalized-joint-range-rms";
    input.bins = {{0.0, 0.5, 0.25, 500, 100}, {0.5, 1.0, 0.85, 500, 400}};
    return input;
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;
    if (argc != 2) {
        std::cerr << "usage: rbfsafe_calibrated_policy_quickstart <new-profile-json>\n";
        return 2;
    }
    auto profile = PolicyCalibrationProfile::create(profile_input());
    if (!profile || !profile.value().save(std::filesystem::path(argv[1])))
        return 1;

    const SerialRobotModel robot(
        "calibration-2r",
        {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}, {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.0, 1.0}, {-1.0, 1.0}}, {0.02, 0.02});
    const SceneSnapshot scene({}, "calibration-empty-v1");
    auto built = AtlasBuilder{}.build(robot, scene, {{0.0, 0.0}});
    if (!built)
        return 1;

    PolicyProposalMetadata metadata;
    metadata.policy_id = "vla-policy-a";
    metadata.task_id = "shelf-pick";
    metadata.episode_id = "quickstart";
    metadata.sequence = 1;
    metadata.confidence = 0.9;
    metadata.state_uncertainty = 0.05;
    metadata.action_uncertainty = 0.05;
    const std::vector<PolicyProposal> proposals{{JointDeltaAction{{0.1, 0.0}}, metadata}};
    CalibratedPolicyGateOptions options;
    options.policy.minimum_confidence = 0.7;
    CalibratedPolicySafetyGate gate;
    auto report = gate.check_proposals(profile.value(), "factory-cell-a", std::string(64, 'a'), robot, scene,
                                       built.value().atlas, Configuration{0.0, 0.0}, proposals, options);
    if (!report || report.value().policy_report.selected_index != 0)
        return 1;

    const auto& application = report.value().applications.front();
    std::cout << "profile=" << profile.value().id() << '\n'
              << "samples=" << profile.value().sample_count() << '\n'
              << "ece=" << profile.value().expected_calibration_error() << '\n'
              << "raw_confidence=" << application.raw_metadata.confidence << '\n'
              << "conservative_confidence=" << application.conservative_confidence << '\n'
              << "selected=true\n"
              << "runtime_executable=false\n";
    return 0;
}
