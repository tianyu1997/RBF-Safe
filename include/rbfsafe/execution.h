#pragma once

#include <rbfsafe/deployment.h>
#include <rbfsafe/trajectory.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class ExecutionEndpointRole : std::uint8_t {
    Controller = 0,
    RuntimeMonitor = 1,
};

struct ExecutionEndpointKey {
    std::string id;
    std::string service_id;
    ExecutionEndpointRole role = ExecutionEndpointRole::Controller;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::array<std::byte, kEd25519PublicKeyBytes> public_key{};
};

bool valid_execution_endpoint_key(const ExecutionEndpointKey& key);
Result<ExecutionEndpointKey> make_execution_endpoint_key(std::string service_id, ExecutionEndpointRole role,
                                                         std::span<const std::byte> ed25519_public_key);

struct ExecutionCommand {
    std::uint64_t index = 0;
    std::uint64_t scheduled_offset_ns = 0;
    Configuration configuration;
};

struct ExecutionCommandSequence {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string atlas_id;
    std::string robot_digest;
    std::string scene_digest;
    std::string connectivity_certificate_id;
    std::size_t dimension = 0;
    std::vector<ExecutionCommand> commands;
    std::vector<RegionId> region_sequence;

    static Result<ExecutionCommandSequence> create(const SafeAtlas& atlas,
                                                   std::vector<Configuration> configurations,
                                                   std::vector<std::uint64_t> scheduled_offsets_ns,
                                                   const TrajectoryAuditOptions& options = {});

    bool valid() const;
    Result<void> verify_compatible(const SafeAtlas& atlas, const TrajectoryAuditOptions& options = {}) const;
};

struct ExecutionSessionLimits {
    std::uint64_t maximum_start_delay_ns = 10'000'000;
    std::uint64_t maximum_duration_ns = 100'000'000;
    std::size_t maximum_commands = 100'000;
};

bool valid_execution_session_limits(const ExecutionSessionLimits& limits);

struct ExecutionSessionRequestInput {
    std::string session_nonce;
    ExecutionEndpointKey controller;
    ExecutionEndpointKey runtime_monitor;
    ExecutionSessionLimits limits;
};

struct ExecutionSessionRequest {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string session_nonce;
    std::string reviewed_profile_id;
    std::string reviewed_profile_approval_set_id;
    std::string trust_root_bundle_id;
    std::string trust_checkpoint_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string command_sequence_id;
    std::string atlas_id;
    std::string robot_digest;
    std::string scene_digest;
    std::size_t command_count = 0;
    ExecutionEndpointKey controller;
    ExecutionEndpointKey runtime_monitor;
    ExecutionSessionLimits limits;

    static Result<ExecutionSessionRequest> create(const ReviewedDeploymentProfile& reviewed,
                                                  const ExecutionCommandSequence& command_sequence,
                                                  ExecutionSessionRequestInput input);

    bool valid() const;
};

struct ExecutionSessionApproval {
    std::string id;
    std::string request_id;
    std::string signer_service_id;
    std::string signer_key_id;
    DeploymentReviewRole role = DeploymentReviewRole::Safety;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_execution_session_approval(const ExecutionSessionApproval& approval);

struct ExecutionSessionApprovalSet {
    std::string id;
    std::string request_id;
    std::vector<ExecutionSessionApproval> approvals;
};

bool valid_execution_session_approval_set(const ExecutionSessionApprovalSet& approval_set);

Result<ExecutionSessionApproval>
sign_execution_session_approval(const ExecutionSessionRequest& request,
                                const DeploymentProfileApproval& profile_approval,
                                std::span<const std::byte> ed25519_secret_key);

Result<ExecutionSessionApprovalSet>
assemble_execution_session_approvals(const ExecutionSessionRequest& request,
                                     const ReviewedDeploymentProfile& reviewed,
                                     std::vector<ExecutionSessionApproval> approvals);

Result<void> verify_execution_session_approvals(const ExecutionSessionRequest& request,
                                                const ReviewedDeploymentProfile& reviewed,
                                                const ExecutionSessionApprovalSet& approval_set,
                                                const ServiceTrustBundle& trust_bundle);

struct ExecutionControllerAcknowledgement {
    std::string id;
    std::string request_id;
    std::string command_sequence_id;
    std::string controller_service_id;
    std::string controller_key_id;
    std::size_t accepted_command_count = 0;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_execution_controller_acknowledgement(const ExecutionControllerAcknowledgement& acknowledgement);

Result<ExecutionControllerAcknowledgement>
sign_execution_controller_acknowledgement(const ExecutionSessionRequest& request,
                                          std::span<const std::byte> ed25519_secret_key);

Result<void>
verify_execution_controller_acknowledgement(const ExecutionSessionRequest& request,
                                            const ExecutionControllerAcknowledgement& acknowledgement);

enum class ExecutionMonitorState : std::uint8_t {
    ArmedCertifiedSequence = 0,
    Disarmed = 1,
    Fault = 2,
};

struct ExecutionRuntimeObservationInput {
    DeploymentRuntimeSnapshot runtime;
    std::uint64_t observation_sequence = 1;
    std::uint64_t observed_monotonic_ns = 0;
    ExecutionMonitorState monitor_state = ExecutionMonitorState::Disarmed;
};

struct ExecutionRuntimeObservation {
    std::string id;
    std::string request_id;
    std::string command_sequence_id;
    DeploymentRuntimeSnapshot runtime;
    std::uint64_t observation_sequence = 0;
    std::uint64_t observed_monotonic_ns = 0;
    ExecutionMonitorState monitor_state = ExecutionMonitorState::Disarmed;

