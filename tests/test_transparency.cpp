#include "test_support.h"

#include "internal/transparency.h"

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

    {
        constexpr char kHexDigits[] = "0123456789abcdef";
        std::array<std::string, 64> frontier{};
        std::vector<std::string> leaf_ids;
        leaf_ids.reserve(257);
        for (std::size_t index = 0; index < 257; ++index) {
            const auto leaf_id = digest(kHexDigits[index % 16]);
            const auto root = internal::transparency_append_merkle_leaf(
                frontier, leaf_id, static_cast<std::uint64_t>(leaf_ids.size()));
            leaf_ids.push_back(leaf_id);
            CHECK(root == internal::transparency_merkle_root(leaf_ids));
            CHECK(root == internal::transparency_merkle_frontier_root(frontier));
        }

        std::array<std::string, 64> subtree_frontier{};
        for (std::uint64_t index = 0; index < 3; ++index) {
            CHECK(!internal::transparency_append_merkle_leaf(subtree_frontier,
                                                             leaf_ids[static_cast<std::size_t>(index)], index)
                       .empty());
        }
        const std::vector<std::string> fourth_leaf{leaf_ids[3]};
        const std::vector<std::string> middle_block(leaf_ids.begin() + 4, leaf_ids.begin() + 8);
        const std::vector<std::string> final_block(leaf_ids.begin() + 8, leaf_ids.begin() + 16);
        CHECK(!internal::transparency_append_merkle_subtree(
                   subtree_frontier, internal::transparency_merkle_root(fourth_leaf), 0, 3)
                   .empty());
        CHECK(!internal::transparency_append_merkle_subtree(
                   subtree_frontier, internal::transparency_merkle_root(middle_block), 2, 4)
                   .empty());
        const auto block_root = internal::transparency_append_merkle_subtree(
            subtree_frontier, internal::transparency_merkle_root(final_block), 3, 8);
        const std::vector<std::string> first_sixteen(leaf_ids.begin(), leaf_ids.begin() + 16);
        CHECK(block_root == internal::transparency_merkle_root(first_sixteen));
        CHECK(internal::transparency_append_merkle_subtree(
                  subtree_frontier, internal::transparency_merkle_root(fourth_leaf), 2, 17)
                  .empty());
    }

    const auto temporary = std::filesystem::temp_directory_path() /
                           ("rbfsafe-transparency-" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    const auto robot = planar_robot();
    const SceneSnapshot scene({}, "transparency-empty-v1");
    auto built = AtlasBuilder{}.build(robot, scene, {{0.0, 0.0}});
    CHECK(built);
    const auto& atlas = built.value().atlas;

    const auto safety_seed = seed<1>();
    const auto controls_seed = seed<33>();
    const auto governance_seed = seed<65>();
    const auto controller_seed = seed<97>();
    const auto monitor_seed = seed<129>();
    const auto observer_one_seed = seed<161>();
    const auto observer_two_seed = seed<193>();
    const auto transparency_seed = seed<225>();
    auto safety_pair = ed25519_key_pair_from_seed(safety_seed);
    auto controls_pair = ed25519_key_pair_from_seed(controls_seed);
    auto governance_pair = ed25519_key_pair_from_seed(governance_seed);
    auto controller_pair = ed25519_key_pair_from_seed(controller_seed);
    auto monitor_pair = ed25519_key_pair_from_seed(monitor_seed);
    auto observer_one_pair = ed25519_key_pair_from_seed(observer_one_seed);
    auto observer_two_pair = ed25519_key_pair_from_seed(observer_two_seed);
    auto transparency_pair = ed25519_key_pair_from_seed(transparency_seed);
    CHECK(safety_pair);
    CHECK(controls_pair);
    CHECK(governance_pair);
    CHECK(controller_pair);
    CHECK(monitor_pair);
    CHECK(observer_one_pair);
    CHECK(observer_two_pair);
    CHECK(transparency_pair);

    auto safety_key = make_service_public_key("transparency-review-safety", safety_pair.value().public_key, 1,
                                              0, ServiceKeyState::Active, false, true, false);
    auto controls_key =
        make_service_public_key("transparency-review-controls", controls_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, false, true, false);
    auto governance_key =
        make_service_public_key("transparency-governance", governance_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, false, false, true);
    auto observer_one_key = make_service_public_key("observer-one", observer_one_pair.value().public_key, 1,
                                                    0, ServiceKeyState::Active, false, true, false);
    auto observer_two_key = make_service_public_key("observer-two", observer_two_pair.value().public_key, 1,
                                                    0, ServiceKeyState::Active, false, true, false);
    auto controller_service_key =
        make_service_public_key("transparency-controller", controller_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, false, true, false);
    auto transparency_key = make_service_public_key("transparency-log", transparency_pair.value().public_key,
                                                    1, 0, ServiceKeyState::Active, false, true, false);
    CHECK(safety_key);
    CHECK(controls_key);
    CHECK(governance_key);
    CHECK(observer_one_key);
    CHECK(observer_two_key);
    CHECK(controller_service_key);
    CHECK(transparency_key);

    ServiceTrustRotationPolicy rotation;
    rotation.minimum_signatures = 1;
    auto bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "",
        {controller_service_key.value(), controls_key.value(), governance_key.value(),
         observer_one_key.value(), observer_two_key.value(), safety_key.value()},
        rotation);
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
    profile_input.deployment_id = "transparency-cell-a";
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

    auto anchor = DeploymentTransparencyAnchor::create(reviewed.value(), history.value(), checkpoint.value(),
                                                       checkpoint.value().id);
    CHECK(anchor);
    CHECK(anchor.value().valid());
    CHECK(anchor.value().reviewed_profile_id == profile.value().id);
    CHECK(anchor.value().trust_head_record_id == history.value().records().back().id);
    CHECK(anchor.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!anchor.value().authorizes_execution());
    CHECK(!DeploymentTransparencyAnchor::create(reviewed.value(), history.value(), checkpoint.value(),
                                                digest('0')));

    const std::vector<Configuration> configurations{{-1.0, -1.0}, {0.0, 0.0}};
    auto sequence = ExecutionCommandSequence::create(atlas, configurations, {0, 50'000});
    CHECK(sequence);
    auto controller_endpoint = make_execution_endpoint_key(
        "transparency-controller", ExecutionEndpointRole::Controller, controller_pair.value().public_key);
    auto monitor_endpoint = make_execution_endpoint_key(
        "transparency-monitor", ExecutionEndpointRole::RuntimeMonitor, monitor_pair.value().public_key);
    CHECK(controller_endpoint);
    CHECK(monitor_endpoint);
    ExecutionSessionRequestInput request_input;
    request_input.session_nonce = digest('9');
    request_input.controller = controller_endpoint.value();
    request_input.runtime_monitor = monitor_endpoint.value();
    request_input.limits.maximum_start_delay_ns = 10'000;
    request_input.limits.maximum_duration_ns = 100'000;
    request_input.limits.maximum_commands = 2;
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
    ExecutionRuntimeObservationInput session_observation_input;
    session_observation_input.runtime = runtime;
    session_observation_input.observation_sequence = 7;
    session_observation_input.observed_monotonic_ns = 1'000'000;
    session_observation_input.monitor_state = ExecutionMonitorState::ArmedCertifiedSequence;
    auto session_observation =
        ExecutionRuntimeObservation::create(request.value(), session_observation_input);
    CHECK(session_observation);
    auto monitor_ack = sign_execution_monitor_acknowledgement(request.value(), session_observation.value(),
                                                              monitor_pair.value().secret_key);
    CHECK(monitor_ack);
    auto session = BoundedExecutionSession::create(
        request.value(), sequence.value(), session_approvals.value(), controller_ack.value(),
        monitor_ack.value(), reviewed.value(), bundle.value(), atlas);
    CHECK(session);

    auto ledger = ExecutionLedger::create(temporary / "ledger", session.value());
    CHECK(ledger);
    auto command = ledger.value().authorize_command(
        session.value(), reviewed.value(), history.value(), checkpoint.value(), checkpoint.value().id, atlas,
        0, configurations[0], 1'000'000, ledger.value().current_record_id());
    CHECK(command);
    CHECK(command.value().authorization);

    IndependentRuntimeObservationInput observation_input;
    observation_input.runtime = runtime;
    observation_input.observation_sequence = 8;
    observation_input.observed_monotonic_ns = 1'000'001;
    observation_input.monitor_state = ExecutionMonitorState::ArmedCertifiedSequence;
    observation_input.configuration_digest = digest('4');
    auto observation = IndependentRuntimeObservation::create(
        session.value(), ledger.value(), *command.value().authorization, observation_input);
    CHECK(observation);
    CHECK(observation.value().valid());
    CHECK(observation.value().ledger_record_id == ledger.value().current_record_id());
    CHECK(observation.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!observation.value().authorizes_execution());

    auto early_input = observation_input;
    early_input.observed_monotonic_ns = 999'999;
    CHECK(!IndependentRuntimeObservation::create(session.value(), ledger.value(),
                                                 *command.value().authorization, early_input));

    auto observer_one =
        sign_runtime_observation(observation.value(), observer_one_key.value().service_id,
                                 observer_one_key.value().id, observer_one_pair.value().secret_key);
    auto observer_two =
        sign_runtime_observation(observation.value(), observer_two_key.value().service_id,
                                 observer_two_key.value().id, observer_two_pair.value().secret_key);
    CHECK(observer_one);
    CHECK(observer_two);
    CHECK(verify_runtime_observation_attestation(observation.value(), observer_one.value(), bundle.value()));
    CHECK(!verify_runtime_observation_attestation(
        observation.value(), observer_one.value(),
        ServiceTrustBundle::create(1, "", {safety_key.value()}).value()));

    RuntimeObservationPolicy observation_policy;
    observation_policy.minimum_attestations = 2;
    auto observation_set = assemble_runtime_observation_attestations(
        session.value(), observation.value(), observation_policy,
        {observer_two.value(), observer_one.value()}, bundle.value());
    CHECK(observation_set);
    CHECK(observation_set.value().valid());
    CHECK(observation_set.value().attestations.front().source_service_id == "observer-one");
    CHECK(verify_runtime_observation_attestations(session.value(), observation_set.value(), bundle.value()));
    CHECK(observation_set.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!observation_set.value().authorizes_execution());

    auto controller_attestation =
        sign_runtime_observation(observation.value(), controller_service_key.value().service_id,
                                 controller_service_key.value().id, controller_pair.value().secret_key);
    CHECK(controller_attestation);
    CHECK(!assemble_runtime_observation_attestations(session.value(), observation.value(), observation_policy,
                                                     {observer_one.value(), controller_attestation.value()},
                                                     bundle.value()));
    auto duplicate_policy = observation_policy;
    duplicate_policy.require_distinct_services = false;
    CHECK(!assemble_runtime_observation_attestations(session.value(), observation.value(), duplicate_policy,
                                                     {observer_one.value(), observer_one.value()},
                                                     bundle.value()));

    auto log_identity =
        TransparencyLogIdentity::create("rbfsafe-public-deployments-v1", transparency_key.value().service_id,
                                        transparency_key.value().id, transparency_pair.value().public_key);
    CHECK(log_identity);
    CHECK(log_identity.value().valid());
    CHECK(!TransparencyLogIdentity::create("", transparency_key.value().service_id,
                                           transparency_key.value().id,
                                           transparency_pair.value().public_key));
    auto empty_gossip_audit = audit_transparency_checkpoint_gossip(
        log_identity.value(), std::vector<TransparencyCheckpointGossip>{}, bundle.value());
    CHECK(empty_gossip_audit);
    CHECK(empty_gossip_audit.value().status == TransparencyGossipStatus::Consistent);
    CHECK(empty_gossip_audit.value().authenticated_gossip_count == 0);
    CHECK(empty_gossip_audit.value().unique_checkpoint_count == 0);

    const auto log_path = temporary / "transparency-log";
    auto log = TransparencyLog::create(log_path, log_identity.value());
    CHECK(log);
    CHECK(log.value().valid());
    CHECK(log.value().records().empty());
    CHECK(log.value().current_checkpoint_id().empty());
    CHECK(log.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!log.value().authorizes_execution());
    CHECK(!TransparencyLog::create(log_path, log_identity.value()));
    CHECK(TransparencyLog::open(log_path, log_identity.value(), ""));

    auto anchor_record =
        log.value().publish_deployment_anchor(anchor.value(), transparency_pair.value().secret_key, "");
    CHECK(anchor_record);
    CHECK(anchor_record.value().valid());
    CHECK(anchor_record.value().sequence == 0);
    CHECK(log.value().records().size() == 1);
    const auto first_checkpoint = anchor_record.value().checkpoint;
    CHECK(verify_transparency_log_checkpoint(log_identity.value(), first_checkpoint));
    auto non_hex_checkpoint = first_checkpoint;
    non_hex_checkpoint.authentication_tag[0] = 'g';
    CHECK(!non_hex_checkpoint.valid());
    CHECK(log.value().current_checkpoint_id() == first_checkpoint.id);
    CHECK(!log.value().publish_runtime_observation(observation_set.value(),
                                                   transparency_pair.value().secret_key, ""));
    CHECK(!log.value().publish_runtime_observation(
        observation_set.value(), observer_one_pair.value().secret_key, first_checkpoint.id));

    auto observation_record = log.value().publish_runtime_observation(
        observation_set.value(), transparency_pair.value().secret_key, first_checkpoint.id);
    CHECK(observation_record);
    CHECK(observation_record.value().valid());
    CHECK(observation_record.value().sequence == 1);
    CHECK(observation_record.value().parent_id == anchor_record.value().id);
    CHECK(observation_record.value().checkpoint.previous_checkpoint_id == first_checkpoint.id);
    CHECK(log.value().records().size() == 2);

    auto anchor_proof = log.value().inclusion_proof(0);
    auto observation_proof = log.value().inclusion_proof(1);
    CHECK(anchor_proof);
    CHECK(observation_proof);
    CHECK(verify_transparency_inclusion(log_identity.value(), observation_record.value().checkpoint,
                                        anchor_record.value().leaf, anchor_proof.value()));
    CHECK(verify_transparency_inclusion(log_identity.value(), observation_record.value().checkpoint,
                                        observation_record.value().leaf, observation_proof.value()));
    CHECK(!log.value().inclusion_proof(2));
    auto tampered_proof = anchor_proof.value();
    tampered_proof.sibling_hashes.front() = digest('0');
    CHECK(!verify_transparency_inclusion(log_identity.value(), observation_record.value().checkpoint,
                                         anchor_record.value().leaf, tampered_proof));

    auto consistency = log.value().consistency_witness(1);
    CHECK(consistency);
    CHECK(consistency.value().ordered_leaf_ids.size() == 2);
    CHECK(verify_transparency_consistency(log_identity.value(), first_checkpoint,
                                          observation_record.value().checkpoint, consistency.value()));
    CHECK(!log.value().consistency_witness(0));
    CHECK(!log.value().consistency_witness(2));
    auto compact_consistency = log.value().compact_consistency_proof(1);
    CHECK(compact_consistency);
    CHECK(compact_consistency.value().old_frontier.size() == 1);
    CHECK(compact_consistency.value().appended_subtrees.size() == 1);
    CHECK(verify_transparency_compact_consistency(log_identity.value(), first_checkpoint,
                                                  observation_record.value().checkpoint,
                                                  compact_consistency.value()));
    auto tampered_compact_consistency = compact_consistency.value();
    tampered_compact_consistency.appended_subtrees.front().hash = digest('0');
    tampered_compact_consistency.id =
        internal::transparency_compact_consistency_proof_identity(tampered_compact_consistency);
    CHECK(!verify_transparency_compact_consistency(log_identity.value(), first_checkpoint,
                                                   observation_record.value().checkpoint,
                                                   tampered_compact_consistency));
    CHECK(!log.value().compact_consistency_proof(0));
    CHECK(!log.value().compact_consistency_proof(2));

    auto first_observer_one_witness = sign_transparency_checkpoint_witness(
        log_identity.value(), first_checkpoint, bundle.value(), observer_one_key.value().service_id,
        observer_one_key.value().id, observer_one_pair.value().secret_key);
    auto first_observer_two_witness = sign_transparency_checkpoint_witness(
        log_identity.value(), first_checkpoint, bundle.value(), observer_two_key.value().service_id,
        observer_two_key.value().id, observer_two_pair.value().secret_key);
    auto second_observer_one_witness = sign_transparency_checkpoint_witness(
        log_identity.value(), observation_record.value().checkpoint, bundle.value(),
        observer_one_key.value().service_id, observer_one_key.value().id,
        observer_one_pair.value().secret_key);
    auto second_observer_two_witness = sign_transparency_checkpoint_witness(
        log_identity.value(), observation_record.value().checkpoint, bundle.value(),
        observer_two_key.value().service_id, observer_two_key.value().id,
        observer_two_pair.value().secret_key);
    CHECK(first_observer_one_witness);
    CHECK(first_observer_two_witness);
    CHECK(second_observer_one_witness);
    CHECK(second_observer_two_witness);
    CHECK(!sign_transparency_checkpoint_witness(log_identity.value(), observation_record.value().checkpoint,
                                                bundle.value(), observer_one_key.value().service_id,
                                                observer_one_key.value().id,
                                                observer_two_pair.value().secret_key));

    TransparencyCheckpointWitnessPolicy witness_policy;
    auto pending_observer_key =
        make_service_public_key("observer-pending", observer_one_pair.value().public_key, 1, 0,
                                ServiceKeyState::Pending, false, true, false);
    auto nonpublishing_observer_key =
        make_service_public_key("observer-no-publish", observer_one_pair.value().public_key, 1, 0,
                                ServiceKeyState::Active, true, false, false);
    CHECK(pending_observer_key);
    CHECK(nonpublishing_observer_key);
    auto pending_observer_bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {pending_observer_key.value(), observer_two_key.value()}, rotation);
    auto nonpublishing_observer_bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {nonpublishing_observer_key.value(), observer_two_key.value()}, rotation);
    CHECK(pending_observer_bundle);
    CHECK(nonpublishing_observer_bundle);
    CHECK(!sign_transparency_checkpoint_witness(
        log_identity.value(), observation_record.value().checkpoint, pending_observer_bundle.value(),
        pending_observer_key.value().service_id, pending_observer_key.value().id,
        observer_one_pair.value().secret_key));
    CHECK(!sign_transparency_checkpoint_witness(
        log_identity.value(), observation_record.value().checkpoint, nonpublishing_observer_bundle.value(),
        nonpublishing_observer_key.value().service_id, nonpublishing_observer_key.value().id,
        observer_one_pair.value().secret_key));

    auto log_signer_bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {observer_two_key.value(), transparency_key.value()}, rotation);
    CHECK(log_signer_bundle);
    auto log_signer_witness = sign_transparency_checkpoint_witness(
        log_identity.value(), observation_record.value().checkpoint, log_signer_bundle.value(),
        transparency_key.value().service_id, transparency_key.value().id,
        transparency_pair.value().secret_key);
    auto observer_two_log_bundle_witness = sign_transparency_checkpoint_witness(
        log_identity.value(), observation_record.value().checkpoint, log_signer_bundle.value(),
        observer_two_key.value().service_id, observer_two_key.value().id,
        observer_two_pair.value().secret_key);
    CHECK(log_signer_witness);
    CHECK(observer_two_log_bundle_witness);
    CHECK(!assemble_witnessed_transparency_checkpoint(
        log_identity.value(), observation_record.value().checkpoint, witness_policy,
        {log_signer_witness.value(), observer_two_log_bundle_witness.value()}, log_signer_bundle.value()));

    auto witnessed_first = assemble_witnessed_transparency_checkpoint(
        log_identity.value(), first_checkpoint, witness_policy,
        {first_observer_two_witness.value(), first_observer_one_witness.value()}, bundle.value());
    auto witnessed_second = assemble_witnessed_transparency_checkpoint(
        log_identity.value(), observation_record.value().checkpoint, witness_policy,
        {second_observer_two_witness.value(), second_observer_one_witness.value()}, bundle.value());
    CHECK(witnessed_first);
    CHECK(witnessed_second);
    CHECK(witnessed_second.value().cosignatures.front().witness_service_id == "observer-one");
    CHECK(verify_witnessed_transparency_checkpoint(log_identity.value(), witnessed_first.value(),
                                                   bundle.value()));
    CHECK(verify_witnessed_transparency_checkpoint(log_identity.value(), witnessed_second.value(),
                                                   bundle.value()));
    CHECK(witnessed_second.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!witnessed_second.value().authorizes_execution());
    CHECK(!assemble_witnessed_transparency_checkpoint(
        log_identity.value(), observation_record.value().checkpoint, witness_policy,
        {second_observer_one_witness.value(), second_observer_one_witness.value()}, bundle.value()));
    auto three_witness_policy = witness_policy;
    three_witness_policy.minimum_witnesses = 3;
    CHECK(!assemble_witnessed_transparency_checkpoint(
        log_identity.value(), observation_record.value().checkpoint, three_witness_policy,
        {second_observer_one_witness.value(), second_observer_two_witness.value()}, bundle.value()));

    auto first_gossip = sign_transparency_checkpoint_gossip(
        log_identity.value(), witnessed_first.value(), std::nullopt, "transparency-auditor", 1, "",
        bundle.value(), safety_key.value().service_id, safety_key.value().id, safety_pair.value().secret_key);
    auto second_gossip = sign_transparency_checkpoint_gossip(
        log_identity.value(), witnessed_second.value(), compact_consistency.value(), "transparency-auditor",
        2, first_gossip ? first_gossip.value().id : std::string{}, bundle.value(),
        safety_key.value().service_id, safety_key.value().id, safety_pair.value().secret_key);
    CHECK(first_gossip);
    CHECK(second_gossip);
    CHECK(verify_transparency_checkpoint_gossip(log_identity.value(), first_gossip.value(), bundle.value()));
    CHECK(verify_transparency_checkpoint_gossip(log_identity.value(), second_gossip.value(), bundle.value()));
    CHECK(second_gossip.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!second_gossip.value().authorizes_execution());
    auto consistent_gossip = audit_transparency_checkpoint_gossip(
        log_identity.value(),
        std::vector<TransparencyCheckpointGossip>{second_gossip.value(), first_gossip.value()},
        bundle.value());
    CHECK(consistent_gossip);
    CHECK(consistent_gossip.value().status == TransparencyGossipStatus::Consistent);
    CHECK(consistent_gossip.value().linked_checkpoint_pairs == 1);
    CHECK(consistent_gossip.value().unlinked_checkpoint_pairs == 0);
    CHECK(consistent_gossip.value().conflicts.empty());
    CHECK(consistent_gossip.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!consistent_gossip.value().authorizes_execution());
    CHECK(!audit_transparency_checkpoint_gossip(
        log_identity.value(),
        std::vector<TransparencyCheckpointGossip>{first_gossip.value(), first_gossip.value()},
        bundle.value()));
    TransparencyGossipAuditOptions one_gossip_message;
    one_gossip_message.maximum_gossip_messages = 1;
    CHECK(!audit_transparency_checkpoint_gossip(
        log_identity.value(),
        std::vector<TransparencyCheckpointGossip>{first_gossip.value(), second_gossip.value()},
        bundle.value(), one_gossip_message));
    TransparencyGossipAuditOptions one_unique_checkpoint;
    one_unique_checkpoint.maximum_unique_checkpoints = 1;
    CHECK(!audit_transparency_checkpoint_gossip(
        log_identity.value(),
        std::vector<TransparencyCheckpointGossip>{first_gossip.value(), second_gossip.value()},
        bundle.value(), one_unique_checkpoint));
    TransparencyGossipAuditOptions cancelled_gossip_audit;
    cancelled_gossip_audit.cancellation.cancel();
    CHECK(!audit_transparency_checkpoint_gossip(
        log_identity.value(),
        std::vector<TransparencyCheckpointGossip>{first_gossip.value(), second_gossip.value()},
        bundle.value(), cancelled_gossip_audit));

    auto unlinked_second_gossip = sign_transparency_checkpoint_gossip(
        log_identity.value(), witnessed_second.value(), std::nullopt, "transparency-auditor", 1, "",
        bundle.value(), controls_key.value().service_id, controls_key.value().id,
        controls_pair.value().secret_key);
    CHECK(unlinked_second_gossip);
    auto incomplete_gossip = audit_transparency_checkpoint_gossip(
        log_identity.value(),
        std::vector<TransparencyCheckpointGossip>{first_gossip.value(), unlinked_second_gossip.value()},
        bundle.value());
    CHECK(incomplete_gossip);
    CHECK(incomplete_gossip.value().status == TransparencyGossipStatus::Incomplete);
    CHECK(incomplete_gossip.value().unlinked_checkpoint_pairs == 1);

    auto invalid_consistency = compact_consistency.value();
    invalid_consistency.appended_subtrees.front().hash = digest('0');
    invalid_consistency.id = internal::transparency_compact_consistency_proof_identity(invalid_consistency);
    auto invalid_consistency_gossip = sign_transparency_checkpoint_gossip(
        log_identity.value(), witnessed_second.value(), invalid_consistency, "transparency-auditor", 1, "",
        bundle.value(), controls_key.value().service_id, controls_key.value().id,
        controls_pair.value().secret_key);
    CHECK(invalid_consistency_gossip);
    auto invalid_consistency_report = audit_transparency_checkpoint_gossip(
        log_identity.value(),
        std::vector<TransparencyCheckpointGossip>{first_gossip.value(), invalid_consistency_gossip.value()},
        bundle.value());
    CHECK(invalid_consistency_report);
    CHECK(invalid_consistency_report.value().status == TransparencyGossipStatus::SplitView);
    CHECK(invalid_consistency_report.value().conflicts.size() == 1);
    CHECK(invalid_consistency_report.value().conflicts.front().type ==
          TransparencyGossipConflictType::InvalidConsistencyProof);

    const auto fork_path = temporary / "fork-transparency-log";
    auto fork_log = TransparencyLog::create(fork_path, log_identity.value());
    CHECK(fork_log);
    auto fork_first =
        fork_log.value().publish_deployment_anchor(anchor.value(), transparency_pair.value().secret_key, "");
    CHECK(fork_first);
    CHECK(fork_first.value().checkpoint.id == first_checkpoint.id);
    auto fork_second = fork_log.value().publish_deployment_anchor(
        anchor.value(), transparency_pair.value().secret_key, fork_log.value().current_checkpoint_id());
    CHECK(fork_second);
    CHECK(fork_second.value().checkpoint.tree_size == observation_record.value().checkpoint.tree_size);
    CHECK(fork_second.value().checkpoint.id != observation_record.value().checkpoint.id);
    auto fork_observer_one_witness = sign_transparency_checkpoint_witness(
        log_identity.value(), fork_second.value().checkpoint, bundle.value(),
        observer_one_key.value().service_id, observer_one_key.value().id,
        observer_one_pair.value().secret_key);
    auto fork_observer_two_witness = sign_transparency_checkpoint_witness(
        log_identity.value(), fork_second.value().checkpoint, bundle.value(),
        observer_two_key.value().service_id, observer_two_key.value().id,
        observer_two_pair.value().secret_key);
    CHECK(fork_observer_one_witness);
    CHECK(fork_observer_two_witness);
    auto witnessed_fork = assemble_witnessed_transparency_checkpoint(
        log_identity.value(), fork_second.value().checkpoint, witness_policy,
        {fork_observer_two_witness.value(), fork_observer_one_witness.value()}, bundle.value());
    CHECK(witnessed_fork);
    auto fork_consistency = fork_log.value().compact_consistency_proof(1);
    CHECK(fork_consistency);
    auto fork_gossip = sign_transparency_checkpoint_gossip(
        log_identity.value(), witnessed_fork.value(), fork_consistency.value(), "transparency-auditor", 1, "",
        bundle.value(), controls_key.value().service_id, controls_key.value().id,
        controls_pair.value().secret_key);
    CHECK(fork_gossip);
    auto split_view_report = audit_transparency_checkpoint_gossip(
        log_identity.value(),
        std::vector<TransparencyCheckpointGossip>{first_gossip.value(), second_gossip.value(),
                                                  fork_gossip.value()},
        bundle.value());
    CHECK(split_view_report);
    CHECK(split_view_report.value().status == TransparencyGossipStatus::SplitView);
    CHECK(std::any_of(split_view_report.value().conflicts.begin(), split_view_report.value().conflicts.end(),
                      [](const auto& conflict) {
                          return conflict.type == TransparencyGossipConflictType::SameSizeEquivocation;
                      }));

    const auto gossip_archive_path = temporary / "transparency-gossip-archive";
    auto gossip_archive =
        TransparencyGossipArchive::create(gossip_archive_path, log_identity.value(), bundle.value());
    CHECK(gossip_archive);
    CHECK(gossip_archive.value().valid());
    CHECK(gossip_archive.value().records().empty());
    CHECK(gossip_archive.value().current_record_id().empty());
    CHECK(gossip_archive.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!gossip_archive.value().authorizes_execution());
    CHECK(!TransparencyGossipArchive::create(gossip_archive_path, log_identity.value(), bundle.value()));
    CHECK(TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                          bundle.value().id(), ""));

    auto first_gossip_record = gossip_archive.value().publish(first_gossip.value(), "");
    CHECK(first_gossip_record);
    CHECK(first_gossip_record.value().sequence == 0);
    CHECK(first_gossip_record.value().parent_id.empty());
    CHECK(first_gossip_record.value().gossip.id == first_gossip.value().id);
    CHECK(gossip_archive.value().records().size() == 1);
    CHECK(!gossip_archive.value().publish(second_gossip.value(), ""));
    auto second_gossip_record =
        gossip_archive.value().publish(second_gossip.value(), first_gossip_record.value().id);
    CHECK(second_gossip_record);
    CHECK(second_gossip_record.value().sequence == 1);
    CHECK(second_gossip_record.value().parent_id == first_gossip_record.value().id);
    CHECK(gossip_archive.value().records().size() == 2);

    auto archived_consistent = gossip_archive.value().audit();
    CHECK(archived_consistent);
    CHECK(archived_consistent.value().status == TransparencyGossipStatus::Consistent);
    CHECK(archived_consistent.value().authenticated_gossip_count == 2);
    CHECK(TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                          bundle.value().id(), gossip_archive.value().current_record_id()));
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), first_gossip_record.value().id));
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           digest('0'), gossip_archive.value().current_record_id()));

    auto discontinuous_gossip = sign_transparency_checkpoint_gossip(
        log_identity.value(), witnessed_fork.value(), fork_consistency.value(), "transparency-auditor", 3,
        second_gossip.value().id, bundle.value(), controls_key.value().service_id, controls_key.value().id,
        controls_pair.value().secret_key);
    CHECK(discontinuous_gossip);
    CHECK(!gossip_archive.value().publish(discontinuous_gossip.value(),
                                          gossip_archive.value().current_record_id()));

    auto fork_gossip_record =
        gossip_archive.value().publish(fork_gossip.value(), gossip_archive.value().current_record_id());
    CHECK(fork_gossip_record);
    auto archived_split_view = gossip_archive.value().audit();
    CHECK(archived_split_view);
    CHECK(archived_split_view.value().status == TransparencyGossipStatus::SplitView);
    CHECK(std::any_of(archived_split_view.value().conflicts.begin(),
                      archived_split_view.value().conflicts.end(), [](const auto& conflict) {
                          return conflict.type == TransparencyGossipConflictType::SameSizeEquivocation;
                      }));

    TransparencyGossipArchiveLoadOptions two_records;
    two_records.maximum_records = 2;
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id(),
                                           two_records));
    TransparencyGossipArchiveLoadOptions one_witness;
    one_witness.maximum_witnesses_per_checkpoint = 1;
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id(),
                                           one_witness));
    TransparencyGossipArchiveLoadOptions one_total_witness;
    one_total_witness.maximum_total_witnesses = 1;
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id(),
                                           one_total_witness));
    TransparencyGossipArchiveLoadOptions one_proof_subtree;
    one_proof_subtree.maximum_proof_subtrees = 1;
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id(),
                                           one_proof_subtree));
    TransparencyGossipArchiveLoadOptions one_total_proof_subtree;
    one_total_proof_subtree.maximum_total_proof_subtrees = 1;
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id(),
                                           one_total_proof_subtree));
    TransparencyGossipArchiveLoadOptions one_checkpoint;
    one_checkpoint.maximum_unique_checkpoints = 1;
    auto one_checkpoint_archive = TransparencyGossipArchive::open(
        gossip_archive_path, log_identity.value(), bundle.value(), bundle.value().id(),
        gossip_archive.value().current_record_id(), one_checkpoint);
    CHECK(one_checkpoint_archive);
    CHECK(!one_checkpoint_archive.value().audit());
    TransparencyGossipArchiveLoadOptions no_pairs;
    no_pairs.maximum_pair_checks = 1;
    auto no_pairs_archive = TransparencyGossipArchive::open(
        gossip_archive_path, log_identity.value(), bundle.value(), bundle.value().id(),
        gossip_archive.value().current_record_id(), no_pairs);
    CHECK(no_pairs_archive);
    CHECK(!no_pairs_archive.value().audit());
    TransparencyGossipArchiveLoadOptions one_graph_step;
    one_graph_step.maximum_graph_steps = 1;
    auto one_graph_step_archive = TransparencyGossipArchive::open(
        gossip_archive_path, log_identity.value(), bundle.value(), bundle.value().id(),
        gossip_archive.value().current_record_id(), one_graph_step);
    CHECK(one_graph_step_archive);
    CHECK(!one_graph_step_archive.value().audit());
    TransparencyGossipArchiveLoadOptions tiny_gossip_manifest;
    tiny_gossip_manifest.maximum_manifest_bytes = 1;
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id(),
                                           tiny_gossip_manifest));
    TransparencyGossipArchiveLoadOptions tiny_gossip_record;
    tiny_gossip_record.maximum_record_bytes = 1;
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id(),
                                           tiny_gossip_record));
    TransparencyGossipArchiveLoadOptions cancelled_gossip_load;
    cancelled_gossip_load.cancellation.cancel();
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id(),
                                           cancelled_gossip_load));

    std::filesystem::create_directory(gossip_archive_path / "records" / ".tmp-abandoned");
    CHECK(TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                          bundle.value().id(), gossip_archive.value().current_record_id()));
    std::filesystem::remove(gossip_archive_path / "records" / ".tmp-abandoned");
    std::filesystem::create_directory(gossip_archive_path / "records" / "unexpected");
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id()));
    std::filesystem::remove(gossip_archive_path / "records" / "unexpected");
    write_text(gossip_archive_path / "unexpected-root", "unexpected");
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id()));
    std::filesystem::remove(gossip_archive_path / "unexpected-root");

    std::filesystem::create_directory(gossip_archive_path / ".writer-lock");
    CHECK(!gossip_archive.value().publish(first_gossip.value(), gossip_archive.value().current_record_id()));
    std::filesystem::remove(gossip_archive_path / ".writer-lock");

    const auto gossip_manifest_path = gossip_archive_path / "manifest.json";
    const auto gossip_manifest_text = read_text(gossip_manifest_path);
    auto unknown_gossip_schema_manifest = gossip_manifest_text;
    const auto gossip_schema_marker = unknown_gossip_schema_manifest.find("\"schema\": 1");
    CHECK(gossip_schema_marker != std::string::npos);
    unknown_gossip_schema_manifest.replace(gossip_schema_marker, std::string("\"schema\": 1").size(),
                                           "\"schema\": 2");
    write_text(gossip_manifest_path, unknown_gossip_schema_manifest);
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id()));
    write_text(gossip_manifest_path, gossip_manifest_text);
    write_text(gossip_manifest_path, gossip_manifest_text.substr(0, gossip_manifest_text.size() / 2));
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id()));
    write_text(gossip_manifest_path, gossip_manifest_text);

    const auto first_gossip_record_path =
        gossip_archive_path / "records" /
        ("00000000000000000000-" + first_gossip_record.value().id + ".json");
    const auto first_gossip_record_text = read_text(first_gossip_record_path);
    auto tampered_gossip_record_text = first_gossip_record_text;
    const auto gossip_sender_marker =
        tampered_gossip_record_text.find(first_gossip.value().recipient_service_id);
    CHECK(gossip_sender_marker != std::string::npos);
    tampered_gossip_record_text[gossip_sender_marker] =
        tampered_gossip_record_text[gossip_sender_marker] == 't' ? 'x' : 't';
    write_text(first_gossip_record_path, tampered_gossip_record_text);
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id()));
    write_text(first_gossip_record_path, first_gossip_record_text);
    CHECK(TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                          bundle.value().id(), gossip_archive.value().current_record_id()));

    const auto byte_limited_gossip_path = temporary / "byte-limited-gossip-archive";
    CHECK(TransparencyGossipArchive::create(byte_limited_gossip_path, log_identity.value(), bundle.value()));
    TransparencyGossipArchiveLoadOptions byte_limited_gossip_options;
    byte_limited_gossip_options.maximum_record_bytes = 1;
    auto byte_limited_gossip =
        TransparencyGossipArchive::open(byte_limited_gossip_path, log_identity.value(), bundle.value(),
                                        bundle.value().id(), "", byte_limited_gossip_options);
    CHECK(byte_limited_gossip);
    CHECK(!byte_limited_gossip.value().publish(first_gossip.value(), ""));
    CHECK(byte_limited_gossip.value().records().empty());

    auto audit = log.value().audit();
    CHECK(audit);
    CHECK(audit.value().valid());
    CHECK(audit.value().verified_records == 2);
    CHECK(audit.value().deployment_anchor_count == 1);
    CHECK(audit.value().runtime_observation_count == 1);
    CHECK(audit.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!audit.value().authorizes_execution());

    auto reopened =
        TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id());
    CHECK(reopened);
    CHECK(reopened.value().valid());
    CHECK(reopened.value().records().size() == 2);
    CHECK(reopened.value().current_root_hash() == log.value().current_root_hash());
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), first_checkpoint.id));
    auto wrong_identity =
        TransparencyLogIdentity::create("rbfsafe-other-log-v1", transparency_key.value().service_id,
                                        transparency_key.value().id, transparency_pair.value().public_key);
    CHECK(wrong_identity);
    CHECK(!TransparencyLog::open(log_path, wrong_identity.value(), log.value().current_checkpoint_id()));
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, wrong_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id()));

    TransparencyLogLoadOptions one_record;
    one_record.maximum_records = 1;
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id(),
                                 one_record));
    TransparencyLogLoadOptions one_attestation;
    one_attestation.maximum_attestations_per_observation = 1;
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id(),
                                 one_attestation));
    TransparencyLogLoadOptions one_total_attestation;
    one_total_attestation.maximum_total_attestations = 1;
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id(),
                                 one_total_attestation));
    TransparencyLogLoadOptions tiny_manifest;
    tiny_manifest.maximum_manifest_bytes = 1;
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id(),
                                 tiny_manifest));
    TransparencyLogLoadOptions tiny_record;
    tiny_record.maximum_record_bytes = 1;
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id(),
                                 tiny_record));
    TransparencyLogLoadOptions cancelled_options;
    cancelled_options.cancellation.cancel();
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id(),
                                 cancelled_options));

    std::filesystem::create_directory(log_path / "records" / ".tmp-abandoned");
    CHECK(TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id()));
    std::filesystem::remove(log_path / "records" / ".tmp-abandoned");
    std::filesystem::create_directory(log_path / "records" / "unexpected");
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id()));
    std::filesystem::remove(log_path / "records" / "unexpected");
    write_text(log_path / "unexpected-root", "unexpected");
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id()));
    std::filesystem::remove(log_path / "unexpected-root");

    std::filesystem::create_directory(log_path / ".writer-lock");
    CHECK(!log.value().publish_deployment_anchor(anchor.value(), transparency_pair.value().secret_key,
                                                 log.value().current_checkpoint_id()));
    std::filesystem::remove(log_path / ".writer-lock");

    const auto manifest_path = log_path / "manifest.json";
    const auto manifest_text = read_text(manifest_path);
    auto unknown_schema_manifest = manifest_text;
    const auto schema_marker = unknown_schema_manifest.find("\"schema\": 1");
    CHECK(schema_marker != std::string::npos);
    unknown_schema_manifest.replace(schema_marker, std::string("\"schema\": 1").size(), "\"schema\": 2");
    write_text(manifest_path, unknown_schema_manifest);
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id()));
    write_text(manifest_path, manifest_text);
    write_text(manifest_path, manifest_text.substr(0, manifest_text.size() / 2));
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id()));
    write_text(manifest_path, manifest_text);
    CHECK(TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id()));

    const auto first_record_path =
        log_path / "records" / ("00000000000000000000-" + anchor_record.value().id + ".json");
    const auto first_record_text = read_text(first_record_path);
    auto tampered_record_text = first_record_text;
    const auto marker = tampered_record_text.find(anchor.value().deployment_id);
    CHECK(marker != std::string::npos);
    tampered_record_text[marker] = tampered_record_text[marker] == 't' ? 'x' : 't';
    write_text(first_record_path, tampered_record_text);
    CHECK(!TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id()));
    write_text(first_record_path, first_record_text);
    CHECK(TransparencyLog::open(log_path, log_identity.value(), log.value().current_checkpoint_id()));

    const auto limited_path = temporary / "limited-transparency-log";
    CHECK(TransparencyLog::create(limited_path, log_identity.value()));
    TransparencyLogLoadOptions limited_options;
    limited_options.maximum_total_attestations = 1;
    auto limited_log = TransparencyLog::open(limited_path, log_identity.value(), "", limited_options);
    CHECK(limited_log);
    auto limited_anchor = limited_log.value().publish_deployment_anchor(
        anchor.value(), transparency_pair.value().secret_key, "");
    CHECK(limited_anchor);
    CHECK(!limited_log.value().publish_runtime_observation(observation_set.value(),
                                                           transparency_pair.value().secret_key,
                                                           limited_log.value().current_checkpoint_id()));
    CHECK(limited_log.value().records().size() == 1);

    const auto byte_limited_path = temporary / "byte-limited-transparency-log";
    CHECK(TransparencyLog::create(byte_limited_path, log_identity.value()));
    TransparencyLogLoadOptions byte_limited_options;
    byte_limited_options.maximum_record_bytes = 1;
    auto byte_limited_log =
        TransparencyLog::open(byte_limited_path, log_identity.value(), "", byte_limited_options);
    CHECK(byte_limited_log);
    CHECK(!byte_limited_log.value().publish_deployment_anchor(anchor.value(),
                                                              transparency_pair.value().secret_key, ""));
    CHECK(byte_limited_log.value().records().empty());

    const auto compact_path = temporary / "compact-consistency-log";
    auto compact_log = TransparencyLog::create(compact_path, log_identity.value());
    CHECK(compact_log);
    std::vector<TransparencyLogCheckpoint> compact_checkpoints;
    compact_checkpoints.reserve(17);
    for (std::size_t index = 0; index < 17; ++index) {
        auto published = compact_log.value().publish_deployment_anchor(
            anchor.value(), transparency_pair.value().secret_key,
            compact_log.value().current_checkpoint_id());
        CHECK(published);
        compact_checkpoints.push_back(published.value().checkpoint);
    }
    for (std::uint64_t old_size = 1; old_size < compact_checkpoints.size(); ++old_size) {
        auto proof = compact_log.value().compact_consistency_proof(old_size);
        CHECK(proof);
        CHECK(proof.value().old_frontier.size() <= 64);
        CHECK(proof.value().appended_subtrees.size() <= 128);
        CHECK(verify_transparency_compact_consistency(
            log_identity.value(), compact_checkpoints[static_cast<std::size_t>(old_size - 1U)],
            compact_checkpoints.back(), proof.value()));
    }

    auto fixed_log = TransparencyLog::open(
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "transparency_log_schema1", log_identity.value(),
        "86d47335bee5850b9c3a404e123d50bdb751cabee03a76de6361b8e25f03772f");
    CHECK(fixed_log);
    CHECK(fixed_log.value().identity().id ==
          "e77f9b5d98d731c0b2e6f41486c3c6870488962aa77d5c12fca4eb5e160655d4");
    CHECK(fixed_log.value().current_root_hash() ==
          "fe8e39f32feae84fae08a914375b1e3fec1afff94f57f97ff7955b3945c14eb1");
    CHECK(fixed_log.value().records().size() == 2);
    CHECK(fixed_log.value().records()[0].id ==
          "988bc018f370b653a31a8a480a01f0f9a21ab6b357a9615cb186952d92761568");
    CHECK(fixed_log.value().records()[1].id ==
          "d95ca7819bd6f1d5c3fbb084d35579c46fa2d620f8ac5c4cba690d77331f380f");
    CHECK(fixed_log.value().audit());

    const auto fixed_session_fixture =
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "bounded_execution_session_schema1";
    auto fixed_checkpoint = ServiceTrustCheckpoint::load(fixed_session_fixture / "checkpoint.json");
    CHECK(fixed_checkpoint);
    auto fixed_history = ServiceTrustHistory::open(
        fixed_session_fixture / "trust-history",
        "3b295bc13d0831ace4bc8a73349dc87f249d09c238468c4058f506a94554c780", fixed_checkpoint.value(),
        "3ebcb9e144577ba8b828f8b728c43b90f1b7412d09212cfec40e69fa1d3f9e01");
    CHECK(fixed_history);
    auto fixed_bundle = fixed_history.value().current_bundle();
    CHECK(fixed_bundle);
    auto fixed_gossip_archive = TransparencyGossipArchive::open(
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "transparency_gossip_archive_schema1",
        log_identity.value(), fixed_bundle.value(), fixed_bundle.value().id(),
        "fd5ac959b484ada7ea2ce15e7cc8bccf41d8b6eaa368d9dafc2aedcdb0036514");
    CHECK(fixed_gossip_archive);
    CHECK(fixed_gossip_archive.value().records().size() == 2);
    CHECK(fixed_gossip_archive.value().records()[0].id ==
          "e4752943e7e504114a181efe8115abb53447e137f542655dd53ada9bce5a4dd2");
    CHECK(fixed_gossip_archive.value().records()[1].id ==
          "fd5ac959b484ada7ea2ce15e7cc8bccf41d8b6eaa368d9dafc2aedcdb0036514");
    auto fixed_gossip_audit = fixed_gossip_archive.value().audit();
    CHECK(fixed_gossip_audit);
    CHECK(fixed_gossip_audit.value().status == TransparencyGossipStatus::Consistent);
    CHECK(fixed_gossip_audit.value().authenticated_gossip_count == 2);
    CHECK(fixed_gossip_audit.value().unique_checkpoint_count == 2);

#ifndef _WIN32
    const auto linked_log = temporary / "linked-log";
    std::filesystem::create_directory_symlink(log_path, linked_log);
    CHECK(!TransparencyLog::open(linked_log, log_identity.value(), log.value().current_checkpoint_id()));

    const auto linked_gossip_archive = temporary / "linked-gossip-archive";
    std::filesystem::create_directory_symlink(gossip_archive_path, linked_gossip_archive);
    CHECK(!TransparencyGossipArchive::open(linked_gossip_archive, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id()));

    const auto linked_gossip_record = gossip_archive_path / "records" / "linked-record.json";
    std::filesystem::create_symlink(gossip_archive_path / "records" /
                                        ("00000000000000000000-" + first_gossip_record.value().id + ".json"),
                                    linked_gossip_record);
    CHECK(!TransparencyGossipArchive::open(gossip_archive_path, log_identity.value(), bundle.value(),
                                           bundle.value().id(), gossip_archive.value().current_record_id()));
    std::filesystem::remove(linked_gossip_record);
#endif

    std::filesystem::remove_all(temporary);
    return 0;
}
