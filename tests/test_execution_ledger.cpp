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

template <std::size_t Offset> std::array<std::byte, rbfsafe::kEd25519SeedBytes> seed() {
    std::array<std::byte, rbfsafe::kEd25519SeedBytes> result{};
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<std::byte>(index + Offset);
    return result;
}

} // namespace

int main() {
    using namespace rbfsafe;

    const auto temporary = std::filesystem::temp_directory_path() /
                           ("rbfsafe-execution-ledger-" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    const auto robot = planar_robot();
    const SceneSnapshot scene({}, "execution-ledger-empty-v1");
    auto built = AtlasBuilder{}.build(robot, scene, {{0.0, 0.0}});
    CHECK(built);
    const auto& atlas = built.value().atlas;

    const auto safety_seed = seed<1>();
    const auto controls_seed = seed<33>();
    const auto governance_seed = seed<65>();
    const auto controller_seed = seed<97>();
    const auto monitor_seed = seed<129>();
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

    auto safety_key = make_service_public_key("ledger-review-safety", safety_pair.value().public_key, 1, 0,
                                              ServiceKeyState::Active, false, true, false);
    auto controls_key = make_service_public_key("ledger-review-controls", controls_pair.value().public_key, 1,
                                                0, ServiceKeyState::Active, false, true, false);
    auto governance_key =
        make_service_public_key("ledger-trust-governance", governance_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, false, false, true);
    CHECK(safety_key);
    CHECK(controls_key);
    CHECK(governance_key);
    ServiceTrustRotationPolicy rotation;
    rotation.minimum_signatures = 1;
    auto bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {controls_key.value(), governance_key.value(), safety_key.value()}, rotation);
    CHECK(bundle);
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
    profile_input.deployment_id = "ledger-cell-a";
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
    auto sequence = ExecutionCommandSequence::create(atlas, configurations, {0, 50'000, 100'000});
    CHECK(sequence);
    auto controller_endpoint = make_execution_endpoint_key(
        "ledger-controller", ExecutionEndpointRole::Controller, controller_pair.value().public_key);
    auto monitor_endpoint = make_execution_endpoint_key(
        "ledger-monitor", ExecutionEndpointRole::RuntimeMonitor, monitor_pair.value().public_key);
    CHECK(controller_endpoint);
    CHECK(monitor_endpoint);
    ExecutionSessionRequestInput request_input;
    request_input.session_nonce = digest('9');
    request_input.controller = controller_endpoint.value();
    request_input.runtime_monitor = monitor_endpoint.value();
    request_input.limits.maximum_start_delay_ns = 10'000;
    request_input.limits.maximum_duration_ns = 100'000;
    request_input.limits.maximum_commands = 3;
    auto request = ExecutionSessionRequest::create(reviewed.value(), sequence.value(), request_input);
    CHECK(request);
    auto session_safety = sign_execution_session_approval(request.value(), profile_safety.value(),
                                                          safety_pair.value().secret_key);
    auto session_controls = sign_execution_session_approval(request.value(), profile_controls.value(),
                                                            controls_pair.value().secret_key);
    CHECK(session_safety);
    CHECK(session_controls);
    auto session_approvals = assemble_execution_session_approvals(
        request.value(), reviewed.value(), {session_safety.value(), session_controls.value()});
    CHECK(session_approvals);
    auto controller_ack =
        sign_execution_controller_acknowledgement(request.value(), controller_pair.value().secret_key);
    CHECK(controller_ack);

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
    CHECK(observation);
    auto monitor_ack = sign_execution_monitor_acknowledgement(request.value(), observation.value(),
                                                              monitor_pair.value().secret_key);
    CHECK(monitor_ack);
    auto session = BoundedExecutionSession::create(
        request.value(), sequence.value(), session_approvals.value(), controller_ack.value(),
        monitor_ack.value(), reviewed.value(), bundle.value(), atlas);
    CHECK(session);

    const auto ledger_path = temporary / "ledger";
    auto ledger = ExecutionLedger::create(ledger_path, session.value());
    CHECK(ledger);
    CHECK(ledger.value().valid());
    CHECK(ledger.value().summary().status == ExecutionLedgerStatus::Open);
    CHECK(ledger.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!ledger.value().authorizes_execution());
    CHECK(!ExecutionLedger::create(ledger_path, session.value()));

    const auto root_id = ledger.value().current_record_id();
    auto command_zero = ledger.value().authorize_command(session.value(), reviewed.value(), history.value(),
                                                         checkpoint.value(), checkpoint.value().id, atlas, 0,
                                                         configurations[0], 1'000'000, root_id);
    CHECK(command_zero);
    CHECK(command_zero.value().valid());
    CHECK(command_zero.value().authorization);
    CHECK(command_zero.value().authorizes_execution());
    CHECK(command_zero.value().evidence() == EvidenceLevel::RuntimeExecutable);
    CHECK(command_zero.value().status == ExecutionLedgerStatus::AwaitingCompletion);

    auto stale_authorization = ledger.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        1, configurations[1], 1'050'001, root_id);
    CHECK(!stale_authorization);
    CHECK(stale_authorization.error().code == StatusCode::IdentityMismatch);
    auto duplicate_authorization = ledger.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'000'000, ledger.value().current_record_id());
    CHECK(!duplicate_authorization);
    CHECK(duplicate_authorization.error().code == StatusCode::IdentityMismatch);
    auto regressing_cancellation =
        ledger.value().cancel(session.value(), reviewed.value(), history.value(), atlas, 999'999,
                              "invalid time regression", ledger.value().current_record_id());
    CHECK(!regressing_cancellation);
    CHECK(regressing_cancellation.error().code == StatusCode::InvalidArgument);
    CHECK(ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(), atlas));

    ExecutionControllerCompletionInput completion_input;
    completion_input.outcome = ExecutionCompletionOutcome::Completed;
    completion_input.completed_monotonic_ns = 1'000'005;
    completion_input.result_digest = digest('e');
    auto completion_zero =
        sign_execution_controller_completion(session.value(), *command_zero.value().authorization,
                                             completion_input, controller_pair.value().secret_key);
    CHECK(completion_zero);
    CHECK(verify_execution_controller_completion(session.value(), *command_zero.value().authorization,
                                                 completion_zero.value()));
    CHECK(!sign_execution_controller_completion(session.value(), *command_zero.value().authorization,
                                                completion_input, monitor_pair.value().secret_key));
    const auto authorization_zero_id = ledger.value().current_record_id();
    auto recorded_zero =
        ledger.value().record_completion(session.value(), reviewed.value(), history.value(), atlas,
                                         completion_zero.value(), authorization_zero_id);
    CHECK(recorded_zero);
    CHECK(ledger.value().summary().status == ExecutionLedgerStatus::Open);

    auto repeated_completion =
        ledger.value().record_completion(session.value(), reviewed.value(), history.value(), atlas,
                                         completion_zero.value(), authorization_zero_id);
    CHECK(!repeated_completion);
    CHECK(repeated_completion.error().code == StatusCode::IdentityMismatch);

    auto command_one = ledger.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        1, configurations[1], 1'050'001, ledger.value().current_record_id());
    CHECK(command_one);
    CHECK(command_one.value().authorization);
    completion_input.completed_monotonic_ns = 1'050'005;
    completion_input.result_digest = digest('f');
    auto completion_one =
        sign_execution_controller_completion(session.value(), *command_one.value().authorization,
                                             completion_input, controller_pair.value().secret_key);
    CHECK(completion_one);
    CHECK(ledger.value().record_completion(session.value(), reviewed.value(), history.value(), atlas,
                                           completion_one.value(), ledger.value().current_record_id()));

    auto command_two = ledger.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        2, configurations[2], 1'100'000, ledger.value().current_record_id());
    CHECK(command_two);
    CHECK(command_two.value().authorization);
    completion_input.completed_monotonic_ns = 1'100'000;
    completion_input.result_digest = digest('a');
    auto completion_two =
        sign_execution_controller_completion(session.value(), *command_two.value().authorization,
                                             completion_input, controller_pair.value().secret_key);
    CHECK(completion_two);
    CHECK(ledger.value().record_completion(session.value(), reviewed.value(), history.value(), atlas,
                                           completion_two.value(), ledger.value().current_record_id()));
    CHECK(ledger.value().summary().status == ExecutionLedgerStatus::Completed);
    CHECK(ledger.value().summary().authorization_count == 3);
    CHECK(ledger.value().summary().completion_count == 3);
    CHECK(ledger.value().summary().valid());

    auto audit = ledger.value().audit(session.value(), reviewed.value(), history.value(), atlas);
    CHECK(audit);
    CHECK(audit.value().valid());
    CHECK(audit.value().status == ExecutionLedgerStatus::Completed);
    CHECK(audit.value().verified_records == 7);
    CHECK(audit.value().verified_checkpoints == 3);
    CHECK(audit.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!audit.value().authorizes_execution());
    auto reopened =
        ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(), atlas);
    CHECK(reopened);
    CHECK(reopened.value().current_record_id() == ledger.value().current_record_id());
    CHECK(reopened.value().summary().id == ledger.value().summary().id);

    const auto cancelled_path = temporary / "cancelled-ledger";
    auto cancelled = ExecutionLedger::create(cancelled_path, session.value());
    CHECK(cancelled);
    auto cancelled_authorization = cancelled.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'000'000, cancelled.value().current_record_id());
    CHECK(cancelled_authorization);
    CHECK(cancelled_authorization.value().authorization);
    CHECK(cancelled.value().cancel(session.value(), reviewed.value(), history.value(), atlas, 1'000'001,
                                   "operator cancelled before dispatch",
                                   cancelled.value().current_record_id()));
    CHECK(cancelled.value().summary().status == ExecutionLedgerStatus::Cancelled);
    CHECK(cancelled.value().summary().authorization_count == 1);
    CHECK(cancelled.value().summary().completion_count == 0);
    CHECK(!cancelled.value().summary().outstanding_command_index);
    CHECK(!cancelled.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'000'002, cancelled.value().current_record_id()));

    const auto expired_path = temporary / "expired-ledger";
    auto expired = ExecutionLedger::create(expired_path, session.value());
    CHECK(expired);
    CHECK(!expired.value().expire(session.value(), reviewed.value(), history.value(), atlas, 1'100'000,
                                  expired.value().current_record_id()));
    CHECK(expired.value().expire(session.value(), reviewed.value(), history.value(), atlas, 1'100'001,
                                 expired.value().current_record_id()));
    CHECK(expired.value().summary().status == ExecutionLedgerStatus::Expired);
    const auto automatic_expired_path = temporary / "automatic-expired-ledger";
    auto automatic_expired = ExecutionLedger::create(automatic_expired_path, session.value());
    CHECK(automatic_expired);
    auto expired_decision = automatic_expired.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'100'001, automatic_expired.value().current_record_id());
    CHECK(expired_decision);
    CHECK(!expired_decision.value().authorization);
    CHECK(expired_decision.value().status == ExecutionLedgerStatus::Expired);

    const auto revoked_path = temporary / "revoked-ledger";
    auto revoked = ExecutionLedger::create(revoked_path, session.value());
    CHECK(revoked);
    CHECK(!revoked.value().revoke_dependency(session.value(), reviewed.value(), history.value(), atlas,
                                             ExecutionDependencyKind::Scene, digest('0'), 1'000'001,
                                             "wrong scene", revoked.value().current_record_id()));
    auto revoked_authorization = revoked.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'000'000, revoked.value().current_record_id());
    CHECK(revoked_authorization);
    CHECK(revoked_authorization.value().authorization);
    CHECK(revoked.value().revoke_dependency(session.value(), reviewed.value(), history.value(), atlas,
                                            ExecutionDependencyKind::Scene,
                                            session.value().request().scene_digest, 1'000'001,
                                            "scene withdrawn", revoked.value().current_record_id()));
    CHECK(revoked.value().summary().status == ExecutionLedgerStatus::Revoked);
    CHECK(!revoked.value().summary().outstanding_command_index);
    CHECK(revoked.value().audit(session.value(), reviewed.value(), history.value(), atlas));
    const std::vector<std::pair<ExecutionDependencyKind, std::string>> exact_dependencies{
        {ExecutionDependencyKind::ReviewedProfile, session.value().request().reviewed_profile_id},
        {ExecutionDependencyKind::Atlas, session.value().request().atlas_id},
        {ExecutionDependencyKind::ControllerKey, session.value().request().controller.id},
        {ExecutionDependencyKind::RuntimeMonitorKey, session.value().request().runtime_monitor.id},
        {ExecutionDependencyKind::ReviewerKey,
         session.value().approval_set().approvals.front().signer_key_id},
        {ExecutionDependencyKind::TrustCheckpoint, session.value().request().trust_checkpoint_id},
    };
    for (std::size_t index = 0; index < exact_dependencies.size(); ++index) {
        auto exact_revocation = ExecutionLedger::create(
            temporary / ("exact-revocation-" + std::to_string(index)), session.value());
        CHECK(exact_revocation);
        CHECK(exact_revocation.value().revoke_dependency(
            session.value(), reviewed.value(), history.value(), atlas, exact_dependencies[index].first,
            exact_dependencies[index].second, 1'000'001, "exact dependency withdrawn",
            exact_revocation.value().current_record_id()));
        CHECK(exact_revocation.value().summary().status == ExecutionLedgerStatus::Revoked);
        CHECK(exact_revocation.value().audit(session.value(), reviewed.value(), history.value(), atlas));
    }

    const auto late_completion_path = temporary / "late-completion-ledger";
    auto late_completion = ExecutionLedger::create(late_completion_path, session.value());
    CHECK(late_completion);
    auto late_authorization = late_completion.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'000'000, late_completion.value().current_record_id());
    CHECK(late_authorization);
    completion_input.outcome = ExecutionCompletionOutcome::Completed;
    completion_input.completed_monotonic_ns = 1'100'001;
    completion_input.result_digest = digest('7');
    auto signed_late_completion =
        sign_execution_controller_completion(session.value(), *late_authorization.value().authorization,
                                             completion_input, controller_pair.value().secret_key);
    CHECK(signed_late_completion);
    CHECK(late_completion.value().record_completion(session.value(), reviewed.value(), history.value(), atlas,
                                                    signed_late_completion.value(),
                                                    late_completion.value().current_record_id()));
    CHECK(late_completion.value().summary().status == ExecutionLedgerStatus::Expired);

    const auto failed_path = temporary / "failed-ledger";
    auto failed = ExecutionLedger::create(failed_path, session.value());
    CHECK(failed);
    auto failed_authorization = failed.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'000'000, failed.value().current_record_id());
    CHECK(failed_authorization);
    completion_input.outcome = ExecutionCompletionOutcome::Rejected;
    completion_input.completed_monotonic_ns = 1'000'003;
    completion_input.result_digest = digest('8');
    auto rejected_completion =
        sign_execution_controller_completion(session.value(), *failed_authorization.value().authorization,
                                             completion_input, controller_pair.value().secret_key);
    CHECK(rejected_completion);
    CHECK(failed.value().record_completion(session.value(), reviewed.value(), history.value(), atlas,
                                           rejected_completion.value(), failed.value().current_record_id()));
    CHECK(failed.value().summary().valid());
    CHECK(failed.value().summary().status == ExecutionLedgerStatus::Failed);
    CHECK(failed.value().summary().next_command_index == 0);
    CHECK(failed.value().summary().completion_count == 1);

    const auto locked_path = temporary / "locked-ledger";
    auto locked = ExecutionLedger::create(locked_path, session.value());
    CHECK(locked);
    CHECK(std::filesystem::create_directory(locked_path / ".writer-lock"));
    auto lock_conflict = locked.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'000'000, locked.value().current_record_id());
    CHECK(!lock_conflict);
    CHECK(lock_conflict.error().code == StatusCode::ResourceLimit);
    CHECK(std::filesystem::remove(locked_path / ".writer-lock"));

    const auto checkpoint_revoked_path = temporary / "checkpoint-revoked-ledger";
    auto checkpoint_revoked = ExecutionLedger::create(checkpoint_revoked_path, session.value());
    CHECK(checkpoint_revoked);
    auto revoked_safety_key = safety_key.value();
    revoked_safety_key.state = ServiceKeyState::Revoked;
    auto successor = rotate_service_trust_bundle(
        bundle.value(), {controls_key.value(), governance_key.value(), revoked_safety_key});
    CHECK(successor);
    auto successor_authorization = authorize_service_trust_bundle_successor(
        bundle.value(), successor.value(), governance_key.value().service_id, governance_key.value().id,
        governance_pair.value().secret_key);
    CHECK(successor_authorization);
    CHECK(history.value().publish(successor.value(), successor_authorization.value(), bundle.value().id()));
    auto successor_checkpoint_signature =
        sign_service_trust_checkpoint(history.value(), governance_key.value().service_id,
                                      governance_key.value().id, governance_pair.value().secret_key);
    CHECK(successor_checkpoint_signature);
    auto successor_checkpoint =
        assemble_service_trust_checkpoint(history.value(), {successor_checkpoint_signature.value()});
    CHECK(successor_checkpoint);
    auto revoked_decision = checkpoint_revoked.value().authorize_command(
        session.value(), reviewed.value(), history.value(), successor_checkpoint.value(),
        successor_checkpoint.value().id, atlas, 0, configurations[0], 1'000'000,
        checkpoint_revoked.value().current_record_id());
    CHECK(revoked_decision);
    CHECK(revoked_decision.value().valid());
    CHECK(!revoked_decision.value().authorization);
    CHECK(!revoked_decision.value().authorizes_execution());
    CHECK(revoked_decision.value().status == ExecutionLedgerStatus::Revoked);
    CHECK(checkpoint_revoked.value().records().back().revocation);
    CHECK(checkpoint_revoked.value().records().back().revocation->kind ==
          ExecutionDependencyKind::ReviewerKey);
    CHECK(checkpoint_revoked.value().records().back().revocation->subject_id == safety_key.value().id);
    auto revoked_audit =
        checkpoint_revoked.value().audit(session.value(), reviewed.value(), history.value(), atlas);
    CHECK(revoked_audit);
    CHECK(revoked_audit.value().status == ExecutionLedgerStatus::Revoked);
    CHECK(revoked_audit.value().verified_checkpoints == 1);

    const auto rollback_path = temporary / "checkpoint-rollback-ledger";
    auto rollback = ExecutionLedger::create(rollback_path, session.value());
    CHECK(rollback);
    auto old_checkpoint_decision = rollback.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'000'000, rollback.value().current_record_id());
    CHECK(!old_checkpoint_decision);
    CHECK(old_checkpoint_decision.error().code == StatusCode::IdentityMismatch);

    ExecutionLedgerLoadOptions one_record;
    one_record.maximum_records = 1;
    auto limited = ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(),
                                         atlas, one_record);
    CHECK(!limited);
    CHECK(limited.error().code == StatusCode::ResourceLimit);
    ExecutionLedgerLoadOptions tiny_manifest;
    tiny_manifest.maximum_manifest_bytes = 1;
    auto manifest_limited = ExecutionLedger::open(ledger_path, session.value(), reviewed.value(),
                                                  history.value(), atlas, tiny_manifest);
    CHECK(!manifest_limited);
    CHECK(manifest_limited.error().code == StatusCode::ResourceLimit);
    ExecutionLedgerLoadOptions tiny_record;
    tiny_record.maximum_record_bytes = 1;
    auto record_limited = ExecutionLedger::open(ledger_path, session.value(), reviewed.value(),
                                                history.value(), atlas, tiny_record);
    CHECK(!record_limited);
    CHECK(record_limited.error().code == StatusCode::ResourceLimit);
    ExecutionLedgerLoadOptions two_checkpoint_signatures;
    two_checkpoint_signatures.maximum_total_checkpoint_signatures = 2;
    auto signature_limited = ExecutionLedger::open(ledger_path, session.value(), reviewed.value(),
                                                   history.value(), atlas, two_checkpoint_signatures);
    CHECK(!signature_limited);
    CHECK(signature_limited.error().code == StatusCode::ResourceLimit);
    const SceneSnapshot other_scene({}, "execution-ledger-other-scene-v1");
    auto other_built = AtlasBuilder{}.build(robot, other_scene, {{0.0, 0.0}});
    CHECK(other_built);
    auto wrong_atlas = ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(),
                                             other_built.value().atlas);
    CHECK(!wrong_atlas);
    CHECK(wrong_atlas.error().code == StatusCode::IdentityMismatch);
    ExecutionLedgerLoadOptions cancelled_options;
    cancelled_options.cancellation.cancel();
    auto cancelled_open = ExecutionLedger::open(ledger_path, session.value(), reviewed.value(),
                                                history.value(), atlas, cancelled_options);
    CHECK(!cancelled_open);
    CHECK(cancelled_open.error().code == StatusCode::Cancelled);

    const auto abandoned_temporary = ledger_path / "records" / ".tmp-abandoned";
    write_text(abandoned_temporary, "incomplete");
    CHECK(ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(), atlas));
    CHECK(std::filesystem::remove(abandoned_temporary));
    const auto unexpected_entry = ledger_path / "records" / "unexpected.txt";
    write_text(unexpected_entry, "unexpected");
    auto unexpected =
        ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(), atlas);
    CHECK(!unexpected);
    CHECK(unexpected.error().code == StatusCode::CorruptData);
    CHECK(std::filesystem::remove(unexpected_entry));

    const auto ledger_link = temporary / "ledger-link";
    std::error_code ledger_link_error;
    std::filesystem::create_directory_symlink(ledger_path, ledger_link, ledger_link_error);
    if (!ledger_link_error) {
        auto indirect =
            ExecutionLedger::open(ledger_link, session.value(), reviewed.value(), history.value(), atlas);
        CHECK(!indirect);
        CHECK(indirect.error().code == StatusCode::CorruptData);
    }

    const auto manifest_path = ledger_path / "manifest.json";
    const auto manifest = read_text(manifest_path);
    auto unknown_schema = manifest;
    const auto schema_position = unknown_schema.find("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    unknown_schema.replace(schema_position, std::string("\"schema\": 1").size(), "\"schema\": 99");
    write_text(manifest_path, unknown_schema);
    auto incompatible =
        ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(), atlas);
    CHECK(!incompatible);
    CHECK(incompatible.error().code == StatusCode::IncompatibleFormat);
    write_text(manifest_path, manifest);

    const auto final_record_path =
        ledger_path / "records" / ("00000000000000000006-" + ledger.value().records().back().id + ".json");
    const auto final_record = read_text(final_record_path);
    CHECK(!final_record.empty());
    auto tampered_record = final_record;
    auto authentication_position = tampered_record.find("\"authentication_tag\": \"");
    CHECK(authentication_position != std::string::npos);
    authentication_position += std::string("\"authentication_tag\": \"").size();
    tampered_record[authentication_position] = tampered_record[authentication_position] == '0' ? '1' : '0';
    write_text(final_record_path, tampered_record);
    CHECK(!ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(), atlas));
    write_text(final_record_path, final_record);
    write_text(final_record_path, final_record.substr(0, final_record.size() / 2));
    auto truncated =
        ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(), atlas);
    CHECK(!truncated);
    CHECK(truncated.error().code == StatusCode::CorruptData);
    write_text(final_record_path, final_record);
    CHECK(ExecutionLedger::open(ledger_path, session.value(), reviewed.value(), history.value(), atlas));

    const auto fixed_session_root =
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "bounded_execution_session_schema1";
    auto fixed_checkpoint = ServiceTrustCheckpoint::load(fixed_session_root / "checkpoint.json");
    CHECK(fixed_checkpoint);
    auto fixed_history = ServiceTrustHistory::open(fixed_session_root / "trust-history",
                                                   fixed_checkpoint.value().root_bundle_id,
                                                   fixed_checkpoint.value(), fixed_checkpoint.value().id);
    CHECK(fixed_history);
    auto fixed_reviewed =
        ReviewedDeploymentProfile::load(fixed_session_root / "profile.json", fixed_history.value(),
                                        fixed_checkpoint.value(), fixed_checkpoint.value().id);
    CHECK(fixed_reviewed);
    auto fixed_atlas = SafeAtlas::load(fixed_session_root / "atlas");
    CHECK(fixed_atlas);
    auto fixed_session = BoundedExecutionSession::load(
        fixed_session_root / "session.json", fixed_reviewed.value(), fixed_history.value(),
        fixed_checkpoint.value(), fixed_checkpoint.value().id, fixed_atlas.value());
    CHECK(fixed_session);
    auto fixed_ledger = ExecutionLedger::open(
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "execution_ledger_schema1", fixed_session.value(),
        fixed_reviewed.value(), fixed_history.value(), fixed_atlas.value());
    CHECK(fixed_ledger);
    CHECK(fixed_ledger.value().id() == "f19c91b8f7788471691ac4d6a09861ee08188c9993e21861ad13e25e9cf99aa5");
    CHECK(fixed_ledger.value().current_record_id() ==
          "ef269da4406e26a5aa7621af1ca3095392fe1ca84b2327b86804024ee2a0437b");
    auto fixed_audit = fixed_ledger.value().audit(fixed_session.value(), fixed_reviewed.value(),
                                                  fixed_history.value(), fixed_atlas.value());
    CHECK(fixed_audit);
    CHECK(fixed_audit.value().status == ExecutionLedgerStatus::Completed);
    CHECK(fixed_audit.value().verified_records == 7);
    CHECK(fixed_audit.value().verified_checkpoints == 3);
    CHECK(fixed_audit.value().authorization_count == 3);
    CHECK(fixed_audit.value().completion_count == 3);

    std::filesystem::remove_all(temporary);
    return EXIT_SUCCESS;
}
