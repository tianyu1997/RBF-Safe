#pragma once

#include <rbfsafe/execution_ledger.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

struct DeploymentTransparencyAnchor {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string deployment_id;
    std::string reviewed_profile_id;
    std::string approval_set_id;
    std::string robot_digest;
    std::string controller_digest;
    std::string platform_digest;
    std::string runtime_digest;
    std::string trust_root_bundle_id;
    std::string trust_checkpoint_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string trust_head_record_id;

    static Result<DeploymentTransparencyAnchor> create(const ReviewedDeploymentProfile& reviewed,
                                                       const ServiceTrustHistory& trust_history,
                                                       const ServiceTrustCheckpoint& trust_checkpoint,
                                                       const std::string& expected_checkpoint_id);

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct IndependentRuntimeObservationInput {
    DeploymentRuntimeSnapshot runtime;
    std::uint64_t observation_sequence = 1;
    std::uint64_t observed_monotonic_ns = 0;
    ExecutionMonitorState monitor_state = ExecutionMonitorState::Disarmed;
    std::string configuration_digest;
};

struct IndependentRuntimeObservation {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string session_id;
    std::string ledger_id;
    std::string ledger_record_id;
    std::string authorization_id;
    std::string command_sequence_id;
    std::uint64_t command_index = 0;
    std::string command_digest;
    DeploymentRuntimeSnapshot runtime;
    std::uint64_t observation_sequence = 0;
    std::uint64_t observed_monotonic_ns = 0;
    ExecutionMonitorState monitor_state = ExecutionMonitorState::Disarmed;
    std::string configuration_digest;

    static Result<IndependentRuntimeObservation> create(const BoundedExecutionSession& session,
                                                        const ExecutionLedger& ledger,
                                                        const ExecutionCommandAuthorization& authorization,
                                                        IndependentRuntimeObservationInput input);

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct RuntimeObservationAttestation {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string observation_id;
    std::string source_service_id;
    std::string source_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
};

struct RuntimeObservationPolicy {
    std::uint32_t minimum_attestations = 1;
    bool require_distinct_services = true;
    bool exclude_controller_service = true;
};

bool valid_runtime_observation_policy(const RuntimeObservationPolicy& policy);

struct RuntimeObservationAttestationSet {
    std::uint32_t storage_schema = 1;
    std::string id;
    IndependentRuntimeObservation observation;
    RuntimeObservationPolicy policy;
    std::vector<RuntimeObservationAttestation> attestations;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<RuntimeObservationAttestation>
sign_runtime_observation(const IndependentRuntimeObservation& observation, std::string source_service_id,
                         std::string source_key_id, std::span<const std::byte> ed25519_secret_key);

Result<void> verify_runtime_observation_attestation(const IndependentRuntimeObservation& observation,
                                                    const RuntimeObservationAttestation& attestation,
                                                    const ServiceTrustBundle& trust_bundle);

Result<RuntimeObservationAttestationSet> assemble_runtime_observation_attestations(
    const BoundedExecutionSession& session, IndependentRuntimeObservation observation,
    RuntimeObservationPolicy policy, std::vector<RuntimeObservationAttestation> attestations,
    const ServiceTrustBundle& trust_bundle);

Result<void> verify_runtime_observation_attestations(const BoundedExecutionSession& session,
                                                     const RuntimeObservationAttestationSet& attestation_set,
                                                     const ServiceTrustBundle& trust_bundle);

struct TransparencyLogIdentity {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_namespace;
    std::string signer_service_id;
    std::string signer_key_id;
    std::array<std::byte, kEd25519PublicKeyBytes> signer_public_key{};

    static Result<TransparencyLogIdentity> create(std::string log_namespace, std::string signer_service_id,
                                                  std::string signer_key_id,
                                                  std::span<const std::byte> signer_public_key);
    bool valid() const;
};

enum class TransparencyLeafKind : std::uint8_t {
    DeploymentAnchor = 0,
    RuntimeObservation = 1,
};

struct TransparencyLogLeaf {
    std::uint32_t storage_schema = 1;
    std::uint64_t index = 0;
    std::string id;
    std::string log_id;
    TransparencyLeafKind kind = TransparencyLeafKind::DeploymentAnchor;
    std::optional<DeploymentTransparencyAnchor> deployment_anchor;
    std::optional<RuntimeObservationAttestationSet> runtime_observation;

    bool valid() const;
};

struct TransparencyLogCheckpoint {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::uint64_t tree_size = 0;
    std::string root_hash;
    std::string previous_checkpoint_id;
    std::string signer_service_id;
    std::string signer_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
};

struct TransparencyLogRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string log_id;
    TransparencyLogLeaf leaf;
    TransparencyLogCheckpoint checkpoint;

    bool valid() const;
};

struct TransparencyInclusionProof {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string checkpoint_id;
    std::string leaf_id;
    std::uint64_t leaf_index = 0;
    std::uint64_t tree_size = 0;
    std::string root_hash;
    std::vector<std::string> sibling_hashes;

