#include <rbfsafe/modules/assurance.h>

#include "internal/certificate_utils.h"
#include "internal/deployment.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::uint32_t kMaximumApprovals = 100'000;

bool valid_text(std::string_view value) {
    return !value.empty() && value.size() <= kMaximumIdentifierBytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool valid_role(DeploymentReviewRole role) {
    return role >= DeploymentReviewRole::Safety && role <= DeploymentReviewRole::Security;
}

bool valid_status(DeploymentProfileAssessmentStatus status) {
    return status >= DeploymentProfileAssessmentStatus::Conformant &&
           status <= DeploymentProfileAssessmentStatus::Nonconformant;
}

bool valid_violation(DeploymentConstraintViolation violation) {
    return violation >= DeploymentConstraintViolation::DeploymentIdentityMismatch &&
           violation <= DeploymentConstraintViolation::AuthenticatedArtifactsRequired;
}

bool valid_lower_hex(std::string_view text, std::size_t characters) {
    return text.size() == characters && std::all_of(text.begin(), text.end(), [](char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
           });
}

auto approval_order(const DeploymentProfileApproval& approval) {
    return std::tie(approval.signer_service_id, approval.signer_key_id, approval.role, approval.id);
}

Result<void> validate_profile_approval_policy(const DeploymentProfile& profile,
                                              const std::vector<DeploymentProfileApproval>& approvals) {
    if (approvals.size() < profile.review_policy.minimum_approvals) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "deployment profile approval quorum is not satisfied", profile.id);
    }
    std::set<std::string> services;
    std::set<DeploymentReviewRole> roles;
    for (const auto& approval : approvals) {
        services.insert(approval.signer_service_id);
        roles.insert(approval.role);
    }
    if (profile.review_policy.require_distinct_services &&
        services.size() < profile.review_policy.minimum_approvals) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "deployment profile requires approvals from distinct services",
                                     profile.id);
    }
    for (const auto role : profile.review_policy.required_roles) {
        if (!roles.contains(role)) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "deployment profile required review role is missing",
                                         deployment_review_role_name(role));
        }
    }
    return Result<void>::success();
}

Result<void> verify_signing_key(std::string_view service_id, std::string_view key_id,
                                std::span<const std::byte> secret_key) {
    if (!valid_text(service_id) || !internal::valid_sha256(std::string(key_id)) ||
        secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "deployment review signing identity is invalid");
    }
    auto pair = ed25519_key_pair_from_seed(secret_key.first(kEd25519SeedBytes));
    if (!pair)
        return pair.error();
    if (!std::equal(pair.value().secret_key.begin(), pair.value().secret_key.end(), secret_key.begin())) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "Ed25519 secret key seed and public half do not match",
                                     std::string(key_id));
    }
    auto public_key = make_service_public_key(std::string(service_id), pair.value().public_key);
    if (!public_key)
        return public_key.error();
    if (public_key.value().id != key_id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "Ed25519 secret key does not match the deployment reviewer key ID",
                                     std::string(key_id));
    }
    return Result<void>::success();
}

} // namespace

