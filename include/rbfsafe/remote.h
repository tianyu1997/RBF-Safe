#pragma once

#include <rbfsafe/trust.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rbfsafe {

enum class ArtifactTransferOperation : std::uint8_t {
    Fetch = 0,
    Publish = 1,
};

enum class ArtifactTransferAuthentication : std::uint8_t {
    None = 0,
    HmacSha256 = 1,
    Ed25519 = 2,
};

struct ArtifactTransferAttestation {
    std::string id;
    std::string service_id;
    std::string key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::HmacSha256;
    ArtifactTransferOperation operation = ArtifactTransferOperation::Fetch;
    std::string request_id;
    std::string response_id;
    std::string artifact_id;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::uint64_t service_sequence = 0;
    std::string authentication_tag;
};

struct ArtifactFetchRequest {
    std::uint64_t sequence = 0;
    std::string id;
    std::string service_id;
    std::string memory_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string locator;
    std::string media_type;
    std::uint64_t maximum_payload_bytes = 0;
    ArtifactTransferAuthentication response_authentication = ArtifactTransferAuthentication::HmacSha256;
};

struct ArtifactFetchResponse {
    std::string id;
    std::string request_id;
    std::string service_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    std::uint64_t service_sequence = 0;
    std::optional<ArtifactTransferAttestation> service_attestation;
};

struct ArtifactPublishRequest {
    std::uint64_t sequence = 0;
    std::string id;
    std::string service_id;
    std::string memory_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string locator;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    ArtifactTransferAuthentication receipt_authentication = ArtifactTransferAuthentication::HmacSha256;
};

struct ArtifactPublishReceipt {
    std::string id;
    std::string request_id;
    std::string service_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    std::uint64_t service_sequence = 0;
    std::optional<ArtifactTransferAttestation> service_attestation;
};

struct VerifiedArtifactTransfer {
    std::string id;
    ArtifactTransferOperation operation = ArtifactTransferOperation::Fetch;
    std::string request_id;
    std::string response_id;
    std::string service_id;
    std::string memory_id;
    std::string artifact_id;
    std::uint64_t artifact_generation = 0;
    MemoryArtifactState artifact_state = MemoryArtifactState::Active;
    std::string artifact_content_digest;
    std::string payload_digest;
    std::uint64_t payload_bytes = 0;
    std::string media_type;
    std::uint64_t service_sequence = 0;
    ArtifactTransferAuthentication authentication = ArtifactTransferAuthentication::None;
    std::string attestation_id;
    std::string verification_key_id;
    std::string trust_bundle_id;
};

struct RemoteArtifactOptions {
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
    bool require_active_artifact = true;
    CancellationToken cancellation;
};

bool valid_artifact_fetch_request(const ArtifactFetchRequest& request);
bool valid_artifact_fetch_response(const ArtifactFetchResponse& response);
bool valid_artifact_publish_request(const ArtifactPublishRequest& request);
bool valid_artifact_publish_receipt(const ArtifactPublishReceipt& receipt);
bool valid_artifact_transfer_attestation(const ArtifactTransferAttestation& attestation);
bool valid_verified_artifact_transfer(const VerifiedArtifactTransfer& transfer);

Result<ArtifactFetchRequest> prepare_artifact_fetch(
    const SafetyMemory& memory, const std::string& artifact_id, std::string service_id,
    std::uint64_t sequence, std::string media_type,
    ArtifactTransferAuthentication response_authentication = ArtifactTransferAuthentication::HmacSha256,
    const RemoteArtifactOptions& options = {});

Result<ArtifactFetchResponse> make_artifact_fetch_response(const ArtifactFetchRequest& request,
                                                           std::span<const std::byte> payload,
                                                           std::uint64_t service_sequence);

Result<ArtifactFetchResponse> authenticate_artifact_fetch_response(ArtifactFetchResponse response,
                                                                   std::string key_id,
                                                                   std::span<const std::byte> hmac_key);

Result<VerifiedArtifactTransfer>
verify_artifact_fetch(const SafetyMemory& memory, const ArtifactFetchRequest& request,
                      const ArtifactFetchResponse& response, std::span<const std::byte> payload,
                      std::string_view expected_key_id = {}, std::span<const std::byte> hmac_key = {},
                      const RemoteArtifactOptions& options = {});

Result<ArtifactPublishRequest> prepare_artifact_publish(
    const SafetyMemory& memory, const std::string& artifact_id, std::span<const std::byte> payload,
    std::string service_id, std::uint64_t sequence, std::string media_type,
    ArtifactTransferAuthentication receipt_authentication = ArtifactTransferAuthentication::HmacSha256,
    const RemoteArtifactOptions& options = {});

Result<ArtifactPublishReceipt> make_artifact_publish_receipt(const ArtifactPublishRequest& request,
                                                             std::uint64_t service_sequence);

Result<ArtifactPublishReceipt> authenticate_artifact_publish_receipt(ArtifactPublishReceipt receipt,
                                                                     std::string key_id,
                                                                     std::span<const std::byte> hmac_key);

Result<VerifiedArtifactTransfer>
verify_artifact_publish(const SafetyMemory& memory, const ArtifactPublishRequest& request,
                        const ArtifactPublishReceipt& receipt, std::span<const std::byte> payload,
                        std::string_view expected_key_id = {}, std::span<const std::byte> hmac_key = {},
                        const RemoteArtifactOptions& options = {});

struct ArtifactTransferRecord {
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    VerifiedArtifactTransfer transfer;
};

struct ArtifactTransferJournalLoadOptions {
    std::size_t maximum_records = 1'000'000;
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
};

class ArtifactTransferJournal;
Result<void> save_artifact_transfer_journal(const ArtifactTransferJournal& journal,
                                            const std::filesystem::path& directory,
                                            const SaveOptions& options);
Result<ArtifactTransferJournal>
load_artifact_transfer_journal(const std::filesystem::path& directory,
                               const ArtifactTransferJournalLoadOptions& options);

class ArtifactTransferJournal {
  public:
    const std::vector<ArtifactTransferRecord>& records() const noexcept { return records_; }
    const std::string& current_record_id() const noexcept { return current_record_id_; }
    std::string identity() const;
    bool valid() const;

    Result<ArtifactTransferRecord> append(VerifiedArtifactTransfer transfer,
                                          const std::string& expected_current_record_id,
                                          std::size_t maximum_records = 1'000'000);

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<ArtifactTransferJournal> load(const std::filesystem::path& directory,
                                                const ArtifactTransferJournalLoadOptions& options = {});

  private:
    friend Result<void> save_artifact_transfer_journal(const ArtifactTransferJournal&,
                                                       const std::filesystem::path&, const SaveOptions&);
    friend Result<ArtifactTransferJournal>
    load_artifact_transfer_journal(const std::filesystem::path&, const ArtifactTransferJournalLoadOptions&);

    std::vector<ArtifactTransferRecord> records_;
    std::string current_record_id_;
};

std::string artifact_transfer_operation_name(ArtifactTransferOperation operation);
std::string artifact_transfer_authentication_name(ArtifactTransferAuthentication authentication);

} // namespace rbfsafe
