#include "test_support.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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

rbfsafe::DeploymentRuntimeSnapshot conformant_snapshot() {
    rbfsafe::DeploymentRuntimeSnapshot snapshot;
    snapshot.deployment_id = "cell-a";
    snapshot.robot_digest = digest('a');
    snapshot.controller_digest = digest('b');
    snapshot.platform_digest = digest('c');
    snapshot.runtime_digest = digest('d');
    snapshot.observation_age_ns = 10'000;
    snapshot.command_latency_ns = 20'000;
    snapshot.control_period_ns = 1'000'000;
    snapshot.consecutive_missed_cycles = 0;
    snapshot.runtime_monitor_active = true;
    snapshot.fail_closed_transport_active = true;
    snapshot.authenticated_artifacts = true;
    return snapshot;
}

} // namespace

int main() {
    using namespace rbfsafe;

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
    CHECK(pair_a);
    CHECK(pair_b);
    CHECK(governance_pair);
    auto reviewer_a = make_service_public_key("review-safety", pair_a.value().public_key, 1, 0,
                                              ServiceKeyState::Active, false, true, false);
    auto reviewer_b = make_service_public_key("review-controls", pair_b.value().public_key, 1, 0,
                                              ServiceKeyState::Active, false, true, false);
    auto governance = make_service_public_key("trust-governance", governance_pair.value().public_key, 1, 0,
                                              ServiceKeyState::Active, false, false, true);
    CHECK(reviewer_a);
    CHECK(reviewer_b);
    CHECK(governance);
    ServiceTrustRotationPolicy rotation_policy;
    rotation_policy.minimum_signatures = 1;
    auto bundle = ServiceTrustBundle::create_with_rotation_policy(
        1, "", {reviewer_b.value(), governance.value(), reviewer_a.value()}, rotation_policy);
    CHECK(bundle);

    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("rbfsafe-deployment-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto history_path = temporary / "trust-history";
    auto history = ServiceTrustHistory::create(history_path, bundle.value(), bundle.value().id());
    CHECK(history);
    auto checkpoint_signature =
        sign_service_trust_checkpoint(history.value(), governance.value().service_id, governance.value().id,
                                      governance_pair.value().secret_key);
    CHECK(checkpoint_signature);
    auto checkpoint = assemble_service_trust_checkpoint(history.value(), {checkpoint_signature.value()});
    CHECK(checkpoint);

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
    input.review_policy.required_roles = {DeploymentReviewRole::Controls, DeploymentReviewRole::Safety,
                                          DeploymentReviewRole::Controls};
    auto profile = DeploymentProfile::create(input);
    CHECK(profile);
    CHECK(profile.value().valid());
    CHECK(profile.value().review_policy.required_roles.size() == 2);
    CHECK(profile.value().review_policy.required_roles[0] == DeploymentReviewRole::Safety);
    CHECK(!DeploymentProfile::create(DeploymentProfileInput{}));

    auto approval_a = sign_deployment_profile_approval(profile.value(), reviewer_a.value().service_id,
                                                       reviewer_a.value().id, DeploymentReviewRole::Safety,
                                                       pair_a.value().secret_key);
    auto approval_b = sign_deployment_profile_approval(profile.value(), reviewer_b.value().service_id,
                                                       reviewer_b.value().id, DeploymentReviewRole::Controls,
                                                       pair_b.value().secret_key);
    CHECK(approval_a);
    CHECK(approval_b);
    CHECK(valid_deployment_profile_approval(approval_a.value()));
    CHECK(!sign_deployment_profile_approval(profile.value(), reviewer_a.value().service_id,
                                            reviewer_a.value().id, DeploymentReviewRole::Safety,
                                            pair_b.value().secret_key));
    CHECK(!assemble_deployment_profile_approvals(profile.value(), {approval_a.value()}));
    CHECK(!assemble_deployment_profile_approvals(profile.value(), {approval_a.value(), approval_a.value()}));
    auto approval_set =
        assemble_deployment_profile_approvals(profile.value(), {approval_b.value(), approval_a.value()});
    CHECK(approval_set);
    CHECK(valid_deployment_profile_approval_set(approval_set.value()));
    CHECK(approval_set.value().approvals[0].signer_service_id == "review-controls");
    CHECK(verify_deployment_profile_approvals(profile.value(), approval_set.value(), bundle.value()));
    auto unprivileged_approval = sign_deployment_profile_approval(
        profile.value(), governance.value().service_id, governance.value().id, DeploymentReviewRole::Controls,
        governance_pair.value().secret_key);
    CHECK(unprivileged_approval);
    auto unprivileged_set = assemble_deployment_profile_approvals(
        profile.value(), {approval_a.value(), unprivileged_approval.value()});
    CHECK(unprivileged_set);
    CHECK(!verify_deployment_profile_approvals(profile.value(), unprivileged_set.value(), bundle.value()));
    auto tampered_approval_set = approval_set.value();
    tampered_approval_set.approvals[0].authentication_tag[0] =
        tampered_approval_set.approvals[0].authentication_tag[0] == '0' ? '1' : '0';
    CHECK(!valid_deployment_profile_approval_set(tampered_approval_set));

    auto reviewed = ReviewedDeploymentProfile::create(profile.value(), approval_set.value(), history.value(),
                                                      checkpoint.value(), checkpoint.value().id);
    CHECK(reviewed);
    CHECK(reviewed.value().valid());
    CHECK(!reviewed.value().authorizes_execution());
    CHECK(!ReviewedDeploymentProfile::create(profile.value(), approval_set.value(), history.value(),
                                             checkpoint.value(), digest('f')));
    auto wrong_head_input = input;
    wrong_head_input.trust_bundle_id = digest('e');
    auto wrong_head_profile = DeploymentProfile::create(std::move(wrong_head_input));
    CHECK(wrong_head_profile);
    auto wrong_head_approval_a = sign_deployment_profile_approval(
        wrong_head_profile.value(), reviewer_a.value().service_id, reviewer_a.value().id,
        DeploymentReviewRole::Safety, pair_a.value().secret_key);
    auto wrong_head_approval_b = sign_deployment_profile_approval(
        wrong_head_profile.value(), reviewer_b.value().service_id, reviewer_b.value().id,
        DeploymentReviewRole::Controls, pair_b.value().secret_key);
    CHECK(wrong_head_approval_a);
    CHECK(wrong_head_approval_b);
    auto wrong_head_approvals = assemble_deployment_profile_approvals(
        wrong_head_profile.value(), {wrong_head_approval_a.value(), wrong_head_approval_b.value()});
    CHECK(wrong_head_approvals);
    CHECK(!ReviewedDeploymentProfile::create(wrong_head_profile.value(), wrong_head_approvals.value(),
                                             history.value(), checkpoint.value(), checkpoint.value().id));

    const auto good_snapshot = conformant_snapshot();
    CHECK(valid_deployment_runtime_snapshot(good_snapshot));
    auto conformant = reviewed.value().assess(good_snapshot);
    CHECK(conformant);
    CHECK(conformant.value().valid());
    CHECK(conformant.value().status == DeploymentProfileAssessmentStatus::Conformant);
    CHECK(conformant.value().violations.empty());
    CHECK(conformant.value().evidence == EvidenceLevel::Unknown);
    CHECK(!conformant.value().authorizes_execution());

    auto bad_snapshot = good_snapshot;
    bad_snapshot.robot_digest = digest('e');
    bad_snapshot.observation_age_ns = 50'001;
    bad_snapshot.command_latency_ns = 50'001;
    bad_snapshot.control_period_ns = 2'000'001;
    bad_snapshot.consecutive_missed_cycles = 2;
    bad_snapshot.runtime_monitor_active = false;
    bad_snapshot.fail_closed_transport_active = false;
    bad_snapshot.authenticated_artifacts = false;
    auto nonconformant = reviewed.value().assess(bad_snapshot);
    CHECK(nonconformant);
    CHECK(nonconformant.value().valid());
    CHECK(nonconformant.value().status == DeploymentProfileAssessmentStatus::Nonconformant);
    CHECK(nonconformant.value().violations.size() == 8);
    CHECK(nonconformant.value().violations.front() == DeploymentConstraintViolation::RobotIdentityMismatch);
    CHECK(deployment_constraint_violation_name(nonconformant.value().violations.back()) ==
          "authenticated_artifacts_required");
    auto wrong_identity_snapshot = good_snapshot;
    wrong_identity_snapshot.deployment_id = "cell-b";
    wrong_identity_snapshot.robot_digest = digest('e');
    wrong_identity_snapshot.controller_digest = digest('e');
    wrong_identity_snapshot.platform_digest = digest('e');
    wrong_identity_snapshot.runtime_digest = digest('e');
    auto wrong_identity = reviewed.value().assess(wrong_identity_snapshot);
    CHECK(wrong_identity);
    CHECK(wrong_identity.value().violations.size() == 5);
    CHECK(wrong_identity.value().violations.front() ==
          DeploymentConstraintViolation::DeploymentIdentityMismatch);
    CHECK(wrong_identity.value().violations.back() == DeploymentConstraintViolation::RuntimeIdentityMismatch);

    const auto profile_path = temporary / "reviewed-deployment-profile.json";
    CHECK(reviewed.value().save(profile_path));
    CHECK(!reviewed.value().save(profile_path));
    SaveOptions overwrite;
    overwrite.overwrite = true;
    CHECK(reviewed.value().save(profile_path, overwrite));
    const auto directory_destination = temporary / "directory-destination";
    std::filesystem::create_directory(directory_destination);
    CHECK(!reviewed.value().save(directory_destination, overwrite));
    const auto symlink_destination = temporary / "symlink-destination.json";
    std::error_code symlink_error;
    std::filesystem::create_symlink(profile_path, symlink_destination, symlink_error);
    if (!symlink_error)
        CHECK(!reviewed.value().save(symlink_destination, overwrite));
    auto loaded = ReviewedDeploymentProfile::load(profile_path, history.value(), checkpoint.value(),
                                                  checkpoint.value().id);
    CHECK(loaded);
    CHECK(loaded.value().profile().id == profile.value().id);
    CHECK(loaded.value().approval_set().id == approval_set.value().id);
    CHECK(loaded.value().assess(good_snapshot).value().id == conformant.value().id);

    ReviewedDeploymentProfileLoadOptions one_approval;
    one_approval.maximum_approvals = 1;
    auto approval_limited = ReviewedDeploymentProfile::load(profile_path, history.value(), checkpoint.value(),
                                                            checkpoint.value().id, one_approval);
    CHECK(!approval_limited);
    CHECK(approval_limited.error().code == StatusCode::ResourceLimit);
    ReviewedDeploymentProfileLoadOptions one_role;
    one_role.maximum_required_roles = 1;
    auto role_limited = ReviewedDeploymentProfile::load(profile_path, history.value(), checkpoint.value(),
                                                        checkpoint.value().id, one_role);
    CHECK(!role_limited);
    CHECK(role_limited.error().code == StatusCode::ResourceLimit);
    ReviewedDeploymentProfileLoadOptions one_byte;
    one_byte.maximum_payload_bytes = 1;
    auto byte_limited = ReviewedDeploymentProfile::load(profile_path, history.value(), checkpoint.value(),
                                                        checkpoint.value().id, one_byte);
    CHECK(!byte_limited);
    CHECK(byte_limited.error().code == StatusCode::ResourceLimit);

    const auto saved = read_text(profile_path);
    auto unknown_schema = saved;
    const auto schema_position = unknown_schema.find("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    unknown_schema.replace(schema_position, std::string("\"schema\": 1").size(), "\"schema\": 99");
    write_text(profile_path, unknown_schema);
    auto incompatible = ReviewedDeploymentProfile::load(profile_path, history.value(), checkpoint.value(),
                                                        checkpoint.value().id);
    CHECK(!incompatible);
    CHECK(incompatible.error().code == StatusCode::IncompatibleFormat);
    auto corrupt = saved;
    auto tag_position = corrupt.find("\"authentication_tag\": \"");
    CHECK(tag_position != std::string::npos);
    tag_position += std::string("\"authentication_tag\": \"").size();
    corrupt[tag_position] = corrupt[tag_position] == '0' ? '1' : '0';
    write_text(profile_path, corrupt);
    auto corrupted = ReviewedDeploymentProfile::load(profile_path, history.value(), checkpoint.value(),
                                                     checkpoint.value().id);
    CHECK(!corrupted);
    CHECK(corrupted.error().code == StatusCode::CorruptData);
    write_text(profile_path, saved.substr(0, saved.size() / 2));
    auto truncated = ReviewedDeploymentProfile::load(profile_path, history.value(), checkpoint.value(),
                                                     checkpoint.value().id);
    CHECK(!truncated);
    CHECK(truncated.error().code == StatusCode::CorruptData);
    write_text(profile_path, saved);

    auto fixed_checkpoint =
        ServiceTrustCheckpoint::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                     "service_trust_checkpoint_schema1" / "checkpoint.json");
    CHECK(fixed_checkpoint);
    auto fixed_history = ServiceTrustHistory::open(
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "service_trust_history_schema2",
        fixed_checkpoint.value().root_bundle_id, fixed_checkpoint.value(), fixed_checkpoint.value().id);
    CHECK(fixed_history);

    const auto fixed_root =
        std::filesystem::path(RBFSAFE_TEST_DATA_DIR) / "reviewed_deployment_profile_schema1";
    auto fixed_deployment_checkpoint = ServiceTrustCheckpoint::load(fixed_root / "checkpoint.json");
    CHECK(fixed_deployment_checkpoint);
    CHECK(fixed_deployment_checkpoint.value().id ==
          "e64197b785ba1ae0c9f349adf5c26ac114c250088504950cc8073e25a6550d32");
    auto fixed_deployment_history = ServiceTrustHistory::open(
        fixed_root / "trust-history", fixed_deployment_checkpoint.value().root_bundle_id,
        fixed_deployment_checkpoint.value(), fixed_deployment_checkpoint.value().id);
    CHECK(fixed_deployment_history);
    auto fixed_reviewed = ReviewedDeploymentProfile::load(
        fixed_root / "profile.json", fixed_deployment_history.value(), fixed_deployment_checkpoint.value(),
        fixed_deployment_checkpoint.value().id);
    CHECK(fixed_reviewed);
    CHECK(fixed_reviewed.value().profile().id ==
          "c652aa75ca153ef429b6fff372b83c675a0ac3b68f055e9bb607108d543c7be4");
    CHECK(fixed_reviewed.value().approval_set().id ==
          "e56a57547437938c39ca903541cf7eb014d921a3254fc06e8a45c96e6d8cd9ae");

    std::filesystem::remove_all(temporary);
    return 0;
}
