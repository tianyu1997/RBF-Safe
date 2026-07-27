#pragma once

#include <rbfsafe/execution.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class ExecutionCompletionOutcome : std::uint8_t {
    Completed = 0,
    Failed = 1,
    Rejected = 2,
};

struct ExecutionControllerCompletionInput {
    ExecutionCompletionOutcome outcome = ExecutionCompletionOutcome::Failed;
    std::uint64_t completed_monotonic_ns = 0;
    std::string result_digest;
};

struct ExecutionControllerCompletion {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string session_id;
    std::string authorization_id;
    std::string command_sequence_id;
    std::uint64_t command_index = 0;
    std::string command_digest;
    std::string controller_service_id;
    std::string controller_key_id;
    ExecutionCompletionOutcome outcome = ExecutionCompletionOutcome::Failed;
    std::uint64_t completed_monotonic_ns = 0;
    std::string result_digest;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
};

Result<ExecutionControllerCompletion> sign_execution_controller_completion(
    const BoundedExecutionSession& session, const ExecutionCommandAuthorization& authorization,
    ExecutionControllerCompletionInput input, std::span<const std::byte> ed25519_secret_key);

Result<void> verify_execution_controller_completion(const BoundedExecutionSession& session,
                                                    const ExecutionCommandAuthorization& authorization,
                                                    const ExecutionControllerCompletion& completion);

enum class ExecutionDependencyKind : std::uint8_t {
    ReviewedProfile = 0,
    Atlas = 1,
    Scene = 2,
    ControllerKey = 3,
    RuntimeMonitorKey = 4,
    ReviewerKey = 5,
    TrustCheckpoint = 6,
};

struct ExecutionDependencyRevocation {
    ExecutionDependencyKind kind = ExecutionDependencyKind::ReviewedProfile;
    std::string subject_id;
    std::string detail;
};

bool valid_execution_dependency_revocation(const ExecutionDependencyRevocation& revocation);

enum class ExecutionLedgerRecordType : std::uint8_t {
    SessionOpened = 0,
    CommandAuthorized = 1,
    ControllerCompletion = 2,
    SessionCancelled = 3,
    SessionExpired = 4,
    DependencyRevoked = 5,
};

enum class ExecutionLedgerStatus : std::uint8_t {
    Open = 0,
    AwaitingCompletion = 1,
    Completed = 2,
    Cancelled = 3,
    Expired = 4,
    Revoked = 5,
    Failed = 6,
};

struct ExecutionLedgerRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string ledger_id;
    std::string session_id;
    ExecutionLedgerRecordType type = ExecutionLedgerRecordType::SessionOpened;
    std::uint64_t observed_monotonic_ns = 0;
    std::optional<ExecutionCommandAuthorization> authorization;
    std::optional<ExecutionControllerCompletion> completion;
    std::optional<ServiceTrustCheckpoint> trust_checkpoint;
    std::optional<ExecutionDependencyRevocation> revocation;
    std::string detail;

    bool valid() const;
};

