#pragma once

#include <rbfsafe/identity.h>
#include <rbfsafe/occupancy.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rbfsafe {

struct OccupancyPublication {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string stream_id;
    std::uint64_t publisher_sequence = 0;
    std::string parent_publication_id;
    std::string publisher_service_id;
    std::string publisher_key_id;
    std::string trust_bundle_id;
    std::string occupancy_bundle_id;
    std::string timeline_id;
    std::string workspace_frame_id;
    std::uint64_t valid_from_tick = 0;
    std::uint64_t valid_through_tick = 0;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<OccupancyPublication> load(const std::filesystem::path& path,
                                             std::uintmax_t maximum_payload_bytes = 1'048'576ULL);
};

struct VerifiedOccupancyPublication {
    std::string id;
    std::string publication_id;
    std::string stream_id;
    std::uint64_t publisher_sequence = 0;
    std::string publisher_service_id;
    std::string publisher_key_id;
    std::string trust_bundle_id;
    std::string occupancy_bundle_id;
    std::string timeline_id;
    std::string workspace_frame_id;
    std::uint64_t valid_from_tick = 0;
    std::uint64_t valid_through_tick = 0;
    std::uint64_t evaluation_tick = 0;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<OccupancyPublication> sign_continuous_fleet_occupancy_publication(
    const std::filesystem::path& occupancy_payload_path, const ServiceTrustBundle& trust_bundle,
    std::string stream_id, std::string publisher_service_id, std::string publisher_key_id,
    std::span<const std::byte> ed25519_secret_key, std::uint64_t publisher_sequence,
    std::string parent_publication_id, std::uint64_t valid_from_tick, std::uint64_t valid_through_tick,
    const ContinuousFleetOccupancyBundleLoadOptions& options = {});

Result<VerifiedOccupancyPublication> verify_continuous_fleet_occupancy_publication(
    const std::filesystem::path& occupancy_payload_path, const OccupancyPublication& publication,
    const ServiceTrustBundle& trust_bundle, std::string_view expected_stream_id,
    std::string_view expected_publisher_service_id, std::string_view expected_trust_bundle_id,
    std::string_view expected_parent_publication_id, std::uint64_t evaluation_tick,
    const ContinuousFleetOccupancyBundleLoadOptions& options = {});

Result<void> verify_occupancy_publication_successor(const OccupancyPublication& previous,
                                                    const OccupancyPublication& successor);

Result<void> save_occupancy_publication(const OccupancyPublication& publication,
                                        const std::filesystem::path& path, const SaveOptions& options = {});
Result<OccupancyPublication> load_occupancy_publication(const std::filesystem::path& path,
                                                        std::uintmax_t maximum_payload_bytes = 1'048'576ULL);

enum class OccupancyPublicationHistoryRelation : std::uint8_t {
    Identical = 0,
    FirstExtendsSecond = 1,
    SecondExtendsFirst = 2,
    Forked = 3,
};

const char*
occupancy_publication_history_relation_name(OccupancyPublicationHistoryRelation relation) noexcept;

struct OccupancyPublicationHistoryRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_record_id;
    std::string publication_id;
    std::string authentication_tag;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct OccupancyPublicationHistoryAudit {
    std::uint32_t storage_schema = 1;
    std::string id;
    OccupancyPublicationHistoryRelation relation = OccupancyPublicationHistoryRelation::Identical;
    std::string stream_id;
    std::string publisher_service_id;
    std::string trust_bundle_id;
    std::string root_publication_id;
    std::string first_head_publication_id;
    std::string second_head_publication_id;
    std::uint64_t first_publication_count = 0;
    std::uint64_t second_publication_count = 0;
    std::uint64_t common_prefix_count = 0;
    std::string common_publication_id;

    bool valid() const;
    bool fork_detected() const noexcept { return relation == OccupancyPublicationHistoryRelation::Forked; }
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct OccupancyPublicationHistoryLoadOptions {
    std::size_t maximum_publications = 100'000;
    std::uintmax_t maximum_manifest_bytes = 65'536ULL;
    std::uintmax_t maximum_record_bytes = 65'536ULL;
    std::uintmax_t maximum_publication_bytes = 1'048'576ULL;
    std::uintmax_t maximum_trust_bundle_bytes = 4'194'304ULL;
    std::uintmax_t maximum_total_payload_bytes = 4'294'967'296ULL;
    std::size_t maximum_trust_keys = 100'000;
    ContinuousFleetOccupancyBundleLoadOptions occupancy;
};

class OccupancyPublicationHistory {
  public:
    static Result<OccupancyPublicationHistory>
    create(const std::filesystem::path& directory, const OccupancyPublication& root_publication,
           const std::filesystem::path& root_payload_path, const ServiceTrustBundle& trust_bundle,
           std::string_view expected_stream_id, std::string_view expected_publisher_service_id,
           std::string_view expected_trust_bundle_id, std::string_view expected_root_publication_id,
           const OccupancyPublicationHistoryLoadOptions& options = {});

    static Result<OccupancyPublicationHistory>
    open(const std::filesystem::path& directory, std::string_view expected_stream_id,
         std::string_view expected_publisher_service_id, std::string_view expected_trust_bundle_id,
         std::string_view expected_root_publication_id, std::string_view expected_head_publication_id,
         const OccupancyPublicationHistoryLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    const std::string& stream_id() const noexcept { return stream_id_; }
    const std::string& publisher_service_id() const noexcept { return publisher_service_id_; }
    const std::string& trust_bundle_id() const noexcept { return trust_bundle_id_; }
    const std::string& root_publication_id() const noexcept { return root_publication_id_; }
    const std::string& current_publication_id() const noexcept { return current_publication_id_; }
    const std::string& timeline_id() const noexcept { return timeline_id_; }
    const std::string& workspace_frame_id() const noexcept { return workspace_frame_id_; }
    const std::vector<OccupancyPublicationHistoryRecord>& records() const noexcept { return records_; }
    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<ServiceTrustBundle> trust_bundle() const;
    Result<OccupancyPublication> current_publication() const;
    Result<OccupancyPublication> publication(std::string_view publication_id) const;
    Result<VerifiedOccupancyPublication> verify(std::string_view publication_id,
                                                std::uint64_t evaluation_tick) const;
    Result<OccupancyPublicationHistoryRecord> publish(const OccupancyPublication& publication,
                                                      const std::filesystem::path& payload_path,
                                                      std::string_view expected_head_publication_id,
                                                      std::size_t maximum_publications = 100'000);

  private:
    friend Result<OccupancyPublicationHistoryAudit>
    audit_occupancy_publication_histories(const OccupancyPublicationHistory& first,
                                          const OccupancyPublicationHistory& second);

    std::filesystem::path directory_;
    std::uint32_t storage_schema_ = 1;
    std::string stream_id_;
    std::string publisher_service_id_;
    std::string trust_bundle_id_;
    std::string root_publication_id_;
    std::string current_publication_id_;
    std::string timeline_id_;
    std::string workspace_frame_id_;
    std::optional<ServiceTrustBundle> trust_bundle_;
    std::vector<OccupancyPublicationHistoryRecord> records_;
    std::vector<OccupancyPublication> publications_;
    std::vector<std::filesystem::path> payload_paths_;
    OccupancyPublicationHistoryLoadOptions options_;
};

Result<OccupancyPublicationHistoryAudit>
audit_occupancy_publication_histories(const OccupancyPublicationHistory& first,
                                      const OccupancyPublicationHistory& second);

} // namespace rbfsafe
