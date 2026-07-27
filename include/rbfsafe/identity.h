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

struct ServiceTrustRotationPolicy {
    std::uint32_t minimum_signatures = 1;
    bool require_distinct_services = false;
};

bool valid_service_trust_rotation_policy(const ServiceTrustRotationPolicy& policy);

class ServiceTrustBundle;
Result<void> save_service_trust_bundle(const ServiceTrustBundle& bundle, const std::filesystem::path& path,
                                       const SaveOptions& options);
Result<ServiceTrustBundle> load_service_trust_bundle(const std::filesystem::path& path,
                                                     const ServiceTrustBundleLoadOptions& options);

class ServiceTrustBundle {
  public:
    static Result<ServiceTrustBundle> create(std::uint64_t sequence, std::string parent_id,
                                             std::vector<ServicePublicKey> keys);
    static Result<ServiceTrustBundle> create_with_rotation_policy(std::uint64_t sequence,
                                                                  std::string parent_id,
                                                                  std::vector<ServicePublicKey> keys,
                                                                  ServiceTrustRotationPolicy rotation_policy);

    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    std::uint64_t sequence() const noexcept { return sequence_; }
    const std::string& id() const noexcept { return id_; }
    const std::string& parent_id() const noexcept { return parent_id_; }
    const std::vector<ServicePublicKey>& keys() const noexcept { return keys_; }
    const ServiceTrustRotationPolicy& rotation_policy() const noexcept { return rotation_policy_; }

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
    ServiceTrustRotationPolicy rotation_policy_;
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

struct ServiceTrustBundleAuthorizationSet {
    std::string id;
    std::string predecessor_bundle_id;
    std::string successor_bundle_id;
    std::uint64_t predecessor_sequence = 0;
    std::uint64_t successor_sequence = 0;
    std::vector<ServiceTrustBundleAuthorization> authorizations;
};

bool valid_service_trust_bundle_authorization_set(
    const ServiceTrustBundleAuthorizationSet& authorization_set);

Result<ServiceTrustBundleAuthorizationSet>
assemble_service_trust_bundle_authorizations(const ServiceTrustBundle& predecessor,
                                             const ServiceTrustBundle& successor,
                                             std::vector<ServiceTrustBundleAuthorization> authorizations);

Result<void>
verify_service_trust_bundle_successor(const ServiceTrustBundle& predecessor,
                                      const ServiceTrustBundle& successor,
                                      const ServiceTrustBundleAuthorizationSet& authorization_set);

enum class ServiceTrustRotationEventType : std::uint8_t {
    RootPinned = 0,
    SuccessorAuthorized = 1,
};

struct ServiceTrustRotationRecord {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_id;
    ServiceTrustRotationEventType type = ServiceTrustRotationEventType::RootPinned;
    std::string bundle_id;
    std::optional<ServiceTrustBundleAuthorization> authorization;
    std::optional<ServiceTrustBundleAuthorizationSet> authorization_set;
};

struct ServiceTrustHistoryLoadOptions {
    std::size_t maximum_bundles = 100'000;
    std::size_t maximum_keys_per_bundle = 100'000;
    std::size_t maximum_total_keys = 1'000'000;
    std::size_t maximum_signatures_per_rotation = 100'000;
    std::uintmax_t maximum_metadata_bytes = 65'536ULL;
    std::uintmax_t maximum_bundle_bytes = 4'194'304ULL;
    CancellationToken cancellation;
};

struct ServiceTrustCheckpointSignature {
    std::string signer_service_id;
    std::string signer_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;
};

bool valid_service_trust_checkpoint_signature(const ServiceTrustCheckpointSignature& signature);

struct ServiceTrustCheckpointLoadOptions {
    std::size_t maximum_signatures = 100'000;
    std::uintmax_t maximum_payload_bytes = 4'194'304ULL;
};

struct ServiceTrustCheckpoint {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string root_bundle_id;
    std::string head_bundle_id;
    std::uint64_t head_sequence = 0;
    std::string head_record_id;
    std::vector<ServiceTrustCheckpointSignature> signatures;

    bool valid() const;
    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<ServiceTrustCheckpoint> load(const std::filesystem::path& path,
                                               const ServiceTrustCheckpointLoadOptions& options = {});
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
    static Result<ServiceTrustHistory> open(const std::filesystem::path& directory,
                                            const std::string& expected_root_bundle_id,
                                            const ServiceTrustCheckpoint& checkpoint,
                                            const std::string& expected_checkpoint_id,
                                            const ServiceTrustHistoryLoadOptions& options = {});

    const std::filesystem::path& directory() const noexcept { return directory_; }
    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
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
    Result<ServiceTrustRotationRecord> publish(const ServiceTrustBundle& successor,
                                               const ServiceTrustBundleAuthorizationSet& authorization_set,
                                               const std::string& expected_head_bundle_id,
                                               std::size_t maximum_bundles = 100'000);

  private:
    Result<ServiceTrustRotationRecord>
    publish_impl(const ServiceTrustBundle& successor,
                 std::optional<ServiceTrustBundleAuthorization> authorization,
                 std::optional<ServiceTrustBundleAuthorizationSet> authorization_set,
                 const std::string& expected_head_bundle_id, std::size_t maximum_bundles);

    std::filesystem::path directory_;
    std::uint32_t storage_schema_ = 1;
    std::string root_bundle_id_;
    std::string current_bundle_id_;
    std::vector<ServiceTrustBundle> bundles_;
    std::vector<ServiceTrustRotationRecord> records_;
    ServiceTrustHistoryLoadOptions options_;
};

Result<ServiceTrustCheckpointSignature>
sign_service_trust_checkpoint(const ServiceTrustHistory& history, std::string signer_service_id,
                              std::string signer_key_id, std::span<const std::byte> ed25519_secret_key);

Result<ServiceTrustCheckpoint>
assemble_service_trust_checkpoint(const ServiceTrustHistory& history,
                                  std::vector<ServiceTrustCheckpointSignature> signatures);

Result<void> verify_service_trust_checkpoint(const ServiceTrustHistory& history,
                                             const ServiceTrustCheckpoint& checkpoint,
                                             const std::string& expected_checkpoint_id);

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
