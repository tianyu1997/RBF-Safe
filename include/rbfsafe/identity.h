#pragma once

#include <rbfsafe/remote.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

inline constexpr std::size_t kEd25519SeedBytes = 32;
inline constexpr std::size_t kEd25519PublicKeyBytes = 32;
inline constexpr std::size_t kEd25519SecretKeyBytes = 64;
inline constexpr std::size_t kEd25519SignatureBytes = 64;

struct Ed25519KeyPair {
    std::array<std::byte, kEd25519PublicKeyBytes> public_key{};
    std::array<std::byte, kEd25519SecretKeyBytes> secret_key{};
};

Result<Ed25519KeyPair> ed25519_key_pair_from_seed(std::span<const std::byte> seed);
Result<std::array<std::byte, kEd25519SignatureBytes>> ed25519_sign(std::span<const std::byte> message,
                                                                   std::span<const std::byte> secret_key);
Result<void> ed25519_verify(std::span<const std::byte> message, std::span<const std::byte> signature,
                            std::span<const std::byte> public_key);

enum class ServiceKeyState : std::uint8_t {
    Pending = 0,
    Active = 1,
    Retired = 2,
    Revoked = 3,
};

struct ServicePublicKey {
    std::string id;
    std::string service_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::array<std::byte, kEd25519PublicKeyBytes> public_key{};
    std::uint64_t valid_from_sequence = 1;
    std::uint64_t valid_through_sequence = 0;
    ServiceKeyState state = ServiceKeyState::Pending;
    bool allow_fetch = true;
    bool allow_publish = true;
    bool allow_rotate = false;
};

bool valid_service_public_key(const ServicePublicKey& key);

Result<ServicePublicKey>
make_service_public_key(std::string service_id, std::span<const std::byte> public_key,
                        std::uint64_t valid_from_sequence = 1, std::uint64_t valid_through_sequence = 0,
                        ServiceKeyState state = ServiceKeyState::Pending, bool allow_fetch = true,
                        bool allow_publish = true, bool allow_rotate = false);

struct ServiceTrustBundleLoadOptions {
    std::size_t maximum_keys = 100'000;
    std::uintmax_t maximum_payload_bytes = 4'194'304ULL;
};

class ServiceTrustBundle;
Result<void> save_service_trust_bundle(const ServiceTrustBundle& bundle, const std::filesystem::path& path,
                                       const SaveOptions& options);
Result<ServiceTrustBundle> load_service_trust_bundle(const std::filesystem::path& path,
                                                     const ServiceTrustBundleLoadOptions& options);

class ServiceTrustBundle {
  public:
    static Result<ServiceTrustBundle> create(std::uint64_t sequence, std::string parent_id,
                                             std::vector<ServicePublicKey> keys);

    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    std::uint64_t sequence() const noexcept { return sequence_; }
    const std::string& id() const noexcept { return id_; }
    const std::string& parent_id() const noexcept { return parent_id_; }
    const std::vector<ServicePublicKey>& keys() const noexcept { return keys_; }

    bool valid() const;
    Result<std::optional<ServicePublicKey>> key(const std::string& service_id,
                                                const std::string& key_id) const;

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<ServiceTrustBundle> load(const std::filesystem::path& path,
                                           const ServiceTrustBundleLoadOptions& options = {});

  private:
    friend Result<void> save_service_trust_bundle(const ServiceTrustBundle&, const std::filesystem::path&,
                                                  const SaveOptions&);
    friend Result<ServiceTrustBundle> load_service_trust_bundle(const std::filesystem::path&,
                                                                const ServiceTrustBundleLoadOptions&);

    std::uint64_t sequence_ = 0;
    std::uint32_t storage_schema_ = 2;
    std::string id_;
    std::string parent_id_;
    std::vector<ServicePublicKey> keys_;
};

Result<ServiceTrustBundle> rotate_service_trust_bundle(const ServiceTrustBundle& previous,
                                                       std::vector<ServicePublicKey> keys);

