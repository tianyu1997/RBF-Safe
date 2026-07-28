#pragma once

#include <rbfsafe/transparency.h>

#include <filesystem>
#include <string>
#include <vector>

namespace rbfsafe::internal {

class TransparencyLogWriteLock {
  public:
    TransparencyLogWriteLock(const TransparencyLogWriteLock&) = delete;
    TransparencyLogWriteLock& operator=(const TransparencyLogWriteLock&) = delete;
    TransparencyLogWriteLock(TransparencyLogWriteLock&& other) noexcept;
    TransparencyLogWriteLock& operator=(TransparencyLogWriteLock&&) = delete;
    ~TransparencyLogWriteLock();

    static Result<TransparencyLogWriteLock> acquire(const std::filesystem::path& directory);

  private:
    explicit TransparencyLogWriteLock(std::filesystem::path path);

    std::filesystem::path path_;
    bool held_ = true;
};

std::string deployment_transparency_anchor_identity(const DeploymentTransparencyAnchor& anchor);
std::string independent_runtime_observation_identity(const IndependentRuntimeObservation& observation);
std::string runtime_observation_attestation_message(const RuntimeObservationAttestation& attestation);
std::string runtime_observation_attestation_identity(const RuntimeObservationAttestation& attestation);
std::string
runtime_observation_attestation_set_identity(const RuntimeObservationAttestationSet& attestation_set);
std::string transparency_log_identity_value(const TransparencyLogIdentity& identity);
std::string transparency_log_leaf_identity(const TransparencyLogLeaf& leaf);
std::string transparency_log_checkpoint_message(const TransparencyLogCheckpoint& checkpoint);
std::string transparency_log_checkpoint_identity(const TransparencyLogCheckpoint& checkpoint);
std::string transparency_log_record_identity(const TransparencyLogRecord& record);
std::string transparency_inclusion_proof_identity(const TransparencyInclusionProof& proof);
std::string transparency_consistency_witness_identity(const TransparencyConsistencyWitness& witness);
std::string transparency_compact_consistency_proof_identity(const TransparencyCompactConsistencyProof& proof);
std::string transparency_log_audit_report_identity(const TransparencyLogAuditReport& report);

std::string transparency_leaf_hash(const std::string& leaf_id);
std::string transparency_merkle_root(const std::vector<std::string>& ordered_leaf_ids);
std::string transparency_append_merkle_leaf(std::array<std::string, 64>& frontier, const std::string& leaf_id,
                                            std::uint64_t previous_tree_size);
std::string transparency_append_merkle_subtree(std::array<std::string, 64>& frontier,
                                               const std::string& subtree_hash, std::uint8_t subtree_level,
                                               std::uint64_t previous_tree_size);
std::string transparency_merkle_frontier_root(const std::array<std::string, 64>& frontier);

Result<void> append_transparency_log_record_file(const std::filesystem::path& directory,
                                                 const TransparencyLogRecord& record,
                                                 std::uintmax_t maximum_record_bytes);

} // namespace rbfsafe::internal
