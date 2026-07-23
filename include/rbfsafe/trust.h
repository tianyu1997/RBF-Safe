#pragma once

#include <rbfsafe/memory.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace rbfsafe {

enum class ArtifactAuthenticationAlgorithm : std::uint8_t {
    HmacSha256 = 0,
};

struct ArtifactAttestation {
    std::uint64_t sequence = 0;
    std::string id;
    std::string service_id;
    std::string key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::HmacSha256;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    std::string authentication_tag;
};

struct ArtifactVerificationOptions {
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
    CancellationToken cancellation;
};

bool valid_artifact_attestation(const ArtifactAttestation& attestation);

Result<ArtifactAttestation> attest_artifact(const MemoryArtifact& artifact,
                                            std::span<const std::byte> payload, std::string service_id,
                                            std::string key_id, std::span<const std::byte> hmac_key,
                                            std::uint64_t sequence,
                                            std::string media_type = "application/octet-stream");

Result<ArtifactAttestation> attest_artifact_file(const MemoryArtifact& artifact,
                                                 const std::filesystem::path& payload_path,
                                                 std::string service_id, std::string key_id,
                                                 std::span<const std::byte> hmac_key, std::uint64_t sequence,
                                                 std::string media_type = "application/octet-stream",
                                                 const ArtifactVerificationOptions& options = {});

Result<void> verify_artifact(const MemoryArtifact& artifact, std::span<const std::byte> payload,
                             const ArtifactAttestation& attestation, std::string_view expected_service_id,
                             std::string_view expected_key_id, std::span<const std::byte> hmac_key);

Result<void> verify_artifact_file(const MemoryArtifact& artifact, const std::filesystem::path& payload_path,
                                  const ArtifactAttestation& attestation,
                                  std::string_view expected_service_id, std::string_view expected_key_id,
                                  std::span<const std::byte> hmac_key,
                                  const ArtifactVerificationOptions& options = {});

Result<void> save_artifact_attestation(const ArtifactAttestation& attestation,
                                       const std::filesystem::path& path, const SaveOptions& options = {});
Result<ArtifactAttestation> load_artifact_attestation(const std::filesystem::path& path,
                                                      std::uintmax_t maximum_bytes = 65'536ULL);

std::string artifact_authentication_algorithm_name(ArtifactAuthenticationAlgorithm algorithm);

} // namespace rbfsafe