namespace internal {
namespace {

Json runtime_constraints_json(const DeploymentRuntimeConstraints& constraints) {
    return Json::Object{
        {"maximum_command_latency_ns", std::to_string(constraints.maximum_command_latency_ns)},
        {"maximum_consecutive_missed_cycles",
         static_cast<double>(constraints.maximum_consecutive_missed_cycles)},
        {"maximum_control_period_ns", std::to_string(constraints.maximum_control_period_ns)},
        {"maximum_observation_age_ns", std::to_string(constraints.maximum_observation_age_ns)},
        {"require_authenticated_artifacts", constraints.require_authenticated_artifacts},
        {"require_fail_closed_transport", constraints.require_fail_closed_transport},
        {"require_runtime_monitor", constraints.require_runtime_monitor},
    };
}

Json review_policy_json(const DeploymentReviewPolicy& policy) {
    Json::Array roles;
    roles.reserve(policy.required_roles.size());
    for (const auto role : policy.required_roles)
        roles.emplace_back(static_cast<int>(role));
    return Json::Object{
        {"minimum_approvals", static_cast<double>(policy.minimum_approvals)},
        {"require_distinct_services", policy.require_distinct_services},
        {"required_roles", std::move(roles)},
    };
}

Json runtime_snapshot_json(const DeploymentRuntimeSnapshot& snapshot) {
    return Json::Object{
        {"authenticated_artifacts", snapshot.authenticated_artifacts},
        {"command_latency_ns", std::to_string(snapshot.command_latency_ns)},
        {"consecutive_missed_cycles", static_cast<double>(snapshot.consecutive_missed_cycles)},
        {"control_period_ns", std::to_string(snapshot.control_period_ns)},
        {"controller_digest", snapshot.controller_digest},
        {"deployment_id", snapshot.deployment_id},
        {"fail_closed_transport_active", snapshot.fail_closed_transport_active},
        {"format", "rbfsafe-deployment-runtime-snapshot"},
        {"observation_age_ns", std::to_string(snapshot.observation_age_ns)},
        {"platform_digest", snapshot.platform_digest},
        {"robot_digest", snapshot.robot_digest},
        {"runtime_digest", snapshot.runtime_digest},
        {"runtime_monitor_active", snapshot.runtime_monitor_active},
        {"schema", 1},
    };
}

} // namespace

std::string deployment_profile_identity(const DeploymentProfile& profile) {
    return sha256(Json(Json::Object{
                           {"controller_digest", profile.controller_digest},
                           {"deployment_id", profile.deployment_id},
                           {"format", "rbfsafe-deployment-profile"},
                           {"platform_digest", profile.platform_digest},
                           {"review_policy", review_policy_json(profile.review_policy)},
                           {"robot_digest", profile.robot_digest},
                           {"runtime_constraints", runtime_constraints_json(profile.runtime_constraints)},
                           {"runtime_digest", profile.runtime_digest},
                           {"schema", static_cast<double>(profile.storage_schema)},
                           {"trust_bundle_id", profile.trust_bundle_id},
                           {"trust_bundle_sequence", std::to_string(profile.trust_bundle_sequence)},
                           {"trust_checkpoint_id", profile.trust_checkpoint_id},
                           {"trust_root_bundle_id", profile.trust_root_bundle_id},
                       })
                      .dump(false));
}

std::string deployment_profile_approval_message(const DeploymentProfileApproval& approval) {
    return std::string("rbfsafe-deployment-profile-approval-v1\n") +
           Json(Json::Object{
                    {"algorithm", static_cast<int>(approval.algorithm)},
                    {"format", "rbfsafe-deployment-profile-approval"},
                    {"profile_id", approval.profile_id},
                    {"role", static_cast<int>(approval.role)},
                    {"schema", 1},
                    {"signer_key_id", approval.signer_key_id},
                    {"signer_service_id", approval.signer_service_id},
                })
               .dump(false);
}

std::string deployment_profile_approval_identity(const DeploymentProfileApproval& approval) {
    return sha256(std::string("rbfsafe-deployment-profile-approval-identity-v1\n") +
                  deployment_profile_approval_message(approval) + "\n" + approval.authentication_tag);
}

std::string deployment_profile_approval_set_identity(const DeploymentProfileApprovalSet& approval_set) {
    Json::Array approval_ids;
    approval_ids.reserve(approval_set.approvals.size());
    for (const auto& approval : approval_set.approvals)
        approval_ids.emplace_back(approval.id);
    return sha256(Json(Json::Object{
                           {"approval_ids", std::move(approval_ids)},
                           {"format", "rbfsafe-deployment-profile-approval-set"},
                           {"profile_id", approval_set.profile_id},
                           {"schema", 1},
                       })
                      .dump(false));
}

std::string deployment_runtime_snapshot_identity(const DeploymentRuntimeSnapshot& snapshot) {
    return sha256(runtime_snapshot_json(snapshot).dump(false));
}

std::string deployment_profile_assessment_identity(const DeploymentProfileAssessment& assessment) {
    Json::Array violations;
    violations.reserve(assessment.violations.size());
    for (const auto violation : assessment.violations)
        violations.emplace_back(static_cast<int>(violation));
    return sha256(Json(Json::Object{
                           {"approval_set_id", assessment.approval_set_id},
                           {"evidence", static_cast<int>(assessment.evidence)},
                           {"format", "rbfsafe-deployment-profile-assessment"},
                           {"profile_id", assessment.profile_id},
                           {"runtime_snapshot_id", assessment.runtime_snapshot_id},
                           {"schema", 1},
                           {"status", static_cast<int>(assessment.status)},
                           {"violations", std::move(violations)},
                       })
                      .dump(false));
}

} // namespace internal

