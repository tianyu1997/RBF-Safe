#include <rbfsafe/rbfsafe.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string digest(char value) { return std::string(64, value); }

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;
    if (argc != 4) {
        std::cerr << "usage: rbfsafe_deployment_profile_quickstart "
                     "<new-trust-history-directory> <new-checkpoint-file> "
                     "<new-reviewed-profile-file>\n";
        return 2;
    }

    std::array<std::byte, kEd25519SeedBytes> reviewer_seed_a{};
    std::array<std::byte, kEd25519SeedBytes> reviewer_seed_b{};
    std::array<std::byte, kEd25519SeedBytes> governance_seed{};
    for (std::size_t index = 0; index < kEd25519SeedBytes; ++index) {
        reviewer_seed_a[index] = static_cast<std::byte>(index + 1);
        reviewer_seed_b[index] = static_cast<std::byte>(index + 33);
        governance_seed[index] = static_cast<std::byte>(index + 65);
    }
    auto pair_a = ed25519_key_pair_from_seed(reviewer_seed_a);
    auto pair_b = ed25519_key_pair_from_seed(reviewer_seed_b);
    auto governance_pair = ed25519_key_pair_from_seed(governance_seed);
    if (!pair_a || !pair_b || !governance_pair) {
        std::cerr << "failed to create deterministic example keys\n";
        return 1;
    }
    auto reviewer_a = make_service_public_key("review-safety", pair_a.value().public_key, 1, 0,
                                              ServiceKeyState::Active, false, true, false);
    auto reviewer_b = make_service_public_key("review-controls", pair_b.value().public_key, 1, 0,
                                              ServiceKeyState::Active, false, true, false);
    auto governance = make_service_public_key("trust-governance", governance_pair.value().public_key, 1, 0,
                                              ServiceKeyState::Active, false, false, true);
    if (!reviewer_a || !reviewer_b || !governance) {
        std::cerr << "failed to create reviewer identities\n";
        return 1;
    }
    ServiceTrustRotationPolicy rotation_policy;
    rotation_policy.minimum_signatures = 1;
    auto bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {reviewer_b.value(), governance.value(), reviewer_a.value()}, rotation_policy);
    if (!bundle) {
        std::cerr << bundle.error().describe() << '\n';
        return 1;
    }
    auto history =
        ServiceTrustHistory::create(std::filesystem::path(argv[1]), bundle.value(), bundle.value().id());
    if (!history) {
        std::cerr << history.error().describe() << '\n';
        return 1;
    }
    auto checkpoint_signature =
        sign_service_trust_checkpoint(history.value(), governance.value().service_id, governance.value().id,
                                      governance_pair.value().secret_key);
    if (!checkpoint_signature) {
        std::cerr << checkpoint_signature.error().describe() << '\n';
        return 1;
    }
    auto checkpoint = assemble_service_trust_checkpoint(history.value(), {checkpoint_signature.value()});
    if (!checkpoint) {
        std::cerr << checkpoint.error().describe() << '\n';
        return 1;
    }
    auto checkpoint_saved = checkpoint.value().save(std::filesystem::path(argv[2]));
    if (!checkpoint_saved) {
        std::cerr << checkpoint_saved.error().describe() << '\n';
        return 1;
    }

    DeploymentProfileInput input;
    input.deployment_id = "cell-a";
    input.robot_digest = digest('a');
    input.controller_digest = digest('b');
    input.platform_digest = digest('c');
    input.runtime_digest = digest('d');
    input.trust_root_bundle_id = bundle.value().id();
    input.trust_checkpoint_id = checkpoint.value().id;
    input.trust_bundle_id = bundle.value().id();
    input.trust_bundle_sequence = bundle.value().sequence();
    input.runtime_constraints.maximum_observation_age_ns = 50'000;
    input.runtime_constraints.maximum_command_latency_ns = 50'000;
    input.runtime_constraints.maximum_control_period_ns = 2'000'000;
    input.runtime_constraints.maximum_consecutive_missed_cycles = 1;
    input.review_policy.minimum_approvals = 2;
    input.review_policy.require_distinct_services = true;
    input.review_policy.required_roles = {DeploymentReviewRole::Controls, DeploymentReviewRole::Safety};
    auto profile = DeploymentProfile::create(std::move(input));
    if (!profile) {
        std::cerr << profile.error().describe() << '\n';
        return 1;
    }
    auto approval_a = sign_deployment_profile_approval(profile.value(), reviewer_a.value().service_id,
                                                       reviewer_a.value().id, DeploymentReviewRole::Safety,
                                                       pair_a.value().secret_key);
    auto approval_b = sign_deployment_profile_approval(profile.value(), reviewer_b.value().service_id,
                                                       reviewer_b.value().id, DeploymentReviewRole::Controls,
                                                       pair_b.value().secret_key);
    if (!approval_a || !approval_b) {
        std::cerr << "failed to sign deployment profile approvals\n";
        return 1;
    }
    auto approval_set =
        assemble_deployment_profile_approvals(profile.value(), {approval_b.value(), approval_a.value()});
    if (!approval_set) {
        std::cerr << approval_set.error().describe() << '\n';
        return 1;
    }
    auto reviewed = ReviewedDeploymentProfile::create(profile.value(), approval_set.value(), history.value(),
                                                      checkpoint.value(), checkpoint.value().id);
    if (!reviewed) {
        std::cerr << reviewed.error().describe() << '\n';
        return 1;
    }
    auto saved = reviewed.value().save(std::filesystem::path(argv[3]));
    if (!saved) {
        std::cerr << saved.error().describe() << '\n';
        return 1;
    }

    DeploymentRuntimeSnapshot snapshot;
    snapshot.deployment_id = "cell-a";
    snapshot.robot_digest = digest('a');
    snapshot.controller_digest = digest('b');
    snapshot.platform_digest = digest('c');
    snapshot.runtime_digest = digest('d');
    snapshot.observation_age_ns = 10'000;
    snapshot.command_latency_ns = 20'000;
    snapshot.control_period_ns = 1'000'000;
    snapshot.runtime_monitor_active = true;
    snapshot.fail_closed_transport_active = true;
    snapshot.authenticated_artifacts = true;
    auto assessment = reviewed.value().assess(snapshot);
    if (!assessment) {
        std::cerr << assessment.error().describe() << '\n';
        return 1;
    }

    std::cout << "trust_root=" << bundle.value().id() << '\n'
              << "checkpoint=" << checkpoint.value().id << '\n'
              << "profile=" << profile.value().id << '\n'
              << "approval_set=" << approval_set.value().id << '\n'
              << "assessment=" << assessment.value().id << '\n'
              << "status=" << deployment_profile_assessment_status_name(assessment.value().status) << '\n'
              << "approvals=" << approval_set.value().approvals.size() << '\n'
              << "runtime_executable=false\n";
    return 0;
}
