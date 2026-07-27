#pragma once

#include <rbfsafe/remote.h>

#include <string>

namespace rbfsafe::internal {

std::string artifact_fetch_request_identity(const ArtifactFetchRequest& request);
std::string artifact_fetch_response_identity(const ArtifactFetchResponse& response);
std::string artifact_publish_request_identity(const ArtifactPublishRequest& request);
std::string artifact_publish_receipt_identity(const ArtifactPublishReceipt& receipt);
std::string verified_artifact_transfer_identity(const VerifiedArtifactTransfer& transfer);
std::string artifact_transfer_record_identity(const ArtifactTransferRecord& record);

ArtifactTransferAttestation make_artifact_transfer_attestation(
    ArtifactAuthenticationAlgorithm algorithm, ArtifactTransferOperation operation,
    const std::string& request_id, const std::string& response_id, const std::string& service_id,
    const std::string& artifact_id, const std::string& payload_digest, std::uint64_t payload_bytes,
    std::uint64_t service_sequence, std::string key_id);

std::string artifact_transfer_authentication_message(const ArtifactTransferAttestation& attestation);

Result<void> validate_artifact_transfer_attestation_binding(
    const ArtifactTransferAttestation& attestation, ArtifactAuthenticationAlgorithm algorithm,
    ArtifactTransferOperation operation, const std::string& request_id, const std::string& response_id,
    const std::string& service_id, const std::string& artifact_id, const std::string& payload_digest,
    std::uint64_t payload_bytes, std::uint64_t service_sequence);

Result<VerifiedArtifactTransfer>
finalize_verified_artifact_fetch(const SafetyMemory& memory, const ArtifactFetchRequest& request,
                                 const ArtifactFetchResponse& response, std::span<const std::byte> payload,
                                 ArtifactTransferAuthentication authentication, std::string attestation_id,
                                 std::string verification_key_id, std::string trust_bundle_id,
                                 const RemoteArtifactOptions& options);

Result<VerifiedArtifactTransfer>
finalize_verified_artifact_publish(const SafetyMemory& memory, const ArtifactPublishRequest& request,
                                   const ArtifactPublishReceipt& receipt, std::span<const std::byte> payload,
                                   ArtifactTransferAuthentication authentication, std::string attestation_id,
                                   std::string verification_key_id, std::string trust_bundle_id,
                                   const RemoteArtifactOptions& options);

} // namespace rbfsafe::internal