bool valid_deployment_review_policy(const DeploymentReviewPolicy& policy) {
    if (policy.minimum_approvals == 0 || policy.minimum_approvals > kMaximumApprovals)
        return false;
    return std::all_of(policy.required_roles.begin(), policy.required_roles.end(), valid_role) &&
           std::is_sorted(policy.required_roles.begin(), policy.required_roles.end()) &&
           std::adjacent_find(policy.required_roles.begin(), policy.required_roles.end()) ==
               policy.required_roles.end();
}

bool valid_deployment_runtime_constraints(const DeploymentRuntimeConstraints& constraints) {
    return constraints.maximum_observation_age_ns > 0 && constraints.maximum_command_latency_ns > 0 &&
           constraints.maximum_control_period_ns > 0;
}

Result<DeploymentProfile> DeploymentProfile::create(DeploymentProfileInput input) {
    std::sort(input.review_policy.required_roles.begin(), input.review_policy.required_roles.end());
    input.review_policy.required_roles.erase(
        std::unique(input.review_policy.required_roles.begin(), input.review_policy.required_roles.end()),
        input.review_policy.required_roles.end());
    DeploymentProfile result;
    result.storage_schema = 1;
    result.deployment_id = std::move(input.deployment_id);
    result.robot_digest = std::move(input.robot_digest);
    result.controller_digest = std::move(input.controller_digest);
    result.platform_digest = std::move(input.platform_digest);
    result.runtime_digest = std::move(input.runtime_digest);
    result.trust_root_bundle_id = std::move(input.trust_root_bundle_id);
    result.trust_checkpoint_id = std::move(input.trust_checkpoint_id);
    result.trust_bundle_id = std::move(input.trust_bundle_id);
    result.trust_bundle_sequence = input.trust_bundle_sequence;
    result.runtime_constraints = input.runtime_constraints;
    result.review_policy = std::move(input.review_policy);
    result.id = internal::deployment_profile_identity(result);
    if (!result.valid()) {
        return Result<DeploymentProfile>::failure(StatusCode::InvalidArgument,
                                                  "deployment profile input is invalid");
    }
    return result;
}

bool DeploymentProfile::valid() const {
    return storage_schema == 1 && valid_text(deployment_id) && internal::valid_sha256(robot_digest) &&
           internal::valid_sha256(controller_digest) && internal::valid_sha256(platform_digest) &&
           internal::valid_sha256(runtime_digest) && internal::valid_sha256(trust_root_bundle_id) &&
           internal::valid_sha256(trust_checkpoint_id) && internal::valid_sha256(trust_bundle_id) &&
           trust_bundle_sequence > 0 && valid_deployment_runtime_constraints(runtime_constraints) &&
           valid_deployment_review_policy(review_policy) && internal::valid_sha256(id) &&
           id == internal::deployment_profile_identity(*this);
}

bool valid_deployment_profile_approval(const DeploymentProfileApproval& approval) {
    return internal::valid_sha256(approval.id) && internal::valid_sha256(approval.profile_id) &&
           valid_text(approval.signer_service_id) && internal::valid_sha256(approval.signer_key_id) &&
           valid_role(approval.role) && approval.algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           valid_lower_hex(approval.authentication_tag, kEd25519SignatureBytes * 2) &&
           approval.id == internal::deployment_profile_approval_identity(approval);
}

bool valid_deployment_profile_approval_set(const DeploymentProfileApprovalSet& approval_set) {
    if (!internal::valid_sha256(approval_set.id) || !internal::valid_sha256(approval_set.profile_id) ||
        approval_set.approvals.empty() || approval_set.approvals.size() > kMaximumApprovals ||
        !std::all_of(approval_set.approvals.begin(), approval_set.approvals.end(),
                     valid_deployment_profile_approval) ||
        !std::is_sorted(approval_set.approvals.begin(), approval_set.approvals.end(),
                        [](const auto& first, const auto& second) {
                            return approval_order(first) < approval_order(second);
                        })) {
        return false;
    }
    std::set<std::pair<std::string, std::string>> signers;
    for (const auto& approval : approval_set.approvals) {
        if (approval.profile_id != approval_set.profile_id ||
            !signers.emplace(approval.signer_service_id, approval.signer_key_id).second) {
            return false;
        }
    }
    return approval_set.id == internal::deployment_profile_approval_set_identity(approval_set);
}