    static Result<ExecutionRuntimeObservation> create(const ExecutionSessionRequest& request,
                                                      ExecutionRuntimeObservationInput input);

    bool valid() const;
};

struct ExecutionMonitorAcknowledgement {
    std::string id;
    ExecutionRuntimeObservation observation;
    std::string monitor_service_id;
    std::string monitor_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_execution_monitor_acknowledgement(const ExecutionMonitorAcknowledgement& acknowledgement);

Result<ExecutionMonitorAcknowledgement>
sign_execution_monitor_acknowledgement(const ExecutionSessionRequest& request,
                                       ExecutionRuntimeObservation observation,
                                       std::span<const std::byte> ed25519_secret_key);

Result<void> verify_execution_monitor_acknowledgement(const ExecutionSessionRequest& request,
                                                      const ExecutionMonitorAcknowledgement& acknowledgement);

struct ExecutionCommandAuthorization {
    std::string id;
    std::string session_id;
    std::string command_sequence_id;
    std::uint64_t command_index = 0;
    std::string command_digest;
    std::uint64_t dispatch_monotonic_ns = 0;
    std::uint64_t valid_from_monotonic_ns = 0;
    std::uint64_t valid_through_monotonic_ns = 0;
    EvidenceLevel evidence = EvidenceLevel::RuntimeExecutable;

    bool valid() const;
    bool open_ended() const noexcept { return false; }
};

struct BoundedExecutionSessionLoadOptions {
    std::size_t maximum_commands = 100'000;
    std::size_t maximum_dimension = 1'000;
    std::size_t maximum_region_sequence = 1'000'000;
    std::size_t maximum_approvals = 100'000;
    std::uintmax_t maximum_payload_bytes = 67'108'864ULL;
};

class BoundedExecutionSession;
Result<void> save_bounded_execution_session(const BoundedExecutionSession& session,
                                            const std::filesystem::path& path, const SaveOptions& options);
Result<BoundedExecutionSession>
load_bounded_execution_session(const std::filesystem::path& path, const ReviewedDeploymentProfile& reviewed,
                               const ServiceTrustHistory& trust_history,
                               const ServiceTrustCheckpoint& trust_checkpoint,
                               const std::string& expected_checkpoint_id, const SafeAtlas& atlas,
                               const BoundedExecutionSessionLoadOptions& options);

class BoundedExecutionSession {
  public:
    static Result<BoundedExecutionSession>
    create(ExecutionSessionRequest request, ExecutionCommandSequence command_sequence,
           ExecutionSessionApprovalSet approval_set,
           ExecutionControllerAcknowledgement controller_acknowledgement,
           ExecutionMonitorAcknowledgement monitor_acknowledgement, const ReviewedDeploymentProfile& reviewed,
           const ServiceTrustBundle& trust_bundle, const SafeAtlas& atlas);

    const std::string& id() const noexcept { return id_; }
    const ExecutionSessionRequest& request() const noexcept { return request_; }
    const ExecutionCommandSequence& command_sequence() const noexcept { return command_sequence_; }
    const ExecutionSessionApprovalSet& approval_set() const noexcept { return approval_set_; }
    const ExecutionControllerAcknowledgement& controller_acknowledgement() const noexcept {
        return controller_acknowledgement_;
    }
    const ExecutionMonitorAcknowledgement& monitor_acknowledgement() const noexcept {
        return monitor_acknowledgement_;
    }
    std::uint64_t valid_from_monotonic_ns() const noexcept { return valid_from_monotonic_ns_; }
    std::uint64_t start_deadline_monotonic_ns() const noexcept { return start_deadline_monotonic_ns_; }
    std::uint64_t valid_through_monotonic_ns() const noexcept { return valid_through_monotonic_ns_; }

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<std::optional<ExecutionCommandAuthorization>>
    authorize_command(std::uint64_t command_index, std::span<const double> configuration,
                      std::uint64_t dispatch_monotonic_ns) const;

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<BoundedExecutionSession>
    load(const std::filesystem::path& path, const ReviewedDeploymentProfile& reviewed,
         const ServiceTrustHistory& trust_history, const ServiceTrustCheckpoint& trust_checkpoint,
         const std::string& expected_checkpoint_id, const SafeAtlas& atlas,
         const BoundedExecutionSessionLoadOptions& options = {});

  private:
    friend Result<void> save_bounded_execution_session(const BoundedExecutionSession&,
                                                       const std::filesystem::path&, const SaveOptions&);
    friend Result<BoundedExecutionSession>
    load_bounded_execution_session(const std::filesystem::path&, const ReviewedDeploymentProfile&,
                                   const ServiceTrustHistory&, const ServiceTrustCheckpoint&,
                                   const std::string&, const SafeAtlas&,
                                   const BoundedExecutionSessionLoadOptions&);

    std::string id_;
    ExecutionSessionRequest request_;
    ExecutionCommandSequence command_sequence_;
    ExecutionSessionApprovalSet approval_set_;
    ExecutionControllerAcknowledgement controller_acknowledgement_;
    ExecutionMonitorAcknowledgement monitor_acknowledgement_;
    std::uint64_t valid_from_monotonic_ns_ = 0;
    std::uint64_t start_deadline_monotonic_ns_ = 0;
    std::uint64_t valid_through_monotonic_ns_ = 0;
};

std::string execution_endpoint_role_name(ExecutionEndpointRole role);
std::string execution_monitor_state_name(ExecutionMonitorState state);

} // namespace rbfsafe
