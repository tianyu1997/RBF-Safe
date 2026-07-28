#pragma once

#include <rbfsafe/transparency.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

struct TransparencyCheckpointWitnessPolicy {
    std::uint32_t minimum_witnesses = 2;
    bool require_distinct_services = true;
    bool exclude_log_signer = true;
};

bool valid_transparency_checkpoint_witness_policy(const TransparencyCheckpointWitnessPolicy& policy);

struct TransparencyCheckpointCosignature {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string checkpoint_id;
    std::uint64_t tree_size = 0;
    std::string root_hash;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string witness_service_id;
    std::string witness_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
};

Result<TransparencyCheckpointCosignature> sign_transparency_checkpoint_witness(
    const TransparencyLogIdentity& identity, const TransparencyLogCheckpoint& checkpoint,
    const ServiceTrustBundle& trust_bundle, std::string witness_service_id, std::string witness_key_id,
    std::span<const std::byte> ed25519_secret_key);

Result<void> verify_transparency_checkpoint_witness(const TransparencyLogIdentity& identity,
                                                    const TransparencyLogCheckpoint& checkpoint,
                                                    const TransparencyCheckpointCosignature& cosignature,
                                                    const ServiceTrustBundle& trust_bundle);

struct WitnessedTransparencyCheckpoint {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    TransparencyCheckpointWitnessPolicy policy;
    TransparencyLogCheckpoint checkpoint;
    std::vector<TransparencyCheckpointCosignature> cosignatures;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<WitnessedTransparencyCheckpoint> assemble_witnessed_transparency_checkpoint(
    const TransparencyLogIdentity& identity, TransparencyLogCheckpoint checkpoint,
    TransparencyCheckpointWitnessPolicy policy, std::vector<TransparencyCheckpointCosignature> cosignatures,
    const ServiceTrustBundle& trust_bundle);

Result<void>
verify_witnessed_transparency_checkpoint(const TransparencyLogIdentity& identity,
                                         const WitnessedTransparencyCheckpoint& witnessed_checkpoint,
                                         const ServiceTrustBundle& trust_bundle);

struct TransparencyCheckpointGossip {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string log_id;
    std::uint64_t sender_sequence = 0;
    std::string parent_gossip_id;
    std::string recipient_service_id;
    std::string sender_service_id;
    std::string sender_key_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    WitnessedTransparencyCheckpoint witnessed_checkpoint;
    std::optional<TransparencyCompactConsistencyProof> consistency_proof;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<TransparencyCheckpointGossip> sign_transparency_checkpoint_gossip(
    const TransparencyLogIdentity& identity, WitnessedTransparencyCheckpoint witnessed_checkpoint,
    std::optional<TransparencyCompactConsistencyProof> consistency_proof, std::string recipient_service_id,
    std::uint64_t sender_sequence, std::string parent_gossip_id, const ServiceTrustBundle& trust_bundle,
    std::string sender_service_id, std::string sender_key_id, std::span<const std::byte> ed25519_secret_key);

Result<void> verify_transparency_checkpoint_gossip(const TransparencyLogIdentity& identity,
                                                   const TransparencyCheckpointGossip& gossip,
                                                   const ServiceTrustBundle& trust_bundle);

enum class TransparencyGossipConflictType : std::uint8_t {
    SameSizeEquivocation = 0,
    InvalidConsistencyProof = 1,
};

struct TransparencyGossipConflict {
    std::string id;
    TransparencyGossipConflictType type = TransparencyGossipConflictType::SameSizeEquivocation;
    std::string first_gossip_id;
    std::string second_gossip_id;
    std::string first_checkpoint_id;
    std::string second_checkpoint_id;
    std::uint64_t first_tree_size = 0;
    std::uint64_t second_tree_size = 0;
    std::string consistency_proof_id;

    bool valid() const;
};

enum class TransparencyGossipStatus : std::uint8_t {
    Consistent = 0,
    Incomplete = 1,
    SplitView = 2,
};

struct TransparencyGossipAuditReport {
    std::string id;
    std::string log_id;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    TransparencyGossipStatus status = TransparencyGossipStatus::Incomplete;
    std::size_t authenticated_gossip_count = 0;
    std::size_t unique_checkpoint_count = 0;
    std::size_t linked_checkpoint_pairs = 0;
    std::size_t unlinked_checkpoint_pairs = 0;
    std::vector<TransparencyGossipConflict> conflicts;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct TransparencyGossipAuditOptions {
    std::size_t maximum_gossip_messages = 100'000;
    std::size_t maximum_unique_checkpoints = 10'000;
    std::size_t maximum_pair_checks = 1'000'000;
    std::size_t maximum_graph_steps = 10'000'000;
    CancellationToken cancellation;
};

struct TransparencyGossipRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    std::string log_id;
    std::string trust_bundle_id;
    TransparencyCheckpointGossip gossip;

    bool valid() const;
};

struct TransparencyGossipArchiveLoadOptions {
    std::size_t maximum_records = 100'000;
    std::size_t maximum_witnesses_per_checkpoint = 100'000;
    std::size_t maximum_total_witnesses = 1'000'000;
    std::size_t maximum_proof_subtrees = 256;
    std::size_t maximum_total_proof_subtrees = 1'000'000;
    std::size_t maximum_unique_checkpoints = 10'000;
    std::size_t maximum_pair_checks = 1'000'000;
    std::size_t maximum_graph_steps = 10'000'000;
    std::uintmax_t maximum_manifest_bytes = 65'536ULL;
    std::uintmax_t maximum_record_bytes = 16'777'216ULL;
    CancellationToken cancellation;
};

class TransparencyGossipArchive {
  public:
    static Result<TransparencyGossipArchive> create(const std::filesystem::path& directory,
                                                    TransparencyLogIdentity log_identity,
                                                    const ServiceTrustBundle& trust_bundle);
    static Result<TransparencyGossipArchive> open(const std::filesystem::path& directory,
                                                  const TransparencyLogIdentity& expected_log_identity,
                                                  const ServiceTrustBundle& trust_bundle,
                                                  const std::string& expected_trust_bundle_id,
                                                  const std::string& expected_head_record_id,
                                                  const TransparencyGossipArchiveLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const TransparencyLogIdentity& log_identity() const noexcept { return log_identity_; }
    const std::string& trust_bundle_id() const noexcept { return trust_bundle_id_; }
    std::uint64_t trust_bundle_sequence() const noexcept { return trust_bundle_sequence_; }
    const std::vector<TransparencyGossipRecord>& records() const noexcept { return records_; }
    const std::string& current_record_id() const noexcept { return current_record_id_; }
    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<TransparencyGossipRecord> publish(TransparencyCheckpointGossip gossip,
                                             const std::string& expected_head_record_id);
    Result<TransparencyGossipAuditReport> audit() const;

  private:
    std::filesystem::path directory_;
    TransparencyLogIdentity log_identity_;
    ServiceTrustBundle trust_bundle_;
    std::string trust_bundle_id_;
    std::uint64_t trust_bundle_sequence_ = 0;
    std::vector<TransparencyGossipRecord> records_;
    std::string current_record_id_;
    TransparencyGossipArchiveLoadOptions options_;
};

Result<TransparencyGossipAuditReport> audit_transparency_checkpoint_gossip(
    const TransparencyLogIdentity& identity, std::span<const TransparencyCheckpointGossip> gossip,
    const ServiceTrustBundle& trust_bundle, const TransparencyGossipAuditOptions& options = {});

std::string transparency_gossip_conflict_type_name(TransparencyGossipConflictType type);
std::string transparency_gossip_status_name(TransparencyGossipStatus status);

} // namespace rbfsafe
