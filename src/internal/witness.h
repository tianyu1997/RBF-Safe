#pragma once

#include <rbfsafe/witness.h>

#include <filesystem>
#include <string>

namespace rbfsafe::internal {

std::string transparency_checkpoint_cosignature_message(const TransparencyCheckpointCosignature& cosignature);
std::string
transparency_checkpoint_cosignature_identity(const TransparencyCheckpointCosignature& cosignature);
std::string
witnessed_transparency_checkpoint_identity(const WitnessedTransparencyCheckpoint& witnessed_checkpoint);
std::string transparency_checkpoint_gossip_message(const TransparencyCheckpointGossip& gossip);
std::string transparency_checkpoint_gossip_identity(const TransparencyCheckpointGossip& gossip);
std::string transparency_gossip_conflict_identity(const TransparencyGossipConflict& conflict);
std::string transparency_gossip_audit_report_identity(const TransparencyGossipAuditReport& report);
std::string transparency_gossip_record_identity(const TransparencyGossipRecord& record);

class TransparencyGossipWriteLock {
  public:
    TransparencyGossipWriteLock(const TransparencyGossipWriteLock&) = delete;
    TransparencyGossipWriteLock& operator=(const TransparencyGossipWriteLock&) = delete;
    TransparencyGossipWriteLock(TransparencyGossipWriteLock&& other) noexcept;
    TransparencyGossipWriteLock& operator=(TransparencyGossipWriteLock&&) = delete;
    ~TransparencyGossipWriteLock();

    static Result<TransparencyGossipWriteLock> acquire(const std::filesystem::path& directory);

  private:
    explicit TransparencyGossipWriteLock(std::filesystem::path path);

    std::filesystem::path path_;
    bool held_ = true;
};

Result<void> append_transparency_gossip_record_file(const std::filesystem::path& directory,
                                                    const TransparencyGossipRecord& record,
                                                    std::uintmax_t maximum_record_bytes);

} // namespace rbfsafe::internal