struct ServiceTrustBundleAuthorization {
    std::string id;
    std::string predecessor_bundle_id;
    std::string successor_bundle_id;
    std::uint64_t predecessor_sequence = 0;
    std::uint64_t successor_sequence = 0;
    std::string signer_service_id;
    std::string signer_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_service_trust_bundle_authorization(const ServiceTrustBundleAuthorization& authorization);

Result<ServiceTrustBundleAuthorization> authorize_service_trust_bundle_successor(
    const ServiceTrustBundle& predecessor, const ServiceTrustBundle& successor, std::string signer_service_id,
    std::string signer_key_id, std::span<const std::byte> ed25519_secret_key);

Result<void> verify_service_trust_bundle_successor(const ServiceTrustBundle& predecessor,
                                                   const ServiceTrustBundle& successor,
                                                   const ServiceTrustBundleAuthorization& authorization);

enum class ServiceTrustRotationEventType : std::uint8_t {
    RootPinned = 0,
    SuccessorAuthorized = 1,
};

struct ServiceTrustRotationRecord {
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    ServiceTrustRotationEventType type = ServiceTrustRotationEventType::RootPinned;
    std::string bundle_id;
    std::optional<ServiceTrustBundleAuthorization> authorization;
};

struct ServiceTrustHistoryLoadOptions {
    std::size_t maximum_bundles = 100'000;
    std::size_t maximum_keys_per_bundle = 100'000;
    std::size_t maximum_total_keys = 1'000'000;
    std::uintmax_t maximum_metadata_bytes = 65'536ULL;
    std::uintmax_t maximum_bundle_bytes = 4'194'304ULL;
    CancellationToken cancellation;
};

class ServiceTrustHistory {
  public:
    static Result<ServiceTrustHistory> create(const std::filesystem::path& directory,
                                              const ServiceTrustBundle& root_bundle,
                                              const std::string& expected_root_bundle_id);
    static Result<ServiceTrustHistory> open(const std::filesystem::path& directory,
                                            const std::string& expected_root_bundle_id,
                                            const std::string& expected_head_bundle_id,
                                            const ServiceTrustHistoryLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    const std::string& root_bundle_id() const noexcept { return root_bundle_id_; }
    const std::string& current_bundle_id() const noexcept { return current_bundle_id_; }
    const std::vector<ServiceTrustRotationRecord>& records() const noexcept { return records_; }
    bool valid() const;

    Result<ServiceTrustBundle> current_bundle() const;
    Result<ServiceTrustBundle> bundle(const std::string& bundle_id) const;
    Result<ServiceTrustRotationRecord> publish(const ServiceTrustBundle& successor,
                                               const ServiceTrustBundleAuthorization& authorization,
                                               const std::string& expected_head_bundle_id,
                                               std::size_t maximum_bundles = 100'000);

  private:
    std::filesystem::path directory_;
    std::string root_bundle_id_;
    std::string current_bundle_id_;
    std::vector<ServiceTrustBundle> bundles_;
    std::vector<ServiceTrustRotationRecord> records_;
    ServiceTrustHistoryLoadOptions options_;
};

Result<ServicePublicKey> trusted_service_public_key(const ServiceTrustBundle& bundle,
                                                    const std::string& service_id, const std::string& key_id,
                                                    ArtifactTransferOperation operation,
                                                    std::uint64_t service_sequence);

Result<ArtifactFetchResponse> sign_artifact_fetch_response(ArtifactFetchResponse response, std::string key_id,
                                                           std::span<const std::byte> ed25519_secret_key);

Result<ArtifactPublishReceipt> sign_artifact_publish_receipt(ArtifactPublishReceipt receipt,
                                                             std::string key_id,
                                                             std::span<const std::byte> ed25519_secret_key);

Result<VerifiedArtifactTransfer> verify_artifact_fetch_offline(const SafetyMemory& memory,
                                                               const ArtifactFetchRequest& request,
                                                               const ArtifactFetchResponse& response,
                                                               std::span<const std::byte> payload,
                                                               const ServiceTrustBundle& trust_bundle,
                                                               const RemoteArtifactOptions& options = {});

Result<VerifiedArtifactTransfer> verify_artifact_publish_offline(const SafetyMemory& memory,
                                                                 const ArtifactPublishRequest& request,
                                                                 const ArtifactPublishReceipt& receipt,
                                                                 std::span<const std::byte> payload,
                                                                 const ServiceTrustBundle& trust_bundle,
                                                                 const RemoteArtifactOptions& options = {});

std::string service_key_state_name(ServiceKeyState state);
std::string service_trust_rotation_event_type_name(ServiceTrustRotationEventType type);

} // namespace rbfsafe