Result<DeploymentProfileApproval>
sign_deployment_profile_approval(const DeploymentProfile& profile, std::string signer_service_id,
                                 std::string signer_key_id, DeploymentReviewRole role,
                                 std::span<const std::byte> ed25519_secret_key) {
    if (!profile.valid() || !valid_role(role)) {
        return Result<DeploymentProfileApproval>::failure(StatusCode::InvalidArgument,
                                                          "deployment profile approval input is invalid");
    }
    auto key_check = verify_signing_key(signer_service_id, signer_key_id, ed25519_secret_key);
    if (!key_check)
        return key_check.error();
    DeploymentProfileApproval result;
    result.profile_id = profile.id;
    result.signer_service_id = std::move(signer_service_id);
    result.signer_key_id = std::move(signer_key_id);
    result.role = role;
    result.algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    const auto message = internal::deployment_profile_approval_message(result);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    result.authentication_tag = internal::encode_hex(signature.value());
    result.id = internal::deployment_profile_approval_identity(result);
    return result;
}

Result<DeploymentProfileApprovalSet>
assemble_deployment_profile_approvals(const DeploymentProfile& profile,
                                      std::vector<DeploymentProfileApproval> approvals) {
    if (!profile.valid() || approvals.empty() || approvals.size() > kMaximumApprovals) {
        return Result<DeploymentProfileApprovalSet>::failure(
            StatusCode::InvalidArgument, "deployment profile approval set input is invalid");
    }
    std::sort(approvals.begin(), approvals.end(), [](const auto& first, const auto& second) {
        return approval_order(first) < approval_order(second);
    });
    DeploymentProfileApprovalSet result;
    result.profile_id = profile.id;
    result.approvals = std::move(approvals);
    result.id = internal::deployment_profile_approval_set_identity(result);
    if (!valid_deployment_profile_approval_set(result)) {
        return Result<DeploymentProfileApprovalSet>::failure(
            StatusCode::InvalidArgument, "deployment profile approvals are invalid or duplicated");
    }
    auto policy = validate_profile_approval_policy(profile, result.approvals);
    if (!policy)
        return policy.error();
    return result;
}

Result<void> verify_deployment_profile_approvals(const DeploymentProfile& profile,
                                                 const DeploymentProfileApprovalSet& approval_set,
                                                 const ServiceTrustBundle& trust_bundle) {
    if (!profile.valid() || !approval_set.approvals.size() || !trust_bundle.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "deployment profile approval verification input is invalid");
    }
    if (!valid_deployment_profile_approval_set(approval_set)) {
        return Result<void>::failure(StatusCode::CorruptData,
                                     "deployment profile approval-set identity is invalid");
    }
    if (approval_set.profile_id != profile.id || profile.trust_bundle_id != trust_bundle.id() ||
        profile.trust_bundle_sequence != trust_bundle.sequence()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "deployment profile review trust binding is invalid", profile.id);
    }
    auto policy = validate_profile_approval_policy(profile, approval_set.approvals);
    if (!policy)
        return policy;
    for (const auto& approval : approval_set.approvals) {
        auto trusted =
            trusted_service_public_key(trust_bundle, approval.signer_service_id, approval.signer_key_id,
                                       ArtifactTransferOperation::Publish, profile.trust_bundle_sequence);
        if (!trusted)
            return trusted.error();
        auto signature = internal::decode_hex(approval.authentication_tag, kEd25519SignatureBytes);
        if (!signature)
            return signature.error();
        const auto message = internal::deployment_profile_approval_message(approval);
        auto verified = ed25519_verify(std::as_bytes(std::span(message.data(), message.size())),
                                       signature.value(), trusted.value().public_key);
        if (!verified)
            return verified.error();
    }
    return Result<void>::success();
}

bool valid_deployment_runtime_snapshot(const DeploymentRuntimeSnapshot& snapshot) {
    return valid_text(snapshot.deployment_id) && internal::valid_sha256(snapshot.robot_digest) &&
           internal::valid_sha256(snapshot.controller_digest) &&
           internal::valid_sha256(snapshot.platform_digest) &&
           internal::valid_sha256(snapshot.runtime_digest) && snapshot.control_period_ns > 0;
}