    bool valid() const;
};

struct TransparencyConsistencyWitness {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string old_checkpoint_id;
    std::string new_checkpoint_id;
    std::uint64_t old_tree_size = 0;
    std::uint64_t new_tree_size = 0;
    std::string old_root_hash;
    std::string new_root_hash;
    std::vector<std::string> ordered_leaf_ids;

    bool valid() const;
};

struct TransparencyMerkleSubtree {
    std::uint8_t level = 0;
    std::string hash;

    bool valid() const;
};

struct TransparencyCompactConsistencyProof {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string old_checkpoint_id;
    std::string new_checkpoint_id;
    std::uint64_t old_tree_size = 0;
    std::uint64_t new_tree_size = 0;
    std::string old_root_hash;
    std::string new_root_hash;
    std::vector<TransparencyMerkleSubtree> old_frontier;
    std::vector<TransparencyMerkleSubtree> appended_subtrees;

    bool valid() const;
};

struct TransparencyLogAuditReport {
    std::string id;
    std::string log_id;
    std::string current_checkpoint_id;
    std::string current_root_hash;
    std::size_t verified_records = 0;
    std::size_t deployment_anchor_count = 0;
    std::size_t runtime_observation_count = 0;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct TransparencyLogLoadOptions {
    std::size_t maximum_records = 1'000'000;
    std::size_t maximum_attestations_per_observation = 100'000;
    std::size_t maximum_total_attestations = 1'000'000;
    std::uintmax_t maximum_manifest_bytes = 65'536ULL;
    std::uintmax_t maximum_record_bytes = 16'777'216ULL;
    CancellationToken cancellation;
};

class TransparencyLog {
  public:
    static Result<TransparencyLog> create(const std::filesystem::path& directory,
                                          TransparencyLogIdentity identity);
    static Result<TransparencyLog> open(const std::filesystem::path& directory,
                                        const TransparencyLogIdentity& expected_identity,
                                        const std::string& expected_checkpoint_id,
                                        const TransparencyLogLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const TransparencyLogIdentity& identity() const noexcept { return identity_; }
    const std::vector<TransparencyLogRecord>& records() const noexcept { return records_; }
    const std::string& current_checkpoint_id() const noexcept { return current_checkpoint_id_; }
    const std::string& current_root_hash() const noexcept { return current_root_hash_; }
    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<TransparencyLogRecord>
    publish_deployment_anchor(DeploymentTransparencyAnchor anchor,
                              std::span<const std::byte> signer_secret_key,
                              const std::string& expected_current_checkpoint_id);
    Result<TransparencyLogRecord>
    publish_runtime_observation(RuntimeObservationAttestationSet observation,
                                std::span<const std::byte> signer_secret_key,
                                const std::string& expected_current_checkpoint_id);

    Result<TransparencyInclusionProof> inclusion_proof(std::uint64_t leaf_index) const;
    Result<TransparencyConsistencyWitness> consistency_witness(std::uint64_t old_tree_size) const;
    Result<TransparencyCompactConsistencyProof> compact_consistency_proof(std::uint64_t old_tree_size) const;
    Result<TransparencyLogAuditReport> audit() const;

  private:
    Result<TransparencyLogRecord> publish_leaf(TransparencyLogLeaf leaf,
                                               std::span<const std::byte> signer_secret_key,
                                               const std::string& expected_current_checkpoint_id);

    std::filesystem::path directory_;
    TransparencyLogIdentity identity_;
    std::vector<TransparencyLogRecord> records_;
    std::string current_checkpoint_id_;
    std::string current_root_hash_;
    std::array<std::string, 64> merkle_frontier_{};
    TransparencyLogLoadOptions options_;
};

Result<void> verify_transparency_log_checkpoint(const TransparencyLogIdentity& identity,
                                                const TransparencyLogCheckpoint& checkpoint);

Result<void> verify_transparency_inclusion(const TransparencyLogIdentity& identity,
                                           const TransparencyLogCheckpoint& checkpoint,
                                           const TransparencyLogLeaf& leaf,
                                           const TransparencyInclusionProof& proof);

Result<void> verify_transparency_consistency(const TransparencyLogIdentity& identity,
                                             const TransparencyLogCheckpoint& old_checkpoint,
                                             const TransparencyLogCheckpoint& new_checkpoint,
                                             const TransparencyConsistencyWitness& witness);

Result<void> verify_transparency_compact_consistency(const TransparencyLogIdentity& identity,
                                                     const TransparencyLogCheckpoint& old_checkpoint,
                                                     const TransparencyLogCheckpoint& new_checkpoint,
                                                     const TransparencyCompactConsistencyProof& proof);

std::string transparency_leaf_kind_name(TransparencyLeafKind kind);

} // namespace rbfsafe
