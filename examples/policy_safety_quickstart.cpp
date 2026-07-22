#include <rbfsafe/policy.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    using namespace rbfsafe;
    const SerialRobotModel robot("policy-demo", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}}, {{-1.0, 1.0}},
                                 {0.02});
    const SceneSnapshot scene({}, "policy-demo-v1");
    auto built = AtlasBuilder{}.build(robot, scene, {{-0.5}, {0.5}});
    if (!built) {
        std::cerr << built.error().describe() << '\n';
        return EXIT_FAILURE;
    }

    PolicyProposalMetadata primary_metadata;
    primary_metadata.policy_id = "vla-primary";
    primary_metadata.task_id = "demo-pick";
    primary_metadata.episode_id = "episode-1";
    primary_metadata.sequence = 1;
    primary_metadata.confidence = 0.92;
    primary_metadata.state_uncertainty = 0.04;
    primary_metadata.action_uncertainty = 0.05;
    primary_metadata.observation_age_seconds = 0.01;
    primary_metadata.inference_latency_seconds = 0.02;

    auto fallback_metadata = primary_metadata;
    fallback_metadata.policy_id = "vla-fallback";
    fallback_metadata.confidence = 0.75;
    const std::vector<PolicyProposal> proposals{
        {JointDeltaAction{{0.2}}, primary_metadata},
        {JointDeltaAction{{0.1}}, fallback_metadata},
    };

    PolicyGateOptions options;
    options.minimum_confidence = 0.7;
    options.maximum_state_uncertainty = 0.2;
    options.maximum_action_uncertainty = 0.2;
    options.maximum_observation_age_seconds = 0.1;
    options.maximum_inference_latency_seconds = 0.1;
    options.selection_mode = PolicySelectionMode::HighestConfidence;

    LearningPolicySafetyGate gate;
    auto report =
        gate.check_proposals(robot, scene, built.value().atlas, Configuration{-0.5}, proposals, options);
    if (!report) {
        std::cerr << report.error().describe() << '\n';
        return EXIT_FAILURE;
    }
    if (!report.value().selected_index) {
        std::cerr << "no proposal passed both policy and geometric safety gates\n";
        return EXIT_FAILURE;
    }
    const auto selected = *report.value().selected_index;
    std::cout << "selected_policy=" << report.value().decisions[selected].metadata.policy_id
              << " reason=" << policy_gate_reason_name(report.value().decisions[selected].reason)
              << " feedback=" << report.value().feedback[selected].id << '\n';

    if (argc == 2) {
        auto database = PolicyFeedbackDatabase::create(report.value().feedback);
        if (!database) {
            std::cerr << database.error().describe() << '\n';
            return EXIT_FAILURE;
        }
        auto saved = database.value().save(std::filesystem::path(argv[1]));
        if (!saved) {
            std::cerr << saved.error().describe() << '\n';
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