bool DeploymentProfileAssessment::valid() const {
    if (!internal::valid_sha256(id) || !internal::valid_sha256(profile_id) ||
        !internal::valid_sha256(approval_set_id) || !internal::valid_sha256(runtime_snapshot_id) ||
        !valid_status(status) || evidence != EvidenceLevel::Unknown ||
        !std::all_of(violations.begin(), violations.end(), valid_violation) ||
        !std::is_sorted(violations.begin(), violations.end()) ||
        std::adjacent_find(violations.begin(), violations.end()) != violations.end()) {
        return false;
    }
    if ((status == DeploymentProfileAssessmentStatus::Conformant) != violations.empty())
        return false;
    return id == internal::deployment_profile_assessment_identity(*this);
}

Result<ReviewedDeploymentProfile>
ReviewedDeploymentProfile::create(DeploymentProfile profile, DeploymentProfileApprovalSet approval_set,
                                  const ServiceTrustHistory& trust_history,
                                  const ServiceTrustCheckpoint& trust_checkpoint,
                                  const std::string& expected_checkpoint_id) {
    if (!profile.valid() || !valid_deployment_profile_approval_set(approval_set) || !trust_history.valid() ||
        !trust_checkpoint.valid() || !internal::valid_sha256(expected_checkpoint_id)) {
        return Result<ReviewedDeploymentProfile>::failure(StatusCode::InvalidArgument,
                                                          "reviewed deployment profile input is invalid");
    }
    auto checkpoint_verified =
        verify_service_trust_checkpoint(trust_history, trust_checkpoint, expected_checkpoint_id);
    if (!checkpoint_verified)
        return checkpoint_verified.error();
    if (profile.trust_root_bundle_id != trust_checkpoint.root_bundle_id ||
        profile.trust_checkpoint_id != trust_checkpoint.id ||
        profile.trust_bundle_id != trust_checkpoint.head_bundle_id ||
        profile.trust_bundle_sequence != trust_checkpoint.head_sequence) {
        return Result<ReviewedDeploymentProfile>::failure(
            StatusCode::IdentityMismatch,
            "deployment profile is bound to another trust root, checkpoint, or head", profile.id);
    }
    auto bundle = trust_history.bundle(profile.trust_bundle_id);
    if (!bundle)
        return bundle.error();
    auto approvals_verified = verify_deployment_profile_approvals(profile, approval_set, bundle.value());
    if (!approvals_verified)
        return approvals_verified.error();
    ReviewedDeploymentProfile result;
    result.profile_ = std::move(profile);
    result.approval_set_ = std::move(approval_set);
    return result;
}

bool ReviewedDeploymentProfile::valid() const {
    return profile_.valid() && valid_deployment_profile_approval_set(approval_set_) &&
           approval_set_.profile_id == profile_.id;
}

Result<DeploymentProfileAssessment>
ReviewedDeploymentProfile::assess(const DeploymentRuntimeSnapshot& snapshot) const {
    if (!valid() || !valid_deployment_runtime_snapshot(snapshot)) {
        return Result<DeploymentProfileAssessment>::failure(StatusCode::InvalidArgument,
                                                            "deployment runtime assessment input is invalid");
    }
    DeploymentProfileAssessment result;
    result.profile_id = profile_.id;
    result.approval_set_id = approval_set_.id;
    result.runtime_snapshot_id = internal::deployment_runtime_snapshot_identity(snapshot);
    if (snapshot.deployment_id != profile_.deployment_id)
        result.violations.push_back(DeploymentConstraintViolation::DeploymentIdentityMismatch);
    if (snapshot.robot_digest != profile_.robot_digest)
        result.violations.push_back(DeploymentConstraintViolation::RobotIdentityMismatch);
    if (snapshot.controller_digest != profile_.controller_digest)
        result.violations.push_back(DeploymentConstraintViolation::ControllerIdentityMismatch);
    if (snapshot.platform_digest != profile_.platform_digest)
        result.violations.push_back(DeploymentConstraintViolation::PlatformIdentityMismatch);
    if (snapshot.runtime_digest != profile_.runtime_digest)
        result.violations.push_back(DeploymentConstraintViolation::RuntimeIdentityMismatch);
    if (snapshot.observation_age_ns > profile_.runtime_constraints.maximum_observation_age_ns)
        result.violations.push_back(DeploymentConstraintViolation::ObservationAgeExceeded);
    if (snapshot.command_latency_ns > profile_.runtime_constraints.maximum_command_latency_ns)
        result.violations.push_back(DeploymentConstraintViolation::CommandLatencyExceeded);
    if (snapshot.control_period_ns > profile_.runtime_constraints.maximum_control_period_ns)
        result.violations.push_back(DeploymentConstraintViolation::ControlPeriodExceeded);
    if (snapshot.consecutive_missed_cycles > profile_.runtime_constraints.maximum_consecutive_missed_cycles) {
        result.violations.push_back(DeploymentConstraintViolation::MissedCycleLimitExceeded);
    }
    if (profile_.runtime_constraints.require_runtime_monitor && !snapshot.runtime_monitor_active)
        result.violations.push_back(DeploymentConstraintViolation::RuntimeMonitorRequired);
    if (profile_.runtime_constraints.require_fail_closed_transport &&
        !snapshot.fail_closed_transport_active) {
        result.violations.push_back(DeploymentConstraintViolation::FailClosedTransportRequired);
    }
    if (profile_.runtime_constraints.require_authenticated_artifacts && !snapshot.authenticated_artifacts) {
        result.violations.push_back(DeploymentConstraintViolation::AuthenticatedArtifactsRequired);
    }
    result.status = result.violations.empty() ? DeploymentProfileAssessmentStatus::Conformant
                                              : DeploymentProfileAssessmentStatus::Nonconformant;
    result.evidence = EvidenceLevel::Unknown;
    result.id = internal::deployment_profile_assessment_identity(result);
    return result;
}

