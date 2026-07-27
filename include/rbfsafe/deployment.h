#pragma once

#include <rbfsafe/identity.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class DeploymentReviewRole : std::uint8_t {
    Safety = 0,
    Controls = 1,
    Operations = 2,
    Security = 3,
};

struct DeploymentReviewPolicy {
    std::uint32_t minimum_approvals = 1;
    bool require_distinct_services = false;
    std::vector<DeploymentReviewRole> required_roles;
};

struct DeploymentRuntimeConstraints {
    std::uint64_t maximum_observation_age_ns = 100'000'000;
    std::uint64_t maximum_command_latency_ns = 100'000'000;
    std::uint64_t maximum_control_period_ns = 100'000'000;
    std::uint32_t maximum_consecutive_missed_cycles = 0;
    bool require_runtime_monitor = true;
    bool require_fail_closed_transport = true;
    bool require_authenticated_artifacts = true;
};

bool valid_deployment_review_policy(const DeploymentReviewPolicy& policy);
bool valid_deployment_runtime_constraints(const DeploymentRuntimeConstraints& constraints);

struct DeploymentProfileInput {
    std::string deployment_id;
    std::string robot_digest;
    std::string controller_digest;
    std::string platform_digest;
    std::string runtime_digest;
    std::string trust_root_bundle_id;
    std::string trust_checkpoint_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    DeploymentRuntimeConstraints runtime_constraints;
    DeploymentReviewPolicy review_policy;
};

struct DeploymentProfile {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string deployment_id;
    std::string robot_digest;
    std::string controller_digest;
    std::string platform_digest;
    std::string runtime_digest;
    std::string trust_root_bundle_id;
    std::string trust_checkpoint_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    DeploymentRuntimeConstraints runtime_constraints;
    DeploymentReviewPolicy review_policy;

    static Result<DeploymentProfile> create(DeploymentProfileInput input);
    bool valid() const;
};

struct DeploymentProfileApproval {
    std::string id;
    std::string profile_id;
    std::string signer_service_id;
    std::string signer_key_id;
    DeploymentReviewRole role = DeploymentReviewRole::Safety;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_deployment_profile_approval(const DeploymentProfileApproval& approval);

struct DeploymentProfileApprovalSet {
    std::string id;
    std::string profile_id;
    std::vector<DeploymentProfileApproval> approvals;
};

bool valid_deployment_profile_approval_set(const DeploymentProfileApprovalSet& approval_set);

Result<DeploymentProfileApproval>
sign_deployment_profile_approval(const DeploymentProfile& profile, std::string signer_service_id,
                                 std::string signer_key_id, DeploymentReviewRole role,
                                 std::span<const std::byte> ed25519_secret_key);

Result<DeploymentProfileApprovalSet>
assemble_deployment_profile_approvals(const DeploymentProfile& profile,
                                      std::vector<DeploymentProfileApproval> approvals);

Result<void> verify_deployment_profile_approvals(const DeploymentProfile& profile,
                                                 const DeploymentProfileApprovalSet& approval_set,
                                                 const ServiceTrustBundle& trust_bundle);

struct DeploymentRuntimeSnapshot {
    std::string deployment_id;
    std::string robot_digest;
    std::string controller_digest;
    std::string platform_digest;
    std::string runtime_digest;
    std::uint64_t observation_age_ns = 0;
    std::uint64_t command_latency_ns = 0;
    std::uint64_t control_period_ns = 0;
    std::uint32_t consecutive_missed_cycles = 0;
    bool runtime_monitor_active = false;
    bool fail_closed_transport_active = false;
    bool authenticated_artifacts = false;
};

bool valid_deployment_runtime_snapshot(const DeploymentRuntimeSnapshot& snapshot);

enum class DeploymentConstraintViolation : std::uint8_t {
    DeploymentIdentityMismatch = 0,
    RobotIdentityMismatch = 1,
    ControllerIdentityMismatch = 2,
    PlatformIdentityMismatch = 3,
    RuntimeIdentityMismatch = 4,
    ObservationAgeExceeded = 5,
    CommandLatencyExceeded = 6,
    ControlPeriodExceeded = 7,
    MissedCycleLimitExceeded = 8,
    RuntimeMonitorRequired = 9,
    FailClosedTransportRequired = 10,
    AuthenticatedArtifactsRequired = 11,
};

enum class DeploymentProfileAssessmentStatus : std::uint8_t {
    Conformant = 0,
    Nonconformant = 1,
};

struct DeploymentProfileAssessment {
    std::string id;
    std::string profile_id;
    std::string approval_set_id;
    std::string runtime_snapshot_id;
    DeploymentProfileAssessmentStatus status = DeploymentProfileAssessmentStatus::Nonconformant;
    std::vector<DeploymentConstraintViolation> violations;
    EvidenceLevel evidence = EvidenceLevel::Unknown;

    bool valid() const;
    bool authorizes_execution() const noexcept { return false; }
};

struct ReviewedDeploymentProfileLoadOptions {
    std::size_t maximum_approvals = 100'000;
    std::size_t maximum_required_roles = 32;
    std::uintmax_t maximum_payload_bytes = 4'194'304ULL;
};

class ReviewedDeploymentProfile;
Result<void> save_reviewed_deployment_profile(const ReviewedDeploymentProfile& reviewed,
                                              const std::filesystem::path& path, const SaveOptions& options);
Result<ReviewedDeploymentProfile>
load_reviewed_deployment_profile(const std::filesystem::path& path, const ServiceTrustHistory& trust_history,
                                 const ServiceTrustCheckpoint& trust_checkpoint,
                                 const std::string& expected_checkpoint_id,
                                 const ReviewedDeploymentProfileLoadOptions& options);

class ReviewedDeploymentProfile {
  public:
    static Result<ReviewedDeploymentProfile> create(DeploymentProfile profile,
                                                    DeploymentProfileApprovalSet approval_set,
                                                    const ServiceTrustHistory& trust_history,
                                                    const ServiceTrustCheckpoint& trust_checkpoint,
                                                    const std::string& expected_checkpoint_id);

    const DeploymentProfile& profile() const noexcept { return profile_; }
    const DeploymentProfileApprovalSet& approval_set() const noexcept { return approval_set_; }
    bool valid() const;
    bool authorizes_execution() const noexcept { return false; }

    Result<DeploymentProfileAssessment> assess(const DeploymentRuntimeSnapshot& snapshot) const;

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<ReviewedDeploymentProfile> load(const std::filesystem::path& path,
                                                  const ServiceTrustHistory& trust_history,
                                                  const ServiceTrustCheckpoint& trust_checkpoint,
                                                  const std::string& expected_checkpoint_id,
                                                  const ReviewedDeploymentProfileLoadOptions& options = {});

  private:
    friend Result<void> save_reviewed_deployment_profile(const ReviewedDeploymentProfile&,
                                                         const std::filesystem::path&, const SaveOptions&);
    friend Result<ReviewedDeploymentProfile>
    load_reviewed_deployment_profile(const std::filesystem::path&, const ServiceTrustHistory&,
                                     const ServiceTrustCheckpoint&, const std::string&,
                                     const ReviewedDeploymentProfileLoadOptions&);

    DeploymentProfile profile_;
    DeploymentProfileApprovalSet approval_set_;
};

std::string deployment_review_role_name(DeploymentReviewRole role);
std::string deployment_constraint_violation_name(DeploymentConstraintViolation violation);
std::string deployment_profile_assessment_status_name(DeploymentProfileAssessmentStatus status);

} // namespace rbfsafe
