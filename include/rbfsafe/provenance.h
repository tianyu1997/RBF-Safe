#pragma once

#include <rbfsafe/identity.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class HardwareAttestationScope : std::uint8_t {
    ArtifactFetch = 0,
    ArtifactPublish = 1,
    TrustRotation = 2,
    DeploymentReview = 3,
    ExecutionControl = 4,
    RuntimeObservation = 5,
    TransparencyLog = 6,
    TransparencyWitness = 7,
    ExternalTime = 8,
};

struct HardwareAttestationAdapterPin {
    std::string adapter_id;
    std::string adapter_version;
    std::string statement_format;

    bool valid() const;
};

struct HardwareAttestationAuthority {
    std::string service_id;
    std::string key_id;

    bool valid() const;
};

struct HardwareKeyAttestationInput {
    std::uint64_t sequence = 1;
    std::string parent_statement_id;
    std::string subject_service_id;
    std::string subject_key_id;
    std::array<std::byte, kEd25519PublicKeyBytes> subject_public_key{};
    HardwareAttestationAdapterPin adapter;
    std::string vendor_id;
    std::string product_id;
    std::string evidence_digest;
    std::string nonce_digest;
    std::vector<HardwareAttestationScope> scopes;
};

struct HardwareKeyAttestationStatement {
    std::uint32_t storage_schema = 1;
    std::uint64_t sequence = 0;
    std::string id;
    std::string parent_statement_id;
    std::string subject_service_id;
    std::string subject_key_id;
    std::array<std::byte, kEd25519PublicKeyBytes> subject_public_key{};
    HardwareAttestationAdapterPin adapter;
    std::string vendor_id;
    std::string product_id;
    std::string evidence_digest;
    std::string nonce_digest;
    std::vector<HardwareAttestationScope> scopes;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string attester_service_id;
    std::string attester_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<HardwareKeyAttestationStatement>
sign_hardware_key_attestation_statement(HardwareKeyAttestationInput input,
                                        const ServiceTrustBundle& trust_bundle,
                                        std::string attester_service_id, std::string attester_key_id,
                                        std::span<const std::byte> ed25519_secret_key);

Result<void> verify_hardware_key_attestation_statement(const HardwareKeyAttestationStatement& statement,
                                                       const ServiceTrustBundle& trust_bundle);

struct HardwareKeyProvenancePolicy {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::uint32_t minimum_statements = 1;
    bool require_distinct_attesters = true;
    std::size_t maximum_chain_length = 1'000;
    std::vector<HardwareAttestationScope> required_scopes;
    std::vector<HardwareAttestationAdapterPin> allowed_adapters;
    std::vector<HardwareAttestationAuthority> allowed_authorities;
    std::vector<std::string> allowed_vendor_ids;

    static Result<HardwareKeyProvenancePolicy>
    create(std::uint32_t minimum_statements, bool require_distinct_attesters,
           std::size_t maximum_chain_length, std::vector<HardwareAttestationScope> required_scopes,
           std::vector<HardwareAttestationAdapterPin> allowed_adapters,
           std::vector<HardwareAttestationAuthority> allowed_authorities,
           std::vector<std::string> allowed_vendor_ids);

