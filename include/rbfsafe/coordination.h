#pragma once

#include <rbfsafe/identity.h>
#include <rbfsafe/occupancy.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

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

} // namespace rbfsafe