struct ExecutionLedgerSummary {
    std::string id;
    std::string ledger_id;
    std::string session_id;
    std::string current_record_id;
    ExecutionLedgerStatus status = ExecutionLedgerStatus::Open;
    std::size_t record_count = 0;
    std::size_t authorization_count = 0;
    std::size_t completion_count = 0;
    std::size_t next_command_index = 0;
    std::optional<std::uint64_t> outstanding_command_index;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct ExecutionLedgerCommandDecision {
    std::string id;
    std::string ledger_id;
    std::string current_record_id;
    ExecutionLedgerStatus status = ExecutionLedgerStatus::Open;
    std::optional<ExecutionCommandAuthorization> authorization;

    bool valid() const;
    EvidenceLevel evidence() const noexcept {
        return authorization ? EvidenceLevel::RuntimeExecutable : EvidenceLevel::Unknown;
    }
    bool authorizes_execution() const noexcept { return authorization.has_value(); }
    bool open_ended() const noexcept { return false; }
};

struct ExecutionLedgerAuditReport {
    std::string id;
    std::string ledger_id;
    std::string session_id;
    std::string current_record_id;
    ExecutionLedgerStatus status = ExecutionLedgerStatus::Open;
    std::size_t verified_records = 0;
    std::size_t verified_checkpoints = 0;
    std::size_t authorization_count = 0;
    std::size_t completion_count = 0;
    std::string latest_checkpoint_id;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct ExecutionLedgerLoadOptions {
    std::size_t maximum_records = 1'000'000;
    std::size_t maximum_signatures_per_checkpoint = 100'000;
    std::size_t maximum_total_checkpoint_signatures = 1'000'000;
    std::uintmax_t maximum_manifest_bytes = 65'536ULL;
    std::uintmax_t maximum_record_bytes = 8'388'608ULL;
    CancellationToken cancellation;
};

class ExecutionLedger {
  public:
    static Result<ExecutionLedger> create(const std::filesystem::path& directory,
                                          const BoundedExecutionSession& session);
    static Result<ExecutionLedger> open(const std::filesystem::path& directory,
                                        const BoundedExecutionSession& session,
                                        const ReviewedDeploymentProfile& reviewed,
                                        const ServiceTrustHistory& trust_history, const SafeAtlas& atlas,
                                        const ExecutionLedgerLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const std::string& id() const noexcept { return id_; }
    const std::string& session_id() const noexcept { return session_id_; }
    const std::string& current_record_id() const noexcept { return current_record_id_; }
    const std::vector<ExecutionLedgerRecord>& records() const noexcept { return records_; }
    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    ExecutionLedgerSummary summary() const;
    Result<ExecutionLedgerAuditReport> audit(const BoundedExecutionSession& session,
                                             const ReviewedDeploymentProfile& reviewed,
                                             const ServiceTrustHistory& trust_history,
                                             const SafeAtlas& atlas) const;

    Result<ExecutionLedgerCommandDecision>
    authorize_command(const BoundedExecutionSession& session, const ReviewedDeploymentProfile& reviewed,
                      const ServiceTrustHistory& current_trust_history,
                      const ServiceTrustCheckpoint& current_trust_checkpoint,
                      const std::string& expected_current_checkpoint_id, const SafeAtlas& atlas,
                      std::uint64_t command_index, std::span<const double> configuration,
                      std::uint64_t dispatch_monotonic_ns, const std::string& expected_current_record_id);

    Result<ExecutionLedgerRecord> record_completion(const BoundedExecutionSession& session,
                                                    const ReviewedDeploymentProfile& reviewed,
                                                    const ServiceTrustHistory& trust_history,
                                                    const SafeAtlas& atlas,
                                                    const ExecutionControllerCompletion& completion,
                                                    const std::string& expected_current_record_id);

    Result<ExecutionLedgerRecord> cancel(const BoundedExecutionSession& session,
                                         const ReviewedDeploymentProfile& reviewed,
                                         const ServiceTrustHistory& trust_history, const SafeAtlas& atlas,
                                         std::uint64_t observed_monotonic_ns, std::string detail,
                                         const std::string& expected_current_record_id);

    Result<ExecutionLedgerRecord> expire(const BoundedExecutionSession& session,
                                         const ReviewedDeploymentProfile& reviewed,
                                         const ServiceTrustHistory& trust_history, const SafeAtlas& atlas,
                                         std::uint64_t observed_monotonic_ns,
                                         const std::string& expected_current_record_id);

    Result<ExecutionLedgerRecord> revoke_dependency(const BoundedExecutionSession& session,
                                                    const ReviewedDeploymentProfile& reviewed,
                                                    const ServiceTrustHistory& trust_history,
                                                    const SafeAtlas& atlas, ExecutionDependencyKind kind,
                                                    std::string subject_id,
                                                    std::uint64_t observed_monotonic_ns, std::string detail,
                                                    const std::string& expected_current_record_id);

  private:
    Result<ExecutionLedgerRecord> append_record_unlocked(ExecutionLedger fresh, ExecutionLedgerRecord record);

    std::filesystem::path directory_;
    std::string id_;
    std::string session_id_;
    std::string current_record_id_;
    std::size_t command_count_ = 0;
    std::uint64_t valid_from_monotonic_ns_ = 0;
    std::uint64_t valid_through_monotonic_ns_ = 0;
    std::vector<ExecutionLedgerRecord> records_;
    ExecutionLedgerLoadOptions options_;
};

std::string execution_completion_outcome_name(ExecutionCompletionOutcome outcome);
std::string execution_dependency_kind_name(ExecutionDependencyKind kind);
std::string execution_ledger_record_type_name(ExecutionLedgerRecordType type);
std::string execution_ledger_status_name(ExecutionLedgerStatus status);

} // namespace rbfsafe
