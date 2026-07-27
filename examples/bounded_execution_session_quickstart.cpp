#include <rbfsafe/rbfsafe.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string digest(char value) { return std::string(64, value); }

rbfsafe::SerialRobotModel example_robot() {
    return rbfsafe::SerialRobotModel("bounded-execution-planar",
                                     {{0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute},
                                      {0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute}},
                                     {{-1.5, 1.5}, {-1.5, 1.5}}, {0.05, 0.05});
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;
    if (argc != 2) {
        std::cerr << "usage: rbfsafe_bounded_execution_session_quickstart "
                     "<new-output-directory>\n";
        return 2;
    }
    const std::filesystem::path root(argv[1]);
    const auto robot = example_robot();
    const SceneSnapshot scene({}, "bounded-execution-empty-v1");
    auto built = AtlasBuilder{}.build(robot, scene, {{0.0, 0.0}});
    if (!built) {
        std::cerr << built.error().describe() << '\n';
        return 1;
    }
    auto atlas_saved = built.value().atlas.save(root / "atlas");
    if (!atlas_saved) {
        std::cerr << atlas_saved.error().describe() << '\n';
        return 1;
    }
    const auto& atlas = built.value().atlas;

    std::array<std::byte, kEd25519SeedBytes> safety_seed{};
    std::array<std::byte, kEd25519SeedBytes> controls_seed{};
    std::array<std::byte, kEd25519SeedBytes> governance_seed{};
    std::array<std::byte, kEd25519SeedBytes> controller_seed{};
    std::array<std::byte, kEd25519SeedBytes> monitor_seed{};
    for (std::size_t index = 0; index < kEd25519SeedBytes; ++index) {
        safety_seed[index] = static_cast<std::byte>(index + 1);
        controls_seed[index] = static_cast<std::byte>(index + 33);
        governance_seed[index] = static_cast<std::byte>(index + 65);
        controller_seed[index] = static_cast<std::byte>(index + 97);
        monitor_seed[index] = static_cast<std::byte>(index + 129);
    }
    auto safety_pair = ed25519_key_pair_from_seed(safety_seed);
    auto controls_pair = ed25519_key_pair_from_seed(controls_seed);
    auto governance_pair = ed25519_key_pair_from_seed(governance_seed);
    auto controller_pair = ed25519_key_pair_from_seed(controller_seed);
    auto monitor_pair = ed25519_key_pair_from_seed(monitor_seed);
    if (!safety_pair || !controls_pair || !governance_pair || !controller_pair || !monitor_pair) {
        std::cerr << "failed to derive synthetic example keys\n";
        return 1;
    }
    auto safety_key = make_service_public_key("execution-review-safety", safety_pair.value().public_key, 1, 0,
                                              ServiceKeyState::Active, false, true, false);
    auto controls_key = make_service_public_key("execution-review-controls", controls_pair.value().public_key,
                                                1, 0, ServiceKeyState::Active, false, true, false);
    auto governance_key =
        make_service_public_key("execution-trust-governance", governance_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, false, false, true);
    if (!safety_key || !controls_key || !governance_key) {
        std::cerr << "failed to construct synthetic public keys\n";
        return 1;
    }
    ServiceTrustRotationPolicy rotation;
    rotation.minimum_signatures = 1;
    auto bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {controls_key.value(), governance_key.value(), safety_key.value()}, rotation);
    if (!bundle) {
        std::cerr << bundle.error().describe() << '\n';
        return 1;
    }
    auto history = ServiceTrustHistory::create(root / "trust-history", bundle.value(), bundle.value().id());
    if (!history) {
        std::cerr << history.error().describe() << '\n';
        return 1;
    }
    auto checkpoint_signature =
        sign_service_trust_checkpoint(history.value(), governance_key.value().service_id,
                                      governance_key.value().id, governance_pair.value().secret_key);
    if (!checkpoint_signature) {
        std::cerr << checkpoint_signature.error().describe() << '\n';
        return 1;
    }
    auto checkpoint = assemble_service_trust_checkpoint(history.value(), {checkpoint_signature.value()});
    if (!checkpoint) {
        std::cerr << checkpoint.error().describe() << '\n';
        return 1;
    }
    auto checkpoint_saved = checkpoint.value().save(root / "checkpoint.json");
    if (!checkpoint_saved) {
        std::cerr << checkpoint_saved.error().describe() << '\n';
        return 1;
    }

    DeploymentProfileInput profile_input;
    profile_input.deployment_id = "execution-cell-a";
    profile_input.robot_digest = atlas.robot_digest();
    profile_input.controller_digest = digest('b');
    profile_input.platform_digest = digest('c');
    profile_input.runtime_digest = digest('d');
    profile_input.trust_root_bundle_id = bundle.value().id();
    profile_input.trust_checkpoint_id = checkpoint.value().id;
    profile_input.trust_bundle_id = bundle.value().id();
    profile_input.trust_bundle_sequence = bundle.value().sequence();
    profile_input.runtime_constraints.maximum_observation_age_ns = 1'000'000;
    profile_input.runtime_constraints.maximum_command_latency_ns = 20'000;
    profile_input.runtime_constraints.maximum_control_period_ns = 50'000;
    profile_input.runtime_constraints.maximum_consecutive_missed_cycles = 0;
    profile_input.review_policy.minimum_approvals = 2;
    profile_input.review_policy.require_distinct_services = true;
    profile_input.review_policy.required_roles = {DeploymentReviewRole::Safety,
                                                  DeploymentReviewRole::Controls};
    auto profile = DeploymentProfile::create(profile_input);
    if (!profile) {
        std::cerr << profile.error().describe() << '\n';
        return 1;
    }
    auto profile_safety = sign_deployment_profile_approval(
        profile.value(), safety_key.value().service_id, safety_key.value().id, DeploymentReviewRole::Safety,
        safety_pair.value().secret_key);
    auto profile_controls = sign_deployment_profile_approval(
        profile.value(), controls_key.value().service_id, controls_key.value().id,
        DeploymentReviewRole::Controls, controls_pair.value().secret_key);
    if (!profile_safety || !profile_controls) {
        std::cerr << "failed to sign profile reviews\n";
        return 1;
    }
    auto profile_approvals = assemble_deployment_profile_approvals(
        profile.value(), {profile_controls.value(), profile_safety.value()});
    if (!profile_approvals) {
        std::cerr << profile_approvals.error().describe() << '\n';
        return 1;
    }
    auto reviewed =
        ReviewedDeploymentProfile::create(profile.value(), profile_approvals.value(), history.value(),
                                          checkpoint.value(), checkpoint.value().id);
    if (!reviewed) {
        std::cerr << reviewed.error().describe() << '\n';
        return 1;
    }
    auto reviewed_saved = reviewed.value().save(root / "profile.json");
    if (!reviewed_saved) {
        std::cerr << reviewed_saved.error().describe() << '\n';
        return 1;
    }

    const std::vector<Configuration> configurations{{-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}};
    auto sequence = ExecutionCommandSequence::create(atlas, configurations, {0, 50'000, 100'000});
    auto controller = make_execution_endpoint_key("execution-controller", ExecutionEndpointRole::Controller,
                                                  controller_pair.value().public_key);
    auto monitor = make_execution_endpoint_key("execution-monitor", ExecutionEndpointRole::RuntimeMonitor,
                                               monitor_pair.value().public_key);
    if (!sequence || !controller || !monitor) {
        std::cerr << "failed to construct execution subject\n";
        return 1;
    }
    ExecutionSessionRequestInput request_input;
    request_input.session_nonce = digest('9');
    request_input.controller = controller.value();
    request_input.runtime_monitor = monitor.value();
    request_input.limits.maximum_start_delay_ns = 10'000;
    request_input.limits.maximum_duration_ns = 100'000;
    request_input.limits.maximum_commands = 3;
    auto request = ExecutionSessionRequest::create(reviewed.value(), sequence.value(), request_input);
    if (!request) {
        std::cerr << request.error().describe() << '\n';
        return 1;
    }
    auto session_safety = sign_execution_session_approval(request.value(), profile_safety.value(),
                                                          safety_pair.value().secret_key);
    auto session_controls = sign_execution_session_approval(request.value(), profile_controls.value(),
                                                            controls_pair.value().secret_key);
    if (!session_safety || !session_controls) {
        std::cerr << "failed to sign execution review\n";
        return 1;
    }
    auto session_approvals = assemble_execution_session_approvals(
        request.value(), reviewed.value(), {session_safety.value(), session_controls.value()});
    auto controller_ack =
        sign_execution_controller_acknowledgement(request.value(), controller_pair.value().secret_key);
    if (!session_approvals || !controller_ack) {
        std::cerr << "failed to approve execution request\n";
        return 1;
    }

    DeploymentRuntimeSnapshot runtime;
    runtime.deployment_id = profile.value().deployment_id;
    runtime.robot_digest = atlas.robot_digest();
    runtime.controller_digest = profile.value().controller_digest;
    runtime.platform_digest = profile.value().platform_digest;
    runtime.runtime_digest = profile.value().runtime_digest;
    runtime.observation_age_ns = 10'000;
    runtime.command_latency_ns = 20'000;
    runtime.control_period_ns = 50'000;
    runtime.runtime_monitor_active = true;
    runtime.fail_closed_transport_active = true;
    runtime.authenticated_artifacts = true;
    ExecutionRuntimeObservationInput observation_input;
    observation_input.runtime = runtime;
    observation_input.observation_sequence = 7;
    observation_input.observed_monotonic_ns = 1'000'000;
    observation_input.monitor_state = ExecutionMonitorState::ArmedCertifiedSequence;
    auto observation = ExecutionRuntimeObservation::create(request.value(), observation_input);
    if (!observation) {
        std::cerr << observation.error().describe() << '\n';
        return 1;
    }
    auto monitor_ack = sign_execution_monitor_acknowledgement(request.value(), observation.value(),
                                                              monitor_pair.value().secret_key);
    if (!monitor_ack) {
        std::cerr << monitor_ack.error().describe() << '\n';
        return 1;
    }
    auto session = BoundedExecutionSession::create(
        request.value(), sequence.value(), session_approvals.value(), controller_ack.value(),
        monitor_ack.value(), reviewed.value(), bundle.value(), atlas);
    if (!session) {
        std::cerr << session.error().describe() << '\n';
        return 1;
    }
    auto session_saved = session.value().save(root / "session.json");
    if (!session_saved) {
        std::cerr << session_saved.error().describe() << '\n';
        return 1;
    }
    auto authorization = session.value().authorize_command(1, configurations[1], 1'050'001);
    if (!authorization || !authorization.value()) {
        std::cerr << "exact example command was not authorized\n";
        return 1;
    }

    std::cout << "atlas=" << atlas.version_info().id << '\n'
              << "trust_root=" << bundle.value().id() << '\n'
              << "checkpoint=" << checkpoint.value().id << '\n'
              << "profile=" << profile.value().id << '\n'
              << "profile_approval_set=" << profile_approvals.value().id << '\n'
              << "command_sequence=" << sequence.value().id << '\n'
              << "request=" << request.value().id << '\n'
              << "session_approval_set=" << session_approvals.value().id << '\n'
              << "session=" << session.value().id() << '\n'
              << "session_evidence=unknown\n"
              << "session_authorizes_execution=false\n"
              << "command_authorization=" << authorization.value()->id << '\n'
              << "command_evidence=runtime_executable\n"
              << "command_open_ended=false\n";
    return 0;
}