Result<void> ReviewedDeploymentProfile::save(const std::filesystem::path& path,
                                             const SaveOptions& options) const {
    return save_reviewed_deployment_profile(*this, path, options);
}

Result<ReviewedDeploymentProfile>
ReviewedDeploymentProfile::load(const std::filesystem::path& path, const ServiceTrustHistory& trust_history,
                                const ServiceTrustCheckpoint& trust_checkpoint,
                                const std::string& expected_checkpoint_id,
                                const ReviewedDeploymentProfileLoadOptions& options) {
    return load_reviewed_deployment_profile(path, trust_history, trust_checkpoint, expected_checkpoint_id,
                                            options);
}

std::string deployment_review_role_name(DeploymentReviewRole role) {
    switch (role) {
    case DeploymentReviewRole::Safety:
        return "safety";
    case DeploymentReviewRole::Controls:
        return "controls";
    case DeploymentReviewRole::Operations:
        return "operations";
    case DeploymentReviewRole::Security:
        return "security";
    }
    return "unknown";
}

std::string deployment_constraint_violation_name(DeploymentConstraintViolation violation) {
    switch (violation) {
    case DeploymentConstraintViolation::DeploymentIdentityMismatch:
        return "deployment_identity_mismatch";
    case DeploymentConstraintViolation::RobotIdentityMismatch:
        return "robot_identity_mismatch";
    case DeploymentConstraintViolation::ControllerIdentityMismatch:
        return "controller_identity_mismatch";
    case DeploymentConstraintViolation::PlatformIdentityMismatch:
        return "platform_identity_mismatch";
    case DeploymentConstraintViolation::RuntimeIdentityMismatch:
        return "runtime_identity_mismatch";
    case DeploymentConstraintViolation::ObservationAgeExceeded:
        return "observation_age_exceeded";
    case DeploymentConstraintViolation::CommandLatencyExceeded:
        return "command_latency_exceeded";
    case DeploymentConstraintViolation::ControlPeriodExceeded:
        return "control_period_exceeded";
    case DeploymentConstraintViolation::MissedCycleLimitExceeded:
        return "missed_cycle_limit_exceeded";
    case DeploymentConstraintViolation::RuntimeMonitorRequired:
        return "runtime_monitor_required";
    case DeploymentConstraintViolation::FailClosedTransportRequired:
        return "fail_closed_transport_required";
    case DeploymentConstraintViolation::AuthenticatedArtifactsRequired:
        return "authenticated_artifacts_required";
    }
    return "unknown";
}

std::string deployment_profile_assessment_status_name(DeploymentProfileAssessmentStatus status) {
    switch (status) {
    case DeploymentProfileAssessmentStatus::Conformant:
        return "conformant";
    case DeploymentProfileAssessmentStatus::Nonconformant:
        return "nonconformant";
    }
    return "unknown";
}

} // namespace rbfsafe