    bool valid() const;
};

enum class HardwareProvenanceStatus : std::uint8_t {
    Satisfied = 0,
    Incomplete = 1,
};

struct HardwareKeyProvenanceReport {
    std::string id;
    std::string subject_service_id;
    std::string subject_key_id;
    std::string trust_bundle_id;
    std::string policy_id;
    HardwareProvenanceStatus status = HardwareProvenanceStatus::Incomplete;
    std::size_t authenticated_statement_count = 0;
    std::size_t distinct_attester_count = 0;
    std::string head_statement_id;
    std::vector<std::string> statement_ids;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

struct ProvenanceReplayOptions {
    std::size_t maximum_statements = 1'000;
    std::size_t maximum_time_assertions = 10'000;
    CancellationToken cancellation;
};

Result<HardwareKeyProvenanceReport> replay_hardware_key_provenance(
    std::span<const HardwareKeyAttestationStatement> statements, const ServicePublicKey& expected_subject_key,
    const ServiceTrustBundle& trust_bundle, const HardwareKeyProvenancePolicy& policy,
    const ProvenanceReplayOptions& options = {});

struct ExternalTimeAssertionInput {
    std::uint64_t source_sequence = 1;
    std::string parent_assertion_id;
    std::string subject_id;
    std::string clock_id;
    std::uint64_t asserted_time_ns = 0;
    std::uint64_t uncertainty_ns = 0;
};

struct ExternalTimeAssertion {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::uint64_t source_sequence = 0;
    std::string parent_assertion_id;
    std::string subject_id;
    std::string clock_id;
    std::uint64_t asserted_time_ns = 0;
    std::uint64_t uncertainty_ns = 0;
    std::string trust_bundle_id;
    std::uint64_t trust_bundle_sequence = 0;
    std::string source_service_id;
    std::string source_key_id;
    ArtifactAuthenticationAlgorithm algorithm = ArtifactAuthenticationAlgorithm::Ed25519;
    std::string authentication_tag;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<ExternalTimeAssertion> sign_external_time_assertion(ExternalTimeAssertionInput input,
                                                           const ServiceTrustBundle& trust_bundle,
                                                           std::string source_service_id,
                                                           std::string source_key_id,
                                                           std::span<const std::byte> ed25519_secret_key);

Result<void> verify_external_time_assertion(const ExternalTimeAssertion& assertion,
                                            const ServiceTrustBundle& trust_bundle);

struct ExternalTimeSource {
    std::string service_id;
    std::string key_id;

    bool valid() const;
};

struct ExternalTimeFreshnessPolicy {
    std::uint32_t storage_schema = 1;
    std::string id;
    std::string clock_id;
    std::uint64_t maximum_age_ns = 0;
    std::uint64_t maximum_future_skew_ns = 0;
    std::uint64_t maximum_uncertainty_ns = 0;
    std::uint32_t minimum_sources = 1;
    bool require_distinct_services = true;
    std::size_t maximum_assertions = 10'000;
    std::vector<ExternalTimeSource> allowed_sources;

    static Result<ExternalTimeFreshnessPolicy>
    create(std::string clock_id, std::uint64_t maximum_age_ns, std::uint64_t maximum_future_skew_ns,
           std::uint64_t maximum_uncertainty_ns, std::uint32_t minimum_sources,
           bool require_distinct_services, std::size_t maximum_assertions,
           std::vector<ExternalTimeSource> allowed_sources);

    bool valid() const;
};

enum class ExternalTimeFreshnessStatus : std::uint8_t {
    Fresh = 0,
    Incomplete = 1,
    Stale = 2,
    Future = 3,
    Inconsistent = 4,
};

struct ExternalTimeFreshnessReport {
    std::string id;
    std::string subject_id;
    std::string trust_bundle_id;
    std::string policy_id;
    std::string clock_id;
    ExternalTimeFreshnessStatus status = ExternalTimeFreshnessStatus::Incomplete;
    std::uint64_t evaluated_at_ns = 0;
    std::uint64_t intersection_lower_ns = 0;
    std::uint64_t intersection_upper_ns = 0;
    std::size_t authenticated_source_count = 0;
    std::vector<std::string> assertion_ids;

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<ExternalTimeFreshnessReport>
evaluate_external_time_freshness(std::string subject_id, std::span<const ExternalTimeAssertion> assertions,
                                 const ServiceTrustBundle& trust_bundle,
                                 const ExternalTimeFreshnessPolicy& policy, std::uint64_t evaluated_at_ns,
                                 const ProvenanceReplayOptions& options = {});

struct VerifiableProvenanceBundleLoadOptions {
    std::size_t maximum_statements = 1'000;
    std::size_t maximum_time_assertions = 10'000;
    std::size_t maximum_policy_entries = 10'000;
    std::uintmax_t maximum_payload_bytes = 16'777'216ULL;
    CancellationToken cancellation;
};

class VerifiableProvenanceBundle {
  public:
    static Result<VerifiableProvenanceBundle>
    create(ServicePublicKey subject_key, HardwareKeyProvenancePolicy hardware_policy,
           ExternalTimeFreshnessPolicy freshness_policy,
           std::vector<HardwareKeyAttestationStatement> hardware_statements,
           std::vector<ExternalTimeAssertion> time_assertions, const ServiceTrustBundle& trust_bundle);

