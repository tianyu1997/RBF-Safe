#include "test_support.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string digest(char value) { return std::string(64, value); }

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

int main() {
    using namespace rbfsafe;

    const auto robot = planar_robot();
    const SceneSnapshot scene({}, "execution-empty-v1");
    auto built = AtlasBuilder{}.build(robot, scene, {{0.0, 0.0}});
    CHECK(built);
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
    CHECK(safety_pair);
    CHECK(controls_pair);
    CHECK(governance_pair);
    CHECK(controller_pair);
    CHECK(monitor_pair);

    auto safety_key = make_service_public_key("execution-review-safety", safety_pair.value().public_key, 1, 0,
                                              ServiceKeyState::Active, false, true, false);
    auto controls_key = make_service_public_key("execution-review-controls", controls_pair.value().public_key,
                                                1, 0, ServiceKeyState::Active, false, true, false);
    auto governance_key =
        make_service_public_key("execution-trust-governance", governance_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, false, false, true);
    CHECK(safety_key);
    CHECK(controls_key);
    CHECK(governance_key);
    ServiceTrustRotationPolicy rotation;
    rotation.minimum_signatures = 1;
    auto bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {controls_key.value(), governance_key.value(), safety_key.value()}, rotation);
    CHECK(bundle);

    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("rbfsafe-execution-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto history =
        ServiceTrustHistory::create(temporary / "trust-history", bundle.value(), bundle.value().id());
    CHECK(history);
    auto checkpoint_signature =
        sign_service_trust_checkpoint(history.value(), governance_key.value().service_id,
                                      governance_key.value().id, governance_pair.value().secret_key);
    CHECK(checkpoint_signature);
    auto checkpoint = assemble_service_trust_checkpoint(history.value(), {checkpoint_signature.value()});
    CHECK(checkpoint);

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
    CHECK(profile);
    auto profile_safety = sign_deployment_profile_approval(
        profile.value(), safety_key.value().service_id, safety_key.value().id, DeploymentReviewRole::Safety,
        safety_pair.value().secret_key);
    auto profile_controls = sign_deployment_profile_approval(
        profile.value(), controls_key.value().service_id, controls_key.value().id,
        DeploymentReviewRole::Controls, controls_pair.value().secret_key);
    CHECK(profile_safety);
    CHECK(profile_controls);
    auto profile_approvals = assemble_deployment_profile_approvals(
        profile.value(), {profile_controls.value(), profile_safety.value()});
    CHECK(profile_approvals);
    auto reviewed =
        ReviewedDeploymentProfile::create(profile.value(), profile_approvals.value(), history.value(),
                                          checkpoint.value(), checkpoint.value().id);
    CHECK(reviewed);

    const std::vector<Configuration> configurations{{-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}};
    const std::vector<std::uint64_t> offsets{0, 50'000, 100'000};
    auto sequence = ExecutionCommandSequence::create(atlas, configurations, offsets);
    CHECK(sequence);
    CHECK(sequence.value().valid());
    CHECK(sequence.value().verify_compatible(atlas));
    auto repeated_sequence = ExecutionCommandSequence::create(atlas, configurations, offsets);
    CHECK(repeated_sequence);
    CHECK(repeated_sequence.value().id == sequence.value().id);
    CHECK(!ExecutionCommandSequence::create(atlas, configurations, {0, 50'000, 50'000}));
    CHECK(!ExecutionCommandSequence::create(atlas, configurations, {1, 50'000, 100'000}));

    auto controller_endpoint = make_execution_endpoint_key(
        "execution-controller", ExecutionEndpointRole::Controller, controller_pair.value().public_key);
    auto monitor_endpoint = make_execution_endpoint_key(
        "execution-monitor", ExecutionEndpointRole::RuntimeMonitor, monitor_pair.value().public_key);
    CHECK(controller_endpoint);
    CHECK(monitor_endpoint);
    CHECK(valid_execution_endpoint_key(controller_endpoint.value()));
    CHECK(valid_execution_endpoint_key(monitor_endpoint.value()));

    ExecutionSessionRequestInput request_input;
    request_input.session_nonce = digest('9');
    request_input.controller = controller_endpoint.value();
    request_input.runtime_monitor = monitor_endpoint.value();
    request_input.limits.maximum_start_delay_ns = 10'000;
    request_input.limits.maximum_duration_ns = 100'000;
    request_input.limits.maximum_commands = 3;
    auto request = ExecutionSessionRequest::create(reviewed.value(), sequence.value(), request_input);
    CHECK(request);
    CHECK(request.value().valid());
    CHECK(request.value().command_count == 3);
    auto invalid_limits = request_input;
    invalid_limits.limits.maximum_start_delay_ns = 100'001;
    CHECK(!ExecutionSessionRequest::create(reviewed.value(), sequence.value(), invalid_limits));
    auto swapped_endpoints = request_input;
    swapped_endpoints.controller = monitor_endpoint.value();
    CHECK(!ExecutionSessionRequest::create(reviewed.value(), sequence.value(), swapped_endpoints));

    auto session_safety = sign_execution_session_approval(request.value(), profile_safety.value(),
                                                          safety_pair.value().secret_key);
    auto session_controls = sign_execution_session_approval(request.value(), profile_controls.value(),
                                                            controls_pair.value().secret_key);
    CHECK(session_safety);
    CHECK(session_controls);
    auto session_approvals = assemble_execution_session_approvals(
        request.value(), reviewed.value(), {session_safety.value(), session_controls.value()});
    CHECK(session_approvals);
    CHECK(verify_execution_session_approvals(request.value(), reviewed.value(), session_approvals.value(),
                                             bundle.value()));
    CHECK(!assemble_execution_session_approvals(request.value(), reviewed.value(), {session_safety.value()}));
    CHECK(!sign_execution_session_approval(request.value(), profile_safety.value(),
                                           controls_pair.value().secret_key));

    auto controller_ack =
        sign_execution_controller_acknowledgement(request.value(), controller_pair.value().secret_key);
    CHECK(controller_ack);
    CHECK(verify_execution_controller_acknowledgement(request.value(), controller_ack.value()));
    CHECK(!sign_execution_controller_acknowledgement(request.value(), monitor_pair.value().secret_key));

    DeploymentRuntimeSnapshot runtime;
    runtime.deployment_id = profile.value().deployment_id;
    runtime.robot_digest = atlas.robot_digest();
    runtime.controller_digest = profile.value().controller_digest;
    runtime.platform_digest = profile.value().platform_digest;
    runtime.runtime_digest = profile.value().runtime_digest;
    runtime.observation_age_ns = 10'000;
    runtime.command_latency_ns = 20'000;
    runtime.control_period_ns = 50'000;
    runtime.consecutive_missed_cycles = 0;
    runtime.runtime_monitor_active = true;
    runtime.fail_closed_transport_active = true;
    runtime.authenticated_artifacts = true;

    ExecutionRuntimeObservationInput observation_input;
    observation_input.runtime = runtime;
    observation_input.observation_sequence = 7;
    observation_input.observed_monotonic_ns = 1'000'000;
    observation_input.monitor_state = ExecutionMonitorState::ArmedCertifiedSequence;
    auto observation = ExecutionRuntimeObservation::create(request.value(), observation_input);
    CHECK(observation);
    auto monitor_ack = sign_execution_monitor_acknowledgement(request.value(), observation.value(),
                                                              monitor_pair.value().secret_key);
    CHECK(monitor_ack);
    CHECK(verify_execution_monitor_acknowledgement(request.value(), monitor_ack.value()));
    CHECK(!sign_execution_monitor_acknowledgement(request.value(), observation.value(),
                                                  controller_pair.value().secret_key));

    auto session = BoundedExecutionSession::create(
        request.value(), sequence.value(), session_approvals.value(), controller_ack.value(),
        monitor_ack.value(), reviewed.value(), bundle.value(), atlas);
    CHECK(session);
    CHECK(session.value().valid());
    CHECK(session.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!session.value().authorizes_execution());
    CHECK(session.value().valid_from_monotonic_ns() == 1'000'000);
    CHECK(session.value().start_deadline_monotonic_ns() == 1'010'000);
    CHECK(session.value().valid_through_monotonic_ns() == 1'100'000);

    auto command_zero = session.value().authorize_command(0, configurations[0], 1'000'000);
    CHECK(command_zero);
    CHECK(command_zero.value());
    CHECK(command_zero.value()->valid());
    CHECK(command_zero.value()->evidence == EvidenceLevel::RuntimeExecutable);
    CHECK(!command_zero.value()->open_ended());
    auto command_one = session.value().authorize_command(1, configurations[1], 1'050'001);
    CHECK(command_one);
    CHECK(command_one.value());
    auto repeated_command_one = session.value().authorize_command(1, configurations[1], 1'050'001);
    CHECK(repeated_command_one);
    CHECK(repeated_command_one.value());
    CHECK(repeated_command_one.value()->id == command_one.value()->id);
    CHECK(!session.value().authorize_command(1, configurations[1], 1'049'999).value());
    CHECK(!session.value().authorize_command(1, configurations[0], 1'050'001).value());
    CHECK(!session.value().authorize_command(0, configurations[0], 1'010'001).value());
    CHECK(!session.value().authorize_command(99, configurations[0], 1'000'000).value());
    const Configuration one_dimension{0.0};
    auto wrong_dimension = session.value().authorize_command(0, one_dimension, 1'000'000);
    CHECK(!wrong_dimension);
    CHECK(wrong_dimension.error().code == StatusCode::InvalidArgument);

    auto disarmed_input = observation_input;
    disarmed_input.monitor_state = ExecutionMonitorState::Disarmed;
    auto disarmed_observation = ExecutionRuntimeObservation::create(request.value(), disarmed_input);
    CHECK(disarmed_observation);
    auto disarmed_ack = sign_execution_monitor_acknowledgement(request.value(), disarmed_observation.value(),
                                                               monitor_pair.value().secret_key);
    CHECK(disarmed_ack);
    auto disarmed_session = BoundedExecutionSession::create(
        request.value(), sequence.value(), session_approvals.value(), controller_ack.value(),
        disarmed_ack.value(), reviewed.value(), bundle.value(), atlas);
    CHECK(!disarmed_session);
    CHECK(disarmed_session.error().code == StatusCode::IdentityMismatch);

    const auto session_path = temporary / "session.json";
    CHECK(session.value().save(session_path));
    CHECK(!session.value().save(session_path));
    SaveOptions overwrite;
    overwrite.overwrite = true;
    CHECK(session.value().save(session_path, overwrite));
    auto loaded = BoundedExecutionSession::load(session_path, reviewed.value(), history.value(),
                                                checkpoint.value(), checkpoint.value().id, atlas);
    CHECK(loaded);
    CHECK(loaded.value().id() == session.value().id());
    CHECK(loaded.value().authorize_command(2, configurations[2], 1'100'000).value());

    BoundedExecutionSessionLoadOptions one_command;
    one_command.maximum_commands = 2;
    auto command_limited =
        BoundedExecutionSession::load(session_path, reviewed.value(), history.value(), checkpoint.value(),
                                      checkpoint.value().id, atlas, one_command);
    CHECK(!command_limited);
    CHECK(command_limited.error().code == StatusCode::ResourceLimit);
    BoundedExecutionSessionLoadOptions one_dimension_limit;
    one_dimension_limit.maximum_dimension = 1;
    auto dimension_limited =
        BoundedExecutionSession::load(session_path, reviewed.value(), history.value(), checkpoint.value(),
                                      checkpoint.value().id, atlas, one_dimension_limit);
    CHECK(!dimension_limited);
    CHECK(dimension_limited.error().code == StatusCode::ResourceLimit);
    BoundedExecutionSessionLoadOptions one_byte;
    one_byte.maximum_payload_bytes = 1;
    auto byte_limited =
        BoundedExecutionSession::load(session_path, reviewed.value(), history.value(), checkpoint.value(),
                                      checkpoint.value().id, atlas, one_byte);
    CHECK(!byte_limited);
    CHECK(byte_limited.error().code == StatusCode::ResourceLimit);

    const auto saved = read_text(session_path);
    auto unknown_schema = saved;
    const auto schema_position = unknown_schema.find("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    unknown_schema.replace(schema_position, std::string("\"schema\": 1").size(), "\"schema\": 99");
    write_text(session_path, unknown_schema);
    auto incompatible = BoundedExecutionSession::load(session_path, reviewed.value(), history.value(),
                                                      checkpoint.value(), checkpoint.value().id, atlas);
    CHECK(!incompatible);
    CHECK(incompatible.error().code == StatusCode::IncompatibleFormat);

    auto corrupt = saved;
    auto tag_position = corrupt.find("\"authentication_tag\": \"");
    CHECK(tag_position != std::string::npos);
    tag_position += std::string("\"authentication_tag\": \"").size();
    corrupt[tag_position] = corrupt[tag_position] == '0' ? '1' : '0';
    write_text(session_path, corrupt);
    auto corrupted = BoundedExecutionSession::load(session_path, reviewed.value(), history.value(),
                                                   checkpoint.value(), checkpoint.value().id, atlas);
    CHECK(!corrupted);
    CHECK(corrupted.error().code == StatusCode::CorruptData);
    write_text(session_path, saved.substr(0, saved.size() / 2));
    auto truncated = BoundedExecutionSession::load(session_path, reviewed.value(), history.value(),
                                                   checkpoint.value(), checkpoint.value().id, atlas);
    CHECK(!truncated);
    CHECK(truncated.error().code == StatusCode::CorruptData);
    write_text(session_path, saved);

    const auto fixed_root =
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "bounded_execution_session_schema1";
    auto fixed_checkpoint = ServiceTrustCheckpoint::load(fixed_root / "checkpoint.json");
    CHECK(fixed_checkpoint);
    CHECK(fixed_checkpoint.value().id == "3ebcb9e144577ba8b828f8b728c43b90f1b7412d09212cfec40e69fa1d3f9e01");
    auto fixed_history =
        ServiceTrustHistory::open(fixed_root / "trust-history", fixed_checkpoint.value().root_bundle_id,
                                  fixed_checkpoint.value(), fixed_checkpoint.value().id);
    CHECK(fixed_history);
    auto fixed_reviewed =
        ReviewedDeploymentProfile::load(fixed_root / "profile.json", fixed_history.value(),
                                        fixed_checkpoint.value(), fixed_checkpoint.value().id);
    CHECK(fixed_reviewed);
    CHECK(fixed_reviewed.value().profile().id ==
          "7981b3bbd373255d5f5bd0bcfac2139ac8c55a8d841dedc15e32bbb53bf310d2");
    auto fixed_atlas = SafeAtlas::load(fixed_root / "atlas");
    CHECK(fixed_atlas);
    CHECK(fixed_atlas.value().version_info().id ==
          "900f017e78acb91948f908edd9fbec5567280e4da4df8284f307b19e46fac862");
    auto fixed_session = BoundedExecutionSession::load(fixed_root / "session.json", fixed_reviewed.value(),
                                                       fixed_history.value(), fixed_checkpoint.value(),
                                                       fixed_checkpoint.value().id, fixed_atlas.value());
    CHECK(fixed_session);
    CHECK(fixed_session.value().id() == "62647c557ba9dad576c9ce3035ffe496fe0c224f91432d5b290586c09e6be2df");
    CHECK(!fixed_session.value().authorizes_execution());
    CHECK(fixed_session.value()
              .authorize_command(1, fixed_session.value().command_sequence().commands[1].configuration,
                                 1'050'001)
              .value());

    const auto directory_destination = temporary / "directory-destination";
    std::filesystem::create_directory(directory_destination);
    CHECK(!session.value().save(directory_destination, overwrite));
    const auto symlink_destination = temporary / "session-link.json";
    std::error_code symlink_error;
    std::filesystem::create_symlink(session_path, symlink_destination, symlink_error);
    if (!symlink_error)
        CHECK(!session.value().save(symlink_destination, overwrite));

    std::filesystem::remove_all(temporary);
    return EXIT_SUCCESS;
}