    std::uint32_t storage_schema() const noexcept { return storage_schema_; }
    const std::string& id() const noexcept { return id_; }
    const std::string& trust_bundle_id() const noexcept { return trust_bundle_id_; }
    std::uint64_t trust_bundle_sequence() const noexcept { return trust_bundle_sequence_; }
    const ServicePublicKey& subject_key() const noexcept { return subject_key_; }
    const HardwareKeyProvenancePolicy& hardware_policy() const noexcept { return hardware_policy_; }
    const ExternalTimeFreshnessPolicy& freshness_policy() const noexcept { return freshness_policy_; }
    const std::vector<HardwareKeyAttestationStatement>& hardware_statements() const noexcept {
        return hardware_statements_;
    }
    const std::vector<ExternalTimeAssertion>& time_assertions() const noexcept { return time_assertions_; }

    bool valid() const;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<VerifiableProvenanceBundle> load(const std::filesystem::path& path,
                                                   const VerifiableProvenanceBundleLoadOptions& options = {});

  private:
    friend Result<void> save_verifiable_provenance_bundle(const VerifiableProvenanceBundle&,
                                                          const std::filesystem::path&, const SaveOptions&);
    friend Result<VerifiableProvenanceBundle>
    load_verifiable_provenance_bundle(const std::filesystem::path&,
                                      const VerifiableProvenanceBundleLoadOptions&);

    std::uint32_t storage_schema_ = 1;
    std::string id_;
    std::string trust_bundle_id_;
    std::uint64_t trust_bundle_sequence_ = 0;
    ServicePublicKey subject_key_;
    HardwareKeyProvenancePolicy hardware_policy_;
    ExternalTimeFreshnessPolicy freshness_policy_;
    std::vector<HardwareKeyAttestationStatement> hardware_statements_;
    std::vector<ExternalTimeAssertion> time_assertions_;
};

struct VerifiableProvenanceAuditReport {
    std::string id;
    std::string bundle_id;
    HardwareKeyProvenanceReport hardware;
    ExternalTimeFreshnessReport freshness;

    bool valid() const;
    bool ready() const noexcept;
    EvidenceLevel evidence() const noexcept { return EvidenceLevel::Unknown; }
    bool authorizes_execution() const noexcept { return false; }
};

Result<VerifiableProvenanceAuditReport>
replay_verifiable_provenance(const VerifiableProvenanceBundle& bundle, const ServiceTrustBundle& trust_bundle,
                             std::uint64_t evaluated_at_ns, const ProvenanceReplayOptions& options = {});

Result<void> save_verifiable_provenance_bundle(const VerifiableProvenanceBundle& bundle,
                                               const std::filesystem::path& path, const SaveOptions& options);
Result<VerifiableProvenanceBundle>
load_verifiable_provenance_bundle(const std::filesystem::path& path,
                                  const VerifiableProvenanceBundleLoadOptions& options);

std::string hardware_attestation_scope_name(HardwareAttestationScope scope);
std::string hardware_provenance_status_name(HardwareProvenanceStatus status);
std::string external_time_freshness_status_name(ExternalTimeFreshnessStatus status);

} // namespace rbfsafe
