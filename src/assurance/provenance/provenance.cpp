#include <rbfsafe/modules/assurance.h>
#include <rbfsafe/modules/core.h>

#include "internal/certificate_utils.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/provenance.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace rbfsafe {
namespace {

using internal::Json;

constexpr std::uint32_t kStorageSchema = 1;
constexpr std::size_t kMaximumTextBytes = 256;
constexpr std::size_t kMaximumPolicyEntries = 10'000;

bool valid_text(std::string_view value, std::size_t maximum = kMaximumTextBytes) {
    return !value.empty() && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char character) { return character >= 0x20U && character != 0x7fU; });
}

bool valid_authentication_tag(std::string_view value) {
    return value.size() == kEd25519SignatureBytes * 2U &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isdigit(character) != 0 || (character >= static_cast<unsigned char>('a') &&
                                                       character <= static_cast<unsigned char>('f'));
           });
}

bool valid_scope(HardwareAttestationScope scope) {
    return scope >= HardwareAttestationScope::ArtifactFetch &&
           scope <= HardwareAttestationScope::ExternalTime;
}

bool valid_hardware_status(HardwareProvenanceStatus status) {
    return status == HardwareProvenanceStatus::Satisfied || status == HardwareProvenanceStatus::Incomplete;
}

bool valid_freshness_status(ExternalTimeFreshnessStatus status) {
    return status >= ExternalTimeFreshnessStatus::Fresh &&
           status <= ExternalTimeFreshnessStatus::Inconsistent;
}

template <typename T, typename Less> bool sorted_unique(const std::vector<T>& values, Less less) {
    for (std::size_t index = 1; index < values.size(); ++index) {
        if (!less(values[index - 1], values[index]))
            return false;
    }
    return true;
}

bool sorted_unique_scopes(const std::vector<HardwareAttestationScope>& scopes) {
    return !scopes.empty() && sorted_unique(scopes, [](auto first, auto second) {
        return static_cast<std::uint8_t>(first) < static_cast<std::uint8_t>(second);
    }) && std::all_of(scopes.begin(), scopes.end(), valid_scope);
}

void canonicalize_scopes(std::vector<HardwareAttestationScope>& scopes) {
    std::sort(scopes.begin(), scopes.end(), [](auto first, auto second) {
        return static_cast<std::uint8_t>(first) < static_cast<std::uint8_t>(second);
    });
    scopes.erase(std::unique(scopes.begin(), scopes.end()), scopes.end());
}

Json adapter_json(const HardwareAttestationAdapterPin& adapter) {
    return Json::Object{
        {"adapter_id", adapter.adapter_id},
        {"adapter_version", adapter.adapter_version},
        {"statement_format", adapter.statement_format},
    };
}

Json authority_json(const HardwareAttestationAuthority& authority) {
    return Json::Object{
        {"key_id", authority.key_id},
        {"service_id", authority.service_id},
    };
}

Json scope_json(const std::vector<HardwareAttestationScope>& scopes) {
    Json::Array array;
    array.reserve(scopes.size());
    for (const auto scope : scopes)
        array.emplace_back(static_cast<int>(scope));
    return Json(std::move(array));
}

Json hardware_statement_json(const HardwareKeyAttestationStatement& statement, bool include_id,
                             bool include_tag) {
    Json::Object object{
        {"adapter", adapter_json(statement.adapter)},
        {"algorithm", static_cast<int>(statement.algorithm)},
        {"attester_key_id", statement.attester_key_id},
        {"attester_service_id", statement.attester_service_id},
        {"evidence_digest", statement.evidence_digest},
        {"nonce_digest", statement.nonce_digest},
        {"parent_statement_id", statement.parent_statement_id},
        {"product_id", statement.product_id},
        {"scopes", scope_json(statement.scopes)},
        {"sequence", std::to_string(statement.sequence)},
        {"storage_schema", std::to_string(statement.storage_schema)},
        {"subject_key_id", statement.subject_key_id},
        {"subject_public_key", internal::encode_hex(statement.subject_public_key)},
        {"subject_service_id", statement.subject_service_id},
        {"trust_bundle_id", statement.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(statement.trust_bundle_sequence)},
        {"vendor_id", statement.vendor_id},
    };
    if (include_id)
        object.emplace("id", statement.id);
    if (include_tag)
        object.emplace("authentication_tag", statement.authentication_tag);
    return Json(std::move(object));
}

Json hardware_policy_json(const HardwareKeyProvenancePolicy& policy, bool include_id) {
    Json::Array adapters;
    adapters.reserve(policy.allowed_adapters.size());
    for (const auto& adapter : policy.allowed_adapters)
        adapters.emplace_back(adapter_json(adapter));
    Json::Array authorities;
    authorities.reserve(policy.allowed_authorities.size());
    for (const auto& authority : policy.allowed_authorities)
        authorities.emplace_back(authority_json(authority));
    Json::Array vendors;
    vendors.reserve(policy.allowed_vendor_ids.size());
    for (const auto& vendor : policy.allowed_vendor_ids)
        vendors.emplace_back(vendor);
    Json::Object object{
        {"allowed_adapters", std::move(adapters)},
        {"allowed_authorities", std::move(authorities)},
        {"allowed_vendor_ids", std::move(vendors)},
        {"maximum_chain_length", std::to_string(policy.maximum_chain_length)},
        {"minimum_statements", std::to_string(policy.minimum_statements)},
        {"required_scopes", scope_json(policy.required_scopes)},
        {"require_distinct_attesters", policy.require_distinct_attesters},
        {"storage_schema", std::to_string(policy.storage_schema)},
    };
    if (include_id)
        object.emplace("id", policy.id);
    return Json(std::move(object));
}

Json hardware_report_json(const HardwareKeyProvenanceReport& report, bool include_id) {
    Json::Array ids;
    ids.reserve(report.statement_ids.size());
    for (const auto& id : report.statement_ids)
        ids.emplace_back(id);
    Json::Object object{
        {"authenticated_statement_count", std::to_string(report.authenticated_statement_count)},
        {"distinct_attester_count", std::to_string(report.distinct_attester_count)},
        {"head_statement_id", report.head_statement_id},
        {"policy_id", report.policy_id},
        {"statement_ids", std::move(ids)},
        {"status", static_cast<int>(report.status)},
        {"subject_key_id", report.subject_key_id},
        {"subject_service_id", report.subject_service_id},
        {"trust_bundle_id", report.trust_bundle_id},
    };
    if (include_id)
        object.emplace("id", report.id);
    return Json(std::move(object));
}

Json time_assertion_json(const ExternalTimeAssertion& assertion, bool include_id, bool include_tag) {
    Json::Object object{
        {"algorithm", static_cast<int>(assertion.algorithm)},
        {"asserted_time_ns", std::to_string(assertion.asserted_time_ns)},
        {"clock_id", assertion.clock_id},
        {"parent_assertion_id", assertion.parent_assertion_id},
        {"source_key_id", assertion.source_key_id},
        {"source_sequence", std::to_string(assertion.source_sequence)},
        {"source_service_id", assertion.source_service_id},
        {"storage_schema", std::to_string(assertion.storage_schema)},
        {"subject_id", assertion.subject_id},
        {"trust_bundle_id", assertion.trust_bundle_id},
        {"trust_bundle_sequence", std::to_string(assertion.trust_bundle_sequence)},
        {"uncertainty_ns", std::to_string(assertion.uncertainty_ns)},
    };
    if (include_id)
        object.emplace("id", assertion.id);
    if (include_tag)
        object.emplace("authentication_tag", assertion.authentication_tag);
    return Json(std::move(object));
}

Json time_source_json(const ExternalTimeSource& source) {
    return Json::Object{
        {"key_id", source.key_id},
        {"service_id", source.service_id},
    };
}

Json freshness_policy_json(const ExternalTimeFreshnessPolicy& policy, bool include_id) {
    Json::Array sources;
    sources.reserve(policy.allowed_sources.size());
    for (const auto& source : policy.allowed_sources)
        sources.emplace_back(time_source_json(source));
    Json::Object object{
        {"allowed_sources", std::move(sources)},
        {"clock_id", policy.clock_id},
        {"maximum_age_ns", std::to_string(policy.maximum_age_ns)},
        {"maximum_assertions", std::to_string(policy.maximum_assertions)},
        {"maximum_future_skew_ns", std::to_string(policy.maximum_future_skew_ns)},
        {"maximum_uncertainty_ns", std::to_string(policy.maximum_uncertainty_ns)},
        {"minimum_sources", std::to_string(policy.minimum_sources)},
        {"require_distinct_services", policy.require_distinct_services},
        {"storage_schema", std::to_string(policy.storage_schema)},
    };
    if (include_id)
        object.emplace("id", policy.id);
    return Json(std::move(object));
}

Json freshness_report_json(const ExternalTimeFreshnessReport& report, bool include_id) {
    Json::Array ids;
    ids.reserve(report.assertion_ids.size());
    for (const auto& id : report.assertion_ids)
        ids.emplace_back(id);
    Json::Object object{
        {"assertion_ids", std::move(ids)},
        {"authenticated_source_count", std::to_string(report.authenticated_source_count)},
        {"clock_id", report.clock_id},
        {"evaluated_at_ns", std::to_string(report.evaluated_at_ns)},
        {"intersection_lower_ns", std::to_string(report.intersection_lower_ns)},
        {"intersection_upper_ns", std::to_string(report.intersection_upper_ns)},
        {"policy_id", report.policy_id},
        {"status", static_cast<int>(report.status)},
        {"subject_id", report.subject_id},
        {"trust_bundle_id", report.trust_bundle_id},
    };
    if (include_id)
        object.emplace("id", report.id);
    return Json(std::move(object));
}

Json service_public_key_json(const ServicePublicKey& key) {
    return Json::Object{
        {"algorithm", static_cast<int>(key.algorithm)},
        {"allow_fetch", key.allow_fetch},
        {"allow_publish", key.allow_publish},
        {"allow_rotate", key.allow_rotate},
        {"id", key.id},
        {"public_key", internal::encode_hex(key.public_key)},
        {"service_id", key.service_id},
        {"state", static_cast<int>(key.state)},
        {"valid_from_sequence", std::to_string(key.valid_from_sequence)},
        {"valid_through_sequence", std::to_string(key.valid_through_sequence)},
    };
}

Result<ServicePublicKey> active_publication_key(const ServiceTrustBundle& trust_bundle,
                                                const std::string& service_id, const std::string& key_id) {
    auto trusted = trusted_service_public_key(trust_bundle, service_id, key_id,
                                              ArtifactTransferOperation::Publish, trust_bundle.sequence());
    if (!trusted)
        return trusted.error();
    if (trusted.value().state != ServiceKeyState::Active) {
        return Result<ServicePublicKey>::failure(StatusCode::IdentityMismatch,
                                                 "provenance signer key is not active", key_id);
    }
    return trusted;
}

bool contains_scope(const std::vector<HardwareAttestationScope>& scopes, HardwareAttestationScope required) {
    return std::binary_search(scopes.begin(), scopes.end(), required, [](auto first, auto second) {
        return static_cast<std::uint8_t>(first) < static_cast<std::uint8_t>(second);
    });
}

bool adapter_allowed(const HardwareKeyProvenancePolicy& policy,
                     const HardwareAttestationAdapterPin& adapter) {
    return std::binary_search(
        policy.allowed_adapters.begin(), policy.allowed_adapters.end(), adapter,
        [](const auto& first, const auto& second) {
            return std::tie(first.adapter_id, first.adapter_version, first.statement_format) <
                   std::tie(second.adapter_id, second.adapter_version, second.statement_format);
        });
}

bool authority_allowed(const HardwareKeyProvenancePolicy& policy,
                       const HardwareKeyAttestationStatement& statement) {
    const HardwareAttestationAuthority authority{statement.attester_service_id, statement.attester_key_id};
    return std::binary_search(policy.allowed_authorities.begin(), policy.allowed_authorities.end(), authority,
                              [](const auto& first, const auto& second) {
                                  return std::tie(first.service_id, first.key_id) <
                                         std::tie(second.service_id, second.key_id);
                              });
}

bool time_source_allowed(const ExternalTimeFreshnessPolicy& policy, const ExternalTimeAssertion& assertion) {
    const ExternalTimeSource source{assertion.source_service_id, assertion.source_key_id};
    return std::binary_search(policy.allowed_sources.begin(), policy.allowed_sources.end(), source,
                              [](const auto& first, const auto& second) {
                                  return std::tie(first.service_id, first.key_id) <
                                         std::tie(second.service_id, second.key_id);
                              });
}

std::uint64_t saturating_add(std::uint64_t first, std::uint64_t second) {
    if (second > std::numeric_limits<std::uint64_t>::max() - first)
        return std::numeric_limits<std::uint64_t>::max();
    return first + second;
}

} // namespace

namespace internal {

std::string hardware_key_attestation_message(const HardwareKeyAttestationStatement& statement) {
    return std::string("rbfsafe.hardware-key-attestation.signature.v1\n") +
           hardware_statement_json(statement, false, false).dump(false);
}

std::string hardware_key_attestation_identity(const HardwareKeyAttestationStatement& statement) {
    return sha256(std::string("rbfsafe.hardware-key-attestation.identity.v1\n") +
                  hardware_statement_json(statement, false, true).dump(false));
}

std::string hardware_key_provenance_policy_identity(const HardwareKeyProvenancePolicy& policy) {
    return sha256(std::string("rbfsafe.hardware-key-provenance-policy.v1\n") +
                  hardware_policy_json(policy, false).dump(false));
}

std::string hardware_key_provenance_report_identity(const HardwareKeyProvenanceReport& report) {
    return sha256(std::string("rbfsafe.hardware-key-provenance-report.v1\n") +
                  hardware_report_json(report, false).dump(false));
}

std::string external_time_assertion_message(const ExternalTimeAssertion& assertion) {
    return std::string("rbfsafe.external-time-assertion.signature.v1\n") +
           time_assertion_json(assertion, false, false).dump(false);
}

std::string external_time_assertion_identity(const ExternalTimeAssertion& assertion) {
    return sha256(std::string("rbfsafe.external-time-assertion.identity.v1\n") +
                  time_assertion_json(assertion, false, true).dump(false));
}

std::string external_time_freshness_policy_identity(const ExternalTimeFreshnessPolicy& policy) {
    return sha256(std::string("rbfsafe.external-time-freshness-policy.v1\n") +
                  freshness_policy_json(policy, false).dump(false));
}

std::string external_time_freshness_report_identity(const ExternalTimeFreshnessReport& report) {
    return sha256(std::string("rbfsafe.external-time-freshness-report.v1\n") +
                  freshness_report_json(report, false).dump(false));
}

std::string verifiable_provenance_bundle_identity(const VerifiableProvenanceBundle& bundle) {
    Json::Array statements;
    statements.reserve(bundle.hardware_statements().size());
    for (const auto& statement : bundle.hardware_statements())
        statements.emplace_back(statement.id);
    Json::Array assertions;
    assertions.reserve(bundle.time_assertions().size());
    for (const auto& assertion : bundle.time_assertions())
        assertions.emplace_back(assertion.id);
    const Json payload = Json::Object{
        {"freshness_policy_id", bundle.freshness_policy().id},
        {"hardware_policy_id", bundle.hardware_policy().id},
        {"hardware_statement_ids", std::move(statements)},
        {"storage_schema", std::to_string(bundle.storage_schema())},
        {"subject_key", service_public_key_json(bundle.subject_key())},
        {"time_assertion_ids", std::move(assertions)},
        {"trust_bundle_id", bundle.trust_bundle_id()},
        {"trust_bundle_sequence", std::to_string(bundle.trust_bundle_sequence())},
    };
    return sha256(std::string("rbfsafe.verifiable-provenance-bundle.v1\n") + payload.dump(false));
}

std::string verifiable_provenance_audit_report_identity(const VerifiableProvenanceAuditReport& report) {
    const Json payload = Json::Object{
        {"bundle_id", report.bundle_id},
        {"freshness_report_id", report.freshness.id},
        {"hardware_report_id", report.hardware.id},
    };
    return sha256(std::string("rbfsafe.verifiable-provenance-audit-report.v1\n") + payload.dump(false));
}

} // namespace internal

bool HardwareAttestationAdapterPin::valid() const {
    return valid_text(adapter_id) && valid_text(adapter_version) && valid_text(statement_format);
}

bool HardwareAttestationAuthority::valid() const {
    return valid_text(service_id) && internal::valid_sha256(key_id);
}

bool HardwareKeyAttestationStatement::valid() const {
    const bool parent_valid =
        sequence == 1 ? parent_statement_id.empty() : internal::valid_sha256(parent_statement_id);
    return storage_schema == kStorageSchema && sequence > 0 && internal::valid_sha256(id) && parent_valid &&
           valid_text(subject_service_id) && internal::valid_sha256(subject_key_id) && adapter.valid() &&
           valid_text(vendor_id) && valid_text(product_id) && internal::valid_sha256(evidence_digest) &&
           internal::valid_sha256(nonce_digest) && sorted_unique_scopes(scopes) &&
           internal::valid_sha256(trust_bundle_id) && trust_bundle_sequence > 0 &&
           valid_text(attester_service_id) && internal::valid_sha256(attester_key_id) &&
           algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           valid_authentication_tag(authentication_tag) &&
           id == internal::hardware_key_attestation_identity(*this);
}

Result<HardwareKeyAttestationStatement>
sign_hardware_key_attestation_statement(HardwareKeyAttestationInput input,
                                        const ServiceTrustBundle& trust_bundle,
                                        std::string attester_service_id, std::string attester_key_id,
                                        std::span<const std::byte> ed25519_secret_key) {
    canonicalize_scopes(input.scopes);
    if (!trust_bundle.valid() || input.sequence == 0 ||
        (input.sequence == 1 ? !input.parent_statement_id.empty()
                             : !internal::valid_sha256(input.parent_statement_id)) ||
        !valid_text(input.subject_service_id) || !internal::valid_sha256(input.subject_key_id) ||
        !input.adapter.valid() || !valid_text(input.vendor_id) || !valid_text(input.product_id) ||
        !internal::valid_sha256(input.evidence_digest) || !internal::valid_sha256(input.nonce_digest) ||
        !sorted_unique_scopes(input.scopes) || !valid_text(attester_service_id) ||
        !internal::valid_sha256(attester_key_id) || ed25519_secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<HardwareKeyAttestationStatement>::failure(
            StatusCode::InvalidArgument, "hardware-key attestation signing input is invalid");
    }
    auto trusted = active_publication_key(trust_bundle, attester_service_id, attester_key_id);
    if (!trusted)
        return trusted.error();

    HardwareKeyAttestationStatement statement;
    statement.sequence = input.sequence;
    statement.parent_statement_id = std::move(input.parent_statement_id);
    statement.subject_service_id = std::move(input.subject_service_id);
    statement.subject_key_id = std::move(input.subject_key_id);
    statement.subject_public_key = input.subject_public_key;
    statement.adapter = std::move(input.adapter);
    statement.vendor_id = std::move(input.vendor_id);
    statement.product_id = std::move(input.product_id);
    statement.evidence_digest = std::move(input.evidence_digest);
    statement.nonce_digest = std::move(input.nonce_digest);
    statement.scopes = std::move(input.scopes);
    statement.trust_bundle_id = trust_bundle.id();
    statement.trust_bundle_sequence = trust_bundle.sequence();
    statement.attester_service_id = std::move(attester_service_id);
    statement.attester_key_id = std::move(attester_key_id);
    const auto message = internal::hardware_key_attestation_message(statement);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    statement.authentication_tag = internal::encode_hex(signature.value());
    statement.id = internal::hardware_key_attestation_identity(statement);
    auto verified = verify_hardware_key_attestation_statement(statement, trust_bundle);
    if (!verified)
        return verified.error();
    return statement;
}

Result<void> verify_hardware_key_attestation_statement(const HardwareKeyAttestationStatement& statement,
                                                       const ServiceTrustBundle& trust_bundle) {
    if (!statement.valid() || !trust_bundle.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "hardware-key attestation verification input is invalid");
    }
    if (statement.trust_bundle_id != trust_bundle.id() ||
        statement.trust_bundle_sequence != trust_bundle.sequence()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "hardware-key attestation trust binding mismatch", statement.id);
    }
    auto trusted =
        active_publication_key(trust_bundle, statement.attester_service_id, statement.attester_key_id);
    if (!trusted)
        return trusted.error();
    auto signature = internal::decode_hex(statement.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::hardware_key_attestation_message(statement);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          trusted.value().public_key);
}

Result<HardwareKeyProvenancePolicy> HardwareKeyProvenancePolicy::create(
    std::uint32_t minimum_statements_value, bool require_distinct_attesters_value,
    std::size_t maximum_chain_length_value, std::vector<HardwareAttestationScope> required_scopes_value,
    std::vector<HardwareAttestationAdapterPin> allowed_adapters_value,
    std::vector<HardwareAttestationAuthority> allowed_authorities_value,
    std::vector<std::string> allowed_vendor_ids_value) {
    canonicalize_scopes(required_scopes_value);
    std::sort(allowed_adapters_value.begin(), allowed_adapters_value.end(),
              [](const auto& first, const auto& second) {
                  return std::tie(first.adapter_id, first.adapter_version, first.statement_format) <
                         std::tie(second.adapter_id, second.adapter_version, second.statement_format);
              });
    allowed_adapters_value.erase(std::unique(allowed_adapters_value.begin(), allowed_adapters_value.end(),
                                             [](const auto& first, const auto& second) {
                                                 return first.adapter_id == second.adapter_id &&
                                                        first.adapter_version == second.adapter_version &&
                                                        first.statement_format == second.statement_format;
                                             }),
                                 allowed_adapters_value.end());
    std::sort(allowed_authorities_value.begin(), allowed_authorities_value.end(),
              [](const auto& first, const auto& second) {
                  return std::tie(first.service_id, first.key_id) <
                         std::tie(second.service_id, second.key_id);
              });
    allowed_authorities_value.erase(
        std::unique(allowed_authorities_value.begin(), allowed_authorities_value.end(),
                    [](const auto& first, const auto& second) {
                        return first.service_id == second.service_id && first.key_id == second.key_id;
                    }),
        allowed_authorities_value.end());
    std::sort(allowed_vendor_ids_value.begin(), allowed_vendor_ids_value.end());
    allowed_vendor_ids_value.erase(
        std::unique(allowed_vendor_ids_value.begin(), allowed_vendor_ids_value.end()),
        allowed_vendor_ids_value.end());

    HardwareKeyProvenancePolicy policy;
    policy.minimum_statements = minimum_statements_value;
    policy.require_distinct_attesters = require_distinct_attesters_value;
    policy.maximum_chain_length = maximum_chain_length_value;
    policy.required_scopes = std::move(required_scopes_value);
    policy.allowed_adapters = std::move(allowed_adapters_value);
    policy.allowed_authorities = std::move(allowed_authorities_value);
    policy.allowed_vendor_ids = std::move(allowed_vendor_ids_value);
    policy.id = internal::hardware_key_provenance_policy_identity(policy);
    if (!policy.valid()) {
        return Result<HardwareKeyProvenancePolicy>::failure(StatusCode::InvalidArgument,
                                                            "hardware-key provenance policy is invalid");
    }
    return policy;
}

bool HardwareKeyProvenancePolicy::valid() const {
    if (storage_schema != kStorageSchema || !internal::valid_sha256(id) || minimum_statements == 0 ||
        maximum_chain_length == 0 || maximum_chain_length > 1'000'000 ||
        minimum_statements > maximum_chain_length || required_scopes.empty() || allowed_adapters.empty() ||
        allowed_authorities.empty() || allowed_vendor_ids.empty() ||
        required_scopes.size() > kMaximumPolicyEntries || allowed_adapters.size() > kMaximumPolicyEntries ||
        allowed_authorities.size() > kMaximumPolicyEntries ||
        allowed_vendor_ids.size() > kMaximumPolicyEntries || !sorted_unique_scopes(required_scopes)) {
        return false;
    }
    if (!sorted_unique(allowed_adapters,
                       [](const auto& first, const auto& second) {
                           return std::tie(first.adapter_id, first.adapter_version, first.statement_format) <
                                  std::tie(second.adapter_id, second.adapter_version,
                                           second.statement_format);
                       }) ||
        !std::all_of(allowed_adapters.begin(), allowed_adapters.end(),
                     [](const auto& value) { return value.valid(); }) ||
        !sorted_unique(allowed_authorities,
                       [](const auto& first, const auto& second) {
                           return std::tie(first.service_id, first.key_id) <
                                  std::tie(second.service_id, second.key_id);
                       }) ||
        !std::all_of(allowed_authorities.begin(), allowed_authorities.end(),
                     [](const auto& value) { return value.valid(); }) ||
        !sorted_unique(allowed_vendor_ids,
                       [](const auto& first, const auto& second) { return first < second; }) ||
        !std::all_of(allowed_vendor_ids.begin(), allowed_vendor_ids.end(),
                     [](const auto& value) { return valid_text(value); })) {
        return false;
    }
    return id == internal::hardware_key_provenance_policy_identity(*this);
}

bool HardwareKeyProvenanceReport::valid() const {
    if (!internal::valid_sha256(id) || !valid_text(subject_service_id) ||
        !internal::valid_sha256(subject_key_id) || !internal::valid_sha256(trust_bundle_id) ||
        !internal::valid_sha256(policy_id) || !valid_hardware_status(status) ||
        authenticated_statement_count != statement_ids.size() ||
        distinct_attester_count > authenticated_statement_count ||
        !sorted_unique(statement_ids, [](const auto& first, const auto& second) { return first < second; }) ||
        !std::all_of(statement_ids.begin(), statement_ids.end(), internal::valid_sha256)) {
        return false;
    }
    if (statement_ids.empty()) {
        if (!head_statement_id.empty())
            return false;
    } else if (!internal::valid_sha256(head_statement_id)) {
        return false;
    }
    return id == internal::hardware_key_provenance_report_identity(*this);
}

Result<HardwareKeyProvenanceReport> replay_hardware_key_provenance(
    std::span<const HardwareKeyAttestationStatement> statements, const ServicePublicKey& expected_subject_key,
    const ServiceTrustBundle& trust_bundle, const HardwareKeyProvenancePolicy& policy,
    const ProvenanceReplayOptions& options) {
    if (!valid_service_public_key(expected_subject_key) || !trust_bundle.valid() || !policy.valid() ||
        options.maximum_statements == 0) {
        return Result<HardwareKeyProvenanceReport>::failure(
            StatusCode::InvalidArgument, "hardware-key provenance replay input or options are invalid");
    }
    const auto maximum = std::min(options.maximum_statements, policy.maximum_chain_length);
    if (statements.size() > maximum) {
        return Result<HardwareKeyProvenanceReport>::failure(
            StatusCode::ResourceLimit, "hardware-key provenance chain exceeds configured limit");
    }
    if (options.cancellation.cancelled()) {
        return Result<HardwareKeyProvenanceReport>::failure(StatusCode::Cancelled,
                                                            "hardware-key provenance replay was cancelled");
    }

    std::vector<const HardwareKeyAttestationStatement*> ordered;
    ordered.reserve(statements.size());
    for (const auto& statement : statements)
        ordered.push_back(&statement);
    std::sort(ordered.begin(), ordered.end(), [](const auto* first, const auto* second) {
        return std::tie(first->sequence, first->id) < std::tie(second->sequence, second->id);
    });

    std::set<std::string> attesters;
    std::set<std::string> ids;
    std::vector<std::string> report_ids;
    report_ids.reserve(ordered.size());
    std::string parent;
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        if (options.cancellation.cancelled()) {
            return Result<HardwareKeyProvenanceReport>::failure(
                StatusCode::Cancelled, "hardware-key provenance replay was cancelled");
        }
        const auto& statement = *ordered[index];
        auto verified = verify_hardware_key_attestation_statement(statement, trust_bundle);
        if (!verified)
            return verified.error();
        if (!ids.insert(statement.id).second || statement.sequence != static_cast<std::uint64_t>(index + 1) ||
            statement.parent_statement_id != parent) {
            return Result<HardwareKeyProvenanceReport>::failure(
                StatusCode::IdentityMismatch,
                "hardware-key provenance chain is not a contiguous append-only history", statement.id);
        }
        if (statement.subject_service_id != expected_subject_key.service_id ||
            statement.subject_key_id != expected_subject_key.id ||
            statement.subject_public_key != expected_subject_key.public_key) {
            return Result<HardwareKeyProvenanceReport>::failure(
                StatusCode::IdentityMismatch,
                "hardware-key provenance subject does not match the expected key", statement.id);
        }
        if (!adapter_allowed(policy, statement.adapter) || !authority_allowed(policy, statement) ||
            !std::binary_search(policy.allowed_vendor_ids.begin(), policy.allowed_vendor_ids.end(),
                                statement.vendor_id)) {
            return Result<HardwareKeyProvenanceReport>::failure(
                StatusCode::IdentityMismatch,
                "hardware-key provenance statement is outside the explicit policy", statement.id);
        }
        for (const auto required : policy.required_scopes) {
            if (!contains_scope(statement.scopes, required)) {
                return Result<HardwareKeyProvenanceReport>::failure(
                    StatusCode::IdentityMismatch, "hardware-key provenance statement omits a required scope",
                    statement.id);
            }
        }
        attesters.insert(policy.require_distinct_attesters
                             ? statement.attester_service_id
                             : statement.attester_service_id + "/" + statement.attester_key_id);
        report_ids.push_back(statement.id);
        parent = statement.id;
    }
    std::sort(report_ids.begin(), report_ids.end());

    HardwareKeyProvenanceReport report;
    report.subject_service_id = expected_subject_key.service_id;
    report.subject_key_id = expected_subject_key.id;
    report.trust_bundle_id = trust_bundle.id();
    report.policy_id = policy.id;
    report.authenticated_statement_count = statements.size();
    report.distinct_attester_count = attesters.size();
    report.head_statement_id = parent;
    report.statement_ids = std::move(report_ids);
    const bool count_satisfied = report.authenticated_statement_count >= policy.minimum_statements;
    const bool distinct_satisfied =
        !policy.require_distinct_attesters || report.distinct_attester_count >= policy.minimum_statements;
    report.status = count_satisfied && distinct_satisfied ? HardwareProvenanceStatus::Satisfied
                                                          : HardwareProvenanceStatus::Incomplete;
    report.id = internal::hardware_key_provenance_report_identity(report);
    if (!report.valid()) {
        return Result<HardwareKeyProvenanceReport>::failure(
            StatusCode::InternalError, "hardware-key provenance report is not canonical");
    }
    return report;
}

bool ExternalTimeAssertion::valid() const {
    const bool parent_valid =
        source_sequence == 1 ? parent_assertion_id.empty() : internal::valid_sha256(parent_assertion_id);
    return storage_schema == kStorageSchema && internal::valid_sha256(id) && source_sequence > 0 &&
           parent_valid && internal::valid_sha256(subject_id) && valid_text(clock_id) &&
           internal::valid_sha256(trust_bundle_id) && trust_bundle_sequence > 0 &&
           valid_text(source_service_id) && internal::valid_sha256(source_key_id) &&
           algorithm == ArtifactAuthenticationAlgorithm::Ed25519 &&
           valid_authentication_tag(authentication_tag) &&
           id == internal::external_time_assertion_identity(*this);
}

Result<ExternalTimeAssertion> sign_external_time_assertion(ExternalTimeAssertionInput input,
                                                           const ServiceTrustBundle& trust_bundle,
                                                           std::string source_service_id,
                                                           std::string source_key_id,
                                                           std::span<const std::byte> ed25519_secret_key) {
    if (!trust_bundle.valid() || input.source_sequence == 0 ||
        (input.source_sequence == 1 ? !input.parent_assertion_id.empty()
                                    : !internal::valid_sha256(input.parent_assertion_id)) ||
        !internal::valid_sha256(input.subject_id) || !valid_text(input.clock_id) ||
        !valid_text(source_service_id) || !internal::valid_sha256(source_key_id) ||
        ed25519_secret_key.size() != kEd25519SecretKeyBytes) {
        return Result<ExternalTimeAssertion>::failure(StatusCode::InvalidArgument,
                                                      "external time assertion signing input is invalid");
    }
    auto trusted = active_publication_key(trust_bundle, source_service_id, source_key_id);
    if (!trusted)
        return trusted.error();
    ExternalTimeAssertion assertion;
    assertion.source_sequence = input.source_sequence;
    assertion.parent_assertion_id = std::move(input.parent_assertion_id);
    assertion.subject_id = std::move(input.subject_id);
    assertion.clock_id = std::move(input.clock_id);
    assertion.asserted_time_ns = input.asserted_time_ns;
    assertion.uncertainty_ns = input.uncertainty_ns;
    assertion.trust_bundle_id = trust_bundle.id();
    assertion.trust_bundle_sequence = trust_bundle.sequence();
    assertion.source_service_id = std::move(source_service_id);
    assertion.source_key_id = std::move(source_key_id);
    const auto message = internal::external_time_assertion_message(assertion);
    auto signature =
        ed25519_sign(std::as_bytes(std::span(message.data(), message.size())), ed25519_secret_key);
    if (!signature)
        return signature.error();
    assertion.authentication_tag = internal::encode_hex(signature.value());
    assertion.id = internal::external_time_assertion_identity(assertion);
    auto verified = verify_external_time_assertion(assertion, trust_bundle);
    if (!verified)
        return verified.error();
    return assertion;
}

Result<void> verify_external_time_assertion(const ExternalTimeAssertion& assertion,
                                            const ServiceTrustBundle& trust_bundle) {
    if (!assertion.valid() || !trust_bundle.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "external time assertion verification input is invalid");
    }
    if (assertion.trust_bundle_id != trust_bundle.id() ||
        assertion.trust_bundle_sequence != trust_bundle.sequence()) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "external time assertion trust binding mismatch", assertion.id);
    }
    auto trusted = active_publication_key(trust_bundle, assertion.source_service_id, assertion.source_key_id);
    if (!trusted)
        return trusted.error();
    auto signature = internal::decode_hex(assertion.authentication_tag, kEd25519SignatureBytes);
    if (!signature)
        return signature.error();
    const auto message = internal::external_time_assertion_message(assertion);
    return ed25519_verify(std::as_bytes(std::span(message.data(), message.size())), signature.value(),
                          trusted.value().public_key);
}

bool ExternalTimeSource::valid() const { return valid_text(service_id) && internal::valid_sha256(key_id); }

Result<ExternalTimeFreshnessPolicy> ExternalTimeFreshnessPolicy::create(
    std::string clock_id_value, std::uint64_t maximum_age_ns_value,
    std::uint64_t maximum_future_skew_ns_value, std::uint64_t maximum_uncertainty_ns_value,
    std::uint32_t minimum_sources_value, bool require_distinct_services_value,
    std::size_t maximum_assertions_value, std::vector<ExternalTimeSource> allowed_sources_value) {
    std::sort(allowed_sources_value.begin(), allowed_sources_value.end(),
              [](const auto& first, const auto& second) {
                  return std::tie(first.service_id, first.key_id) <
                         std::tie(second.service_id, second.key_id);
              });
    allowed_sources_value.erase(std::unique(allowed_sources_value.begin(), allowed_sources_value.end(),
                                            [](const auto& first, const auto& second) {
                                                return first.service_id == second.service_id &&
                                                       first.key_id == second.key_id;
                                            }),
                                allowed_sources_value.end());
    ExternalTimeFreshnessPolicy policy;
    policy.clock_id = std::move(clock_id_value);
    policy.maximum_age_ns = maximum_age_ns_value;
    policy.maximum_future_skew_ns = maximum_future_skew_ns_value;
    policy.maximum_uncertainty_ns = maximum_uncertainty_ns_value;
    policy.minimum_sources = minimum_sources_value;
    policy.require_distinct_services = require_distinct_services_value;
    policy.maximum_assertions = maximum_assertions_value;
    policy.allowed_sources = std::move(allowed_sources_value);
    policy.id = internal::external_time_freshness_policy_identity(policy);
    if (!policy.valid()) {
        return Result<ExternalTimeFreshnessPolicy>::failure(StatusCode::InvalidArgument,
                                                            "external time freshness policy is invalid");
    }
    return policy;
}

bool ExternalTimeFreshnessPolicy::valid() const {
    if (storage_schema != kStorageSchema || !internal::valid_sha256(id) || !valid_text(clock_id) ||
        minimum_sources == 0 || maximum_assertions == 0 || maximum_assertions > 1'000'000 ||
        allowed_sources.empty() || allowed_sources.size() > kMaximumPolicyEntries ||
        minimum_sources > allowed_sources.size() ||
        !sorted_unique(allowed_sources,
                       [](const auto& first, const auto& second) {
                           return std::tie(first.service_id, first.key_id) <
                                  std::tie(second.service_id, second.key_id);
                       }) ||
        !std::all_of(allowed_sources.begin(), allowed_sources.end(),
                     [](const auto& source) { return source.valid(); })) {
        return false;
    }
    if (require_distinct_services) {
        std::set<std::string> services;
        for (const auto& source : allowed_sources)
            services.insert(source.service_id);
        if (minimum_sources > services.size())
            return false;
    }
    return id == internal::external_time_freshness_policy_identity(*this);
}

bool ExternalTimeFreshnessReport::valid() const {
    if (!internal::valid_sha256(id) || !internal::valid_sha256(subject_id) ||
        !internal::valid_sha256(trust_bundle_id) || !internal::valid_sha256(policy_id) ||
        !valid_text(clock_id) || !valid_freshness_status(status) ||
        !sorted_unique(assertion_ids, [](const auto& first, const auto& second) { return first < second; }) ||
        !std::all_of(assertion_ids.begin(), assertion_ids.end(), internal::valid_sha256)) {
        return false;
    }
    if (status == ExternalTimeFreshnessStatus::Inconsistent) {
        if (intersection_lower_ns <= intersection_upper_ns)
            return false;
    } else if (intersection_lower_ns > intersection_upper_ns) {
        return false;
    }
    return id == internal::external_time_freshness_report_identity(*this);
}

Result<ExternalTimeFreshnessReport>
evaluate_external_time_freshness(std::string subject_id, std::span<const ExternalTimeAssertion> assertions,
                                 const ServiceTrustBundle& trust_bundle,
                                 const ExternalTimeFreshnessPolicy& policy, std::uint64_t evaluated_at_ns,
                                 const ProvenanceReplayOptions& options) {
    if (!internal::valid_sha256(subject_id) || !trust_bundle.valid() || !policy.valid() ||
        options.maximum_time_assertions == 0) {
        return Result<ExternalTimeFreshnessReport>::failure(
            StatusCode::InvalidArgument, "external time freshness input or options are invalid");
    }
    const auto maximum = std::min(options.maximum_time_assertions, policy.maximum_assertions);
    if (assertions.size() > maximum) {
        return Result<ExternalTimeFreshnessReport>::failure(
            StatusCode::ResourceLimit, "external time assertion count exceeds configured limit");
    }
    if (options.cancellation.cancelled()) {
        return Result<ExternalTimeFreshnessReport>::failure(
            StatusCode::Cancelled, "external time freshness evaluation was cancelled");
    }

    using SourceKey = std::pair<std::string, std::string>;
    std::map<SourceKey, std::vector<const ExternalTimeAssertion*>> chains;
    std::set<std::string> ids;
    for (const auto& assertion : assertions) {
        if (options.cancellation.cancelled()) {
            return Result<ExternalTimeFreshnessReport>::failure(
                StatusCode::Cancelled, "external time freshness evaluation was cancelled");
        }
        auto verified = verify_external_time_assertion(assertion, trust_bundle);
        if (!verified)
            return verified.error();
        if (!ids.insert(assertion.id).second) {
            return Result<ExternalTimeFreshnessReport>::failure(
                StatusCode::InvalidArgument, "external time assertion set contains a duplicate",
                assertion.id);
        }
        if (assertion.subject_id != subject_id || assertion.clock_id != policy.clock_id ||
            assertion.uncertainty_ns > policy.maximum_uncertainty_ns ||
            !time_source_allowed(policy, assertion)) {
            return Result<ExternalTimeFreshnessReport>::failure(
                StatusCode::IdentityMismatch,
                "external time assertion is outside the explicit freshness policy", assertion.id);
        }
        chains[{assertion.source_service_id, assertion.source_key_id}].push_back(&assertion);
    }

    std::vector<const ExternalTimeAssertion*> latest;
    latest.reserve(chains.size());
    for (auto& [source, chain] : chains) {
        static_cast<void>(source);
        std::sort(chain.begin(), chain.end(), [](const auto* first, const auto* second) {
            return std::tie(first->source_sequence, first->id) <
                   std::tie(second->source_sequence, second->id);
        });
        std::string parent;
        std::uint64_t previous_time = 0;
        for (std::size_t index = 0; index < chain.size(); ++index) {
            const auto& assertion = *chain[index];
            if (assertion.source_sequence != static_cast<std::uint64_t>(index + 1) ||
                assertion.parent_assertion_id != parent ||
                (index > 0 && assertion.asserted_time_ns < previous_time)) {
                return Result<ExternalTimeFreshnessReport>::failure(
                    StatusCode::IdentityMismatch,
                    "external time source chain is not contiguous and monotonic", assertion.id);
            }
            parent = assertion.id;
            previous_time = assertion.asserted_time_ns;
        }
        latest.push_back(chain.back());
    }

    std::set<std::string> distinct_services;
    for (const auto* assertion : latest)
        distinct_services.insert(assertion->source_service_id);
    const std::size_t source_count =
        policy.require_distinct_services ? distinct_services.size() : latest.size();

    ExternalTimeFreshnessReport report;
    report.subject_id = std::move(subject_id);
    report.trust_bundle_id = trust_bundle.id();
    report.policy_id = policy.id;
    report.clock_id = policy.clock_id;
    report.evaluated_at_ns = evaluated_at_ns;
    report.authenticated_source_count = source_count;
    report.assertion_ids.assign(ids.begin(), ids.end());

    if (source_count < policy.minimum_sources) {
        report.status = ExternalTimeFreshnessStatus::Incomplete;
    } else {
        report.intersection_lower_ns = 0;
        report.intersection_upper_ns = std::numeric_limits<std::uint64_t>::max();
        for (const auto* assertion : latest) {
            const auto lower = assertion->asserted_time_ns >= assertion->uncertainty_ns
                                   ? assertion->asserted_time_ns - assertion->uncertainty_ns
                                   : 0;
            const auto upper = saturating_add(assertion->asserted_time_ns, assertion->uncertainty_ns);
            report.intersection_lower_ns = std::max(report.intersection_lower_ns, lower);
            report.intersection_upper_ns = std::min(report.intersection_upper_ns, upper);
        }
        if (report.intersection_lower_ns > report.intersection_upper_ns) {
            report.status = ExternalTimeFreshnessStatus::Inconsistent;
        } else if (report.intersection_lower_ns >
                   saturating_add(evaluated_at_ns, policy.maximum_future_skew_ns)) {
            report.status = ExternalTimeFreshnessStatus::Future;
        } else if (saturating_add(report.intersection_upper_ns, policy.maximum_age_ns) < evaluated_at_ns) {
            report.status = ExternalTimeFreshnessStatus::Stale;
        } else {
            report.status = ExternalTimeFreshnessStatus::Fresh;
        }
    }
    report.id = internal::external_time_freshness_report_identity(report);
    if (!report.valid()) {
        return Result<ExternalTimeFreshnessReport>::failure(
            StatusCode::InternalError, "external time freshness report is not canonical");
    }
    return report;
}

Result<VerifiableProvenanceBundle>
VerifiableProvenanceBundle::create(ServicePublicKey subject_key, HardwareKeyProvenancePolicy hardware_policy,
                                   ExternalTimeFreshnessPolicy freshness_policy,
                                   std::vector<HardwareKeyAttestationStatement> hardware_statements,
                                   std::vector<ExternalTimeAssertion> time_assertions,
                                   const ServiceTrustBundle& trust_bundle) {
    if (!valid_service_public_key(subject_key) || !hardware_policy.valid() || !freshness_policy.valid() ||
        !trust_bundle.valid()) {
        return Result<VerifiableProvenanceBundle>::failure(StatusCode::InvalidArgument,
                                                           "verifiable provenance bundle input is invalid");
    }
    std::sort(hardware_statements.begin(), hardware_statements.end(),
              [](const auto& first, const auto& second) {
                  return std::tie(first.sequence, first.id) < std::tie(second.sequence, second.id);
              });
    std::sort(time_assertions.begin(), time_assertions.end(), [](const auto& first, const auto& second) {
        return std::tie(first.source_service_id, first.source_key_id, first.source_sequence, first.id) <
               std::tie(second.source_service_id, second.source_key_id, second.source_sequence, second.id);
    });
    VerifiableProvenanceBundle bundle;
    bundle.trust_bundle_id_ = trust_bundle.id();
    bundle.trust_bundle_sequence_ = trust_bundle.sequence();
    bundle.subject_key_ = std::move(subject_key);
    bundle.hardware_policy_ = std::move(hardware_policy);
    bundle.freshness_policy_ = std::move(freshness_policy);
    bundle.hardware_statements_ = std::move(hardware_statements);
    bundle.time_assertions_ = std::move(time_assertions);
    bundle.id_ = internal::verifiable_provenance_bundle_identity(bundle);
    if (!bundle.valid()) {
        return Result<VerifiableProvenanceBundle>::failure(
            StatusCode::InvalidArgument, "verifiable provenance bundle is not structurally valid");
    }
    return bundle;
}

bool VerifiableProvenanceBundle::valid() const {
    if (storage_schema_ != kStorageSchema || !internal::valid_sha256(id_) ||
        !internal::valid_sha256(trust_bundle_id_) || trust_bundle_sequence_ == 0 ||
        !valid_service_public_key(subject_key_) || !hardware_policy_.valid() || !freshness_policy_.valid() ||
        hardware_statements_.size() > hardware_policy_.maximum_chain_length ||
        time_assertions_.size() > freshness_policy_.maximum_assertions) {
        return false;
    }
    if (!std::all_of(hardware_statements_.begin(), hardware_statements_.end(),
                     [this](const auto& statement) {
                         return statement.valid() && statement.trust_bundle_id == trust_bundle_id_ &&
                                statement.trust_bundle_sequence == trust_bundle_sequence_;
                     }) ||
        !std::all_of(time_assertions_.begin(), time_assertions_.end(), [this](const auto& assertion) {
            return assertion.valid() && assertion.trust_bundle_id == trust_bundle_id_ &&
                   assertion.trust_bundle_sequence == trust_bundle_sequence_ &&
                   assertion.subject_id == subject_key_.id;
        })) {
        return false;
    }
    return id_ == internal::verifiable_provenance_bundle_identity(*this);
}

bool VerifiableProvenanceAuditReport::valid() const {
    return internal::valid_sha256(id) && internal::valid_sha256(bundle_id) && hardware.valid() &&
           freshness.valid() && id == internal::verifiable_provenance_audit_report_identity(*this);
}

bool VerifiableProvenanceAuditReport::ready() const noexcept {
    return hardware.status == HardwareProvenanceStatus::Satisfied &&
           freshness.status == ExternalTimeFreshnessStatus::Fresh;
}

Result<VerifiableProvenanceAuditReport> replay_verifiable_provenance(const VerifiableProvenanceBundle& bundle,
                                                                     const ServiceTrustBundle& trust_bundle,
                                                                     std::uint64_t evaluated_at_ns,
                                                                     const ProvenanceReplayOptions& options) {
    if (!bundle.valid() || !trust_bundle.valid()) {
        return Result<VerifiableProvenanceAuditReport>::failure(
            StatusCode::InvalidArgument, "verifiable provenance replay input is invalid");
    }
    if (bundle.trust_bundle_id() != trust_bundle.id() ||
        bundle.trust_bundle_sequence() != trust_bundle.sequence()) {
        return Result<VerifiableProvenanceAuditReport>::failure(
            StatusCode::IdentityMismatch, "verifiable provenance trust binding mismatch", bundle.id());
    }
    auto hardware = replay_hardware_key_provenance(bundle.hardware_statements(), bundle.subject_key(),
                                                   trust_bundle, bundle.hardware_policy(), options);
    if (!hardware)
        return hardware.error();
    auto freshness =
        evaluate_external_time_freshness(bundle.subject_key().id, bundle.time_assertions(), trust_bundle,
                                         bundle.freshness_policy(), evaluated_at_ns, options);
    if (!freshness)
        return freshness.error();
    VerifiableProvenanceAuditReport report;
    report.bundle_id = bundle.id();
    report.hardware = std::move(hardware).value();
    report.freshness = std::move(freshness).value();
    report.id = internal::verifiable_provenance_audit_report_identity(report);
    if (!report.valid()) {
        return Result<VerifiableProvenanceAuditReport>::failure(
            StatusCode::InternalError, "verifiable provenance audit report is not canonical");
    }
    return report;
}

std::string hardware_attestation_scope_name(HardwareAttestationScope scope) {
    switch (scope) {
    case HardwareAttestationScope::ArtifactFetch:
        return "ArtifactFetch";
    case HardwareAttestationScope::ArtifactPublish:
        return "ArtifactPublish";
    case HardwareAttestationScope::TrustRotation:
        return "TrustRotation";
    case HardwareAttestationScope::DeploymentReview:
        return "DeploymentReview";
    case HardwareAttestationScope::ExecutionControl:
        return "ExecutionControl";
    case HardwareAttestationScope::RuntimeObservation:
        return "RuntimeObservation";
    case HardwareAttestationScope::TransparencyLog:
        return "TransparencyLog";
    case HardwareAttestationScope::TransparencyWitness:
        return "TransparencyWitness";
    case HardwareAttestationScope::ExternalTime:
        return "ExternalTime";
    }
    return "Unknown";
}

std::string hardware_provenance_status_name(HardwareProvenanceStatus status) {
    switch (status) {
    case HardwareProvenanceStatus::Satisfied:
        return "SATISFIED";
    case HardwareProvenanceStatus::Incomplete:
        return "INCOMPLETE";
    }
    return "UNKNOWN";
}

std::string external_time_freshness_status_name(ExternalTimeFreshnessStatus status) {
    switch (status) {
    case ExternalTimeFreshnessStatus::Fresh:
        return "FRESH";
    case ExternalTimeFreshnessStatus::Incomplete:
        return "INCOMPLETE";
    case ExternalTimeFreshnessStatus::Stale:
        return "STALE";
    case ExternalTimeFreshnessStatus::Future:
        return "FUTURE";
    case ExternalTimeFreshnessStatus::Inconsistent:
        return "INCONSISTENT";
    }
    return "UNKNOWN";
}

} // namespace rbfsafe

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumStorageStringBytes = 4'096;

std::filesystem::path provenance_unique_sibling(const std::filesystem::path& destination,
                                                std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

Json provenance_bundle_payload_json(const VerifiableProvenanceBundle& bundle) {
    Json::Array statements;
    statements.reserve(bundle.hardware_statements().size());
    for (const auto& statement : bundle.hardware_statements())
        statements.emplace_back(hardware_statement_json(statement, true, true));
    Json::Array assertions;
    assertions.reserve(bundle.time_assertions().size());
    for (const auto& assertion : bundle.time_assertions())
        assertions.emplace_back(time_assertion_json(assertion, true, true));
    return Json::Object{
        {"freshness_policy", freshness_policy_json(bundle.freshness_policy(), true)},
        {"hardware_policy", hardware_policy_json(bundle.hardware_policy(), true)},
        {"hardware_statements", std::move(statements)},
        {"id", bundle.id()},
        {"storage_schema", std::to_string(bundle.storage_schema())},
        {"subject_key", service_public_key_json(bundle.subject_key())},
        {"time_assertions", std::move(assertions)},
        {"trust_bundle_id", bundle.trust_bundle_id()},
        {"trust_bundle_sequence", std::to_string(bundle.trust_bundle_sequence())},
    };
}

Json provenance_bundle_document(const VerifiableProvenanceBundle& bundle) {
    const auto payload = provenance_bundle_payload_json(bundle);
    return Json::Object{
        {"checksum", internal::sha256(payload.dump(false))},
        {"format", "rbfsafe-verifiable-provenance-bundle"},
        {"library_version", kVersion},
        {"payload", payload},
        {"schema", 1},
    };
}

Result<std::string> provenance_string_field(const Json& object, std::string_view key,
                                            bool allow_empty = false,
                                            std::size_t maximum = kMaximumStorageStringBytes) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "provenance value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > maximum ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData, "provenance string field is invalid",
                                            std::string(key));
    }
    return value->as_string();
}

Result<std::uint64_t> provenance_decimal_field(const Json& object, std::string_view key) {
    auto text = provenance_string_field(object, key, false, 32);
    if (!text)
        return text.error();
    std::uint64_t value = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "provenance decimal field is invalid",
                                              std::string(key));
    }
    return value;
}

Result<std::size_t> provenance_enum_field(const Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object()) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "provenance value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || value->as_number() < 0.0 ||
        value->as_number() > static_cast<double>(maximum) ||
        value->as_number() != static_cast<double>(static_cast<std::size_t>(value->as_number()))) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "provenance enum field is invalid",
                                            std::string(key));
    }
    return static_cast<std::size_t>(value->as_number());
}

Result<bool> provenance_bool_field(const Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<bool>::failure(StatusCode::CorruptData, "provenance value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_bool()) {
        return Result<bool>::failure(StatusCode::CorruptData, "provenance Boolean field is invalid",
                                     std::string(key));
    }
    return value->as_bool();
}

Result<const Json::Array*> provenance_array_field(const Json& object, std::string_view key,
                                                  std::size_t maximum) {
    if (!object.is_object()) {
        return Result<const Json::Array*>::failure(StatusCode::CorruptData,
                                                   "provenance value is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_array()) {
        return Result<const Json::Array*>::failure(StatusCode::CorruptData,
                                                   "provenance array field is invalid", std::string(key));
    }
    if (value->as_array().size() > maximum) {
        return Result<const Json::Array*>::failure(
            StatusCode::ResourceLimit, "provenance array exceeds configured entry limit", std::string(key));
    }
    return &value->as_array();
}

Result<HardwareAttestationAdapterPin> decode_provenance_adapter(const Json& object) {
    auto id = provenance_string_field(object, "adapter_id");
    auto version = provenance_string_field(object, "adapter_version");
    auto format = provenance_string_field(object, "statement_format");
    if (!id || !version || !format) {
        return Result<HardwareAttestationAdapterPin>::failure(
            StatusCode::CorruptData, "hardware attestation adapter pin is incomplete");
    }
    HardwareAttestationAdapterPin result{std::move(id).value(), std::move(version).value(),
                                         std::move(format).value()};
    if (!result.valid()) {
        return Result<HardwareAttestationAdapterPin>::failure(StatusCode::CorruptData,
                                                              "hardware attestation adapter pin is invalid");
    }
    return result;
}

Result<HardwareAttestationAuthority> decode_provenance_authority(const Json& object) {
    auto service = provenance_string_field(object, "service_id");
    auto key = provenance_string_field(object, "key_id");
    if (!service || !key) {
        return Result<HardwareAttestationAuthority>::failure(StatusCode::CorruptData,
                                                             "hardware attestation authority is incomplete");
    }
    HardwareAttestationAuthority result{std::move(service).value(), std::move(key).value()};
    if (!result.valid()) {
        return Result<HardwareAttestationAuthority>::failure(StatusCode::CorruptData,
                                                             "hardware attestation authority is invalid");
    }
    return result;
}

Result<std::vector<HardwareAttestationScope>>
decode_provenance_scopes(const Json& object, std::string_view key, std::size_t maximum) {
    auto array = provenance_array_field(object, key, maximum);
    if (!array)
        return array.error();
    std::vector<HardwareAttestationScope> scopes;
    scopes.reserve(array.value()->size());
    for (const auto& item : *array.value()) {
        if (!item.is_number() || item.as_number() < 0.0 ||
            item.as_number() > static_cast<double>(HardwareAttestationScope::ExternalTime) ||
            item.as_number() != static_cast<double>(static_cast<std::uint8_t>(item.as_number()))) {
            return Result<std::vector<HardwareAttestationScope>>::failure(
                StatusCode::CorruptData, "hardware attestation scope is invalid");
        }
        scopes.push_back(static_cast<HardwareAttestationScope>(static_cast<std::uint8_t>(item.as_number())));
    }
    return scopes;
}

Result<ServicePublicKey> decode_provenance_service_key(const Json& object) {
    auto id = provenance_string_field(object, "id");
    auto service = provenance_string_field(object, "service_id");
    auto algorithm = provenance_enum_field(
        object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto public_text = provenance_string_field(object, "public_key");
    auto valid_from = provenance_decimal_field(object, "valid_from_sequence");
    auto valid_through = provenance_decimal_field(object, "valid_through_sequence");
    auto state = provenance_enum_field(object, "state", static_cast<std::size_t>(ServiceKeyState::Revoked));
    auto allow_fetch = provenance_bool_field(object, "allow_fetch");
    auto allow_publish = provenance_bool_field(object, "allow_publish");
    auto allow_rotate = provenance_bool_field(object, "allow_rotate");
    if (!id || !service || !algorithm || !public_text || !valid_from || !valid_through || !state ||
        !allow_fetch || !allow_publish || !allow_rotate) {
        return Result<ServicePublicKey>::failure(StatusCode::CorruptData,
                                                 "provenance subject service key is incomplete");
    }
    auto public_key = internal::decode_hex(public_text.value(), kEd25519PublicKeyBytes);
    if (!public_key)
        return public_key.error();
    ServicePublicKey result;
    result.id = std::move(id).value();
    result.service_id = std::move(service).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    std::memcpy(result.public_key.data(), public_key.value().data(), result.public_key.size());
    result.valid_from_sequence = valid_from.value();
    result.valid_through_sequence = valid_through.value();
    result.state = static_cast<ServiceKeyState>(state.value());
    result.allow_fetch = allow_fetch.value();
    result.allow_publish = allow_publish.value();
    result.allow_rotate = allow_rotate.value();
    if (!valid_service_public_key(result)) {
        return Result<ServicePublicKey>::failure(StatusCode::CorruptData,
                                                 "provenance subject service key is invalid", result.id);
    }
    return result;
}

Result<HardwareKeyAttestationStatement> decode_provenance_statement(const Json& object,
                                                                    std::size_t maximum_scopes) {
    auto schema = provenance_decimal_field(object, "storage_schema");
    auto sequence = provenance_decimal_field(object, "sequence");
    auto id = provenance_string_field(object, "id");
    auto parent = provenance_string_field(object, "parent_statement_id", true);
    auto subject_service = provenance_string_field(object, "subject_service_id");
    auto subject_key = provenance_string_field(object, "subject_key_id");
    auto subject_public = provenance_string_field(object, "subject_public_key");
    const auto* adapter_value = object.find("adapter");
    auto adapter = adapter_value == nullptr
                       ? Result<HardwareAttestationAdapterPin>::failure(
                             StatusCode::CorruptData, "hardware attestation adapter is missing")
                       : decode_provenance_adapter(*adapter_value);
    auto vendor = provenance_string_field(object, "vendor_id");
    auto product = provenance_string_field(object, "product_id");
    auto evidence = provenance_string_field(object, "evidence_digest");
    auto nonce = provenance_string_field(object, "nonce_digest");
    auto scopes = decode_provenance_scopes(object, "scopes", maximum_scopes);
    auto trust = provenance_string_field(object, "trust_bundle_id");
    auto trust_sequence = provenance_decimal_field(object, "trust_bundle_sequence");
    auto attester_service = provenance_string_field(object, "attester_service_id");
    auto attester_key = provenance_string_field(object, "attester_key_id");
    auto algorithm = provenance_enum_field(
        object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = provenance_string_field(object, "authentication_tag");
    if (!schema || !sequence || !id || !parent || !subject_service || !subject_key || !subject_public ||
        !adapter || !vendor || !product || !evidence || !nonce || !scopes || !trust || !trust_sequence ||
        !attester_service || !attester_key || !algorithm || !tag) {
        return Result<HardwareKeyAttestationStatement>::failure(
            StatusCode::CorruptData, "hardware-key attestation statement is incomplete");
    }
    auto decoded_public = internal::decode_hex(subject_public.value(), kEd25519PublicKeyBytes);
    if (!decoded_public)
        return decoded_public.error();
    HardwareKeyAttestationStatement result;
    result.storage_schema = static_cast<std::uint32_t>(schema.value());
    result.sequence = sequence.value();
    result.id = std::move(id).value();
    result.parent_statement_id = std::move(parent).value();
    result.subject_service_id = std::move(subject_service).value();
    result.subject_key_id = std::move(subject_key).value();
    std::memcpy(result.subject_public_key.data(), decoded_public.value().data(),
                result.subject_public_key.size());
    result.adapter = std::move(adapter).value();
    result.vendor_id = std::move(vendor).value();
    result.product_id = std::move(product).value();
    result.evidence_digest = std::move(evidence).value();
    result.nonce_digest = std::move(nonce).value();
    result.scopes = std::move(scopes).value();
    result.trust_bundle_id = std::move(trust).value();
    result.trust_bundle_sequence = trust_sequence.value();
    result.attester_service_id = std::move(attester_service).value();
    result.attester_key_id = std::move(attester_key).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!result.valid()) {
        return Result<HardwareKeyAttestationStatement>::failure(
            StatusCode::CorruptData, "hardware-key attestation statement identity is invalid", result.id);
    }
    return result;
}

Result<HardwareKeyProvenancePolicy> decode_provenance_hardware_policy(const Json& object,
                                                                      std::size_t maximum_entries) {
    auto schema = provenance_decimal_field(object, "storage_schema");
    auto id = provenance_string_field(object, "id");
    auto minimum = provenance_decimal_field(object, "minimum_statements");
    auto distinct = provenance_bool_field(object, "require_distinct_attesters");
    auto maximum = provenance_decimal_field(object, "maximum_chain_length");
    auto scopes = decode_provenance_scopes(object, "required_scopes", maximum_entries);
    auto adapters_json = provenance_array_field(object, "allowed_adapters", maximum_entries);
    auto authorities_json = provenance_array_field(object, "allowed_authorities", maximum_entries);
    auto vendors_json = provenance_array_field(object, "allowed_vendor_ids", maximum_entries);
    if (!scopes)
        return scopes.error();
    if (!adapters_json)
        return adapters_json.error();
    if (!authorities_json)
        return authorities_json.error();
    if (!vendors_json)
        return vendors_json.error();
    if (!schema || !id || !minimum || !distinct || !maximum ||
        minimum.value() > std::numeric_limits<std::uint32_t>::max() ||
        maximum.value() > std::numeric_limits<std::size_t>::max()) {
        return Result<HardwareKeyProvenancePolicy>::failure(StatusCode::CorruptData,
                                                            "hardware-key provenance policy is incomplete");
    }
    std::vector<HardwareAttestationAdapterPin> adapters;
    adapters.reserve(adapters_json.value()->size());
    for (const auto& item : *adapters_json.value()) {
        auto decoded = decode_provenance_adapter(item);
        if (!decoded)
            return decoded.error();
        adapters.push_back(std::move(decoded).value());
    }
    std::vector<HardwareAttestationAuthority> authorities;
    authorities.reserve(authorities_json.value()->size());
    for (const auto& item : *authorities_json.value()) {
        auto decoded = decode_provenance_authority(item);
        if (!decoded)
            return decoded.error();
        authorities.push_back(std::move(decoded).value());
    }
    std::vector<std::string> vendors;
    vendors.reserve(vendors_json.value()->size());
    for (const auto& item : *vendors_json.value()) {
        if (!item.is_string() || item.as_string().empty() ||
            item.as_string().size() > kMaximumStorageStringBytes) {
            return Result<HardwareKeyProvenancePolicy>::failure(StatusCode::CorruptData,
                                                                "hardware-key provenance vendor is invalid");
        }
        vendors.push_back(item.as_string());
    }
    auto policy = HardwareKeyProvenancePolicy::create(
        static_cast<std::uint32_t>(minimum.value()), distinct.value(),
        static_cast<std::size_t>(maximum.value()), std::move(scopes).value(), std::move(adapters),
        std::move(authorities), std::move(vendors));
    if (!policy)
        return Result<HardwareKeyProvenancePolicy>::failure(StatusCode::CorruptData,
                                                            "hardware-key provenance policy is invalid");
    if (schema.value() != policy.value().storage_schema || id.value() != policy.value().id) {
        return Result<HardwareKeyProvenancePolicy>::failure(
            StatusCode::CorruptData, "hardware-key provenance policy identity mismatch", id.value());
    }
    return policy;
}

Result<ExternalTimeSource> decode_provenance_time_source(const Json& object) {
    auto service = provenance_string_field(object, "service_id");
    auto key = provenance_string_field(object, "key_id");
    if (!service || !key) {
        return Result<ExternalTimeSource>::failure(StatusCode::CorruptData,
                                                   "external time source is incomplete");
    }
    ExternalTimeSource result{std::move(service).value(), std::move(key).value()};
    if (!result.valid()) {
        return Result<ExternalTimeSource>::failure(StatusCode::CorruptData,
                                                   "external time source is invalid");
    }
    return result;
}

Result<ExternalTimeFreshnessPolicy> decode_provenance_freshness_policy(const Json& object,
                                                                       std::size_t maximum_entries) {
    auto schema = provenance_decimal_field(object, "storage_schema");
    auto id = provenance_string_field(object, "id");
    auto clock = provenance_string_field(object, "clock_id");
    auto maximum_age = provenance_decimal_field(object, "maximum_age_ns");
    auto future = provenance_decimal_field(object, "maximum_future_skew_ns");
    auto uncertainty = provenance_decimal_field(object, "maximum_uncertainty_ns");
    auto minimum = provenance_decimal_field(object, "minimum_sources");
    auto distinct = provenance_bool_field(object, "require_distinct_services");
    auto maximum_assertions = provenance_decimal_field(object, "maximum_assertions");
    auto sources_json = provenance_array_field(object, "allowed_sources", maximum_entries);
    if (!sources_json)
        return sources_json.error();
    if (!schema || !id || !clock || !maximum_age || !future || !uncertainty || !minimum || !distinct ||
        !maximum_assertions || minimum.value() > std::numeric_limits<std::uint32_t>::max() ||
        maximum_assertions.value() > std::numeric_limits<std::size_t>::max()) {
        return Result<ExternalTimeFreshnessPolicy>::failure(StatusCode::CorruptData,
                                                            "external time freshness policy is incomplete");
    }
    std::vector<ExternalTimeSource> sources;
    sources.reserve(sources_json.value()->size());
    for (const auto& item : *sources_json.value()) {
        auto decoded = decode_provenance_time_source(item);
        if (!decoded)
            return decoded.error();
        sources.push_back(std::move(decoded).value());
    }
    auto policy = ExternalTimeFreshnessPolicy::create(
        std::move(clock).value(), maximum_age.value(), future.value(), uncertainty.value(),
        static_cast<std::uint32_t>(minimum.value()), distinct.value(),
        static_cast<std::size_t>(maximum_assertions.value()), std::move(sources));
    if (!policy) {
        return Result<ExternalTimeFreshnessPolicy>::failure(StatusCode::CorruptData,
                                                            "external time freshness policy is invalid");
    }
    if (schema.value() != policy.value().storage_schema || id.value() != policy.value().id) {
        return Result<ExternalTimeFreshnessPolicy>::failure(
            StatusCode::CorruptData, "external time freshness policy identity mismatch", id.value());
    }
    return policy;
}

Result<ExternalTimeAssertion> decode_provenance_time_assertion(const Json& object) {
    auto schema = provenance_decimal_field(object, "storage_schema");
    auto id = provenance_string_field(object, "id");
    auto sequence = provenance_decimal_field(object, "source_sequence");
    auto parent = provenance_string_field(object, "parent_assertion_id", true);
    auto subject = provenance_string_field(object, "subject_id");
    auto clock = provenance_string_field(object, "clock_id");
    auto asserted = provenance_decimal_field(object, "asserted_time_ns");
    auto uncertainty = provenance_decimal_field(object, "uncertainty_ns");
    auto trust = provenance_string_field(object, "trust_bundle_id");
    auto trust_sequence = provenance_decimal_field(object, "trust_bundle_sequence");
    auto service = provenance_string_field(object, "source_service_id");
    auto key = provenance_string_field(object, "source_key_id");
    auto algorithm = provenance_enum_field(
        object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = provenance_string_field(object, "authentication_tag");
    if (!schema || !id || !sequence || !parent || !subject || !clock || !asserted || !uncertainty || !trust ||
        !trust_sequence || !service || !key || !algorithm || !tag) {
        return Result<ExternalTimeAssertion>::failure(StatusCode::CorruptData,
                                                      "external time assertion is incomplete");
    }
    ExternalTimeAssertion result;
    result.storage_schema = static_cast<std::uint32_t>(schema.value());
    result.id = std::move(id).value();
    result.source_sequence = sequence.value();
    result.parent_assertion_id = std::move(parent).value();
    result.subject_id = std::move(subject).value();
    result.clock_id = std::move(clock).value();
    result.asserted_time_ns = asserted.value();
    result.uncertainty_ns = uncertainty.value();
    result.trust_bundle_id = std::move(trust).value();
    result.trust_bundle_sequence = trust_sequence.value();
    result.source_service_id = std::move(service).value();
    result.source_key_id = std::move(key).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!result.valid()) {
        return Result<ExternalTimeAssertion>::failure(
            StatusCode::CorruptData, "external time assertion identity is invalid", result.id);
    }
    return result;
}

Result<Json> read_bounded_provenance_json(const std::filesystem::path& path,
                                          const VerifiableProvenanceBundleLoadOptions& options) {
    if (options.cancellation.cancelled()) {
        return Result<Json>::failure(StatusCode::Cancelled, "provenance bundle load was cancelled");
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<Json>::failure(StatusCode::CorruptData,
                                     "provenance bundle is missing, indirect, or not a regular file",
                                     path.string());
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<Json>::failure(StatusCode::IoError, "failed to inspect provenance bundle size",
                                     path.string());
    }
    if (bytes > options.maximum_payload_bytes ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return Result<Json>::failure(StatusCode::ResourceLimit,
                                     "provenance bundle exceeds configured byte limit", path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<Json>::failure(StatusCode::IoError, "failed to open provenance bundle", path.string());
    }
    std::string text(static_cast<std::size_t>(bytes), '\0');
    if (!text.empty()) {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (input.gcount() != static_cast<std::streamsize>(text.size())) {
            return Result<Json>::failure(StatusCode::CorruptData, "provenance bundle changed while reading",
                                         path.string());
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        return Result<Json>::failure(StatusCode::CorruptData, "provenance bundle changed while reading",
                                     path.string());
    }
    return Json::parse(text);
}

Result<void> publish_provenance_file(const std::filesystem::path& temporary,
                                     const std::filesystem::path& destination, bool destination_exists) {
    std::error_code error;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = provenance_unique_sibling(destination, ".backup-");
        std::filesystem::rename(destination, backup, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError, "failed to stage existing provenance bundle",
                                         destination.string());
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish provenance bundle",
                                     destination.string());
    }
    if (destination_exists) {
        std::filesystem::remove(backup, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError, "failed to remove staged provenance bundle",
                                         backup.string());
        }
    }
    return Result<void>::success();
}

} // namespace

Result<void> save_verifiable_provenance_bundle(const VerifiableProvenanceBundle& bundle,
                                               const std::filesystem::path& path,
                                               const SaveOptions& options) {
    if (!bundle.valid() || path.empty()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "verifiable provenance bundle or destination is invalid");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError, "failed to inspect provenance bundle destination",
                                     path.string());
    }
    if (destination_exists) {
        const auto status = std::filesystem::symlink_status(path, error);
        if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
            return Result<void>::failure(StatusCode::IoError,
                                         "provenance bundle destination is indirect or not a regular file",
                                         path.string());
        }
        if (!options.overwrite) {
            return Result<void>::failure(StatusCode::IoError, "provenance bundle destination already exists",
                                         path.string());
        }
    }
    const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    if (!std::filesystem::exists(parent, error) || !std::filesystem::is_directory(parent, error) || error) {
        return Result<void>::failure(StatusCode::IoError, "provenance bundle parent directory is unavailable",
                                     parent.string());
    }
    const auto temporary = provenance_unique_sibling(path, ".tmp-");
    const auto document = provenance_bundle_document(bundle).dump(true) + "\n";
    auto written = internal::write_text_file(temporary, document);
    if (!written)
        return written.error();

    VerifiableProvenanceBundleLoadOptions verify_options;
    verify_options.maximum_statements = std::max<std::size_t>(1, bundle.hardware_statements().size());
    verify_options.maximum_time_assertions = std::max<std::size_t>(1, bundle.time_assertions().size());
    verify_options.maximum_policy_entries =
        std::max({std::size_t{1}, bundle.hardware_policy().required_scopes.size(),
                  bundle.hardware_policy().allowed_adapters.size(),
                  bundle.hardware_policy().allowed_authorities.size(),
                  bundle.hardware_policy().allowed_vendor_ids.size(),
                  bundle.freshness_policy().allowed_sources.size()});
    verify_options.maximum_payload_bytes = std::max<std::uintmax_t>(document.size(), 1);
    auto verified = load_verifiable_provenance_bundle(temporary, verify_options);
    if (!verified || verified.value().id() != bundle.id()) {
        std::filesystem::remove(temporary, error);
        if (!verified)
            return verified.error();
        return Result<void>::failure(StatusCode::CorruptData, "staged provenance bundle identity changed");
    }
    auto published = publish_provenance_file(temporary, path, destination_exists);
    if (!published)
        std::filesystem::remove(temporary, error);
    return published;
}

Result<VerifiableProvenanceBundle>
load_verifiable_provenance_bundle(const std::filesystem::path& path,
                                  const VerifiableProvenanceBundleLoadOptions& options) {
    if (path.empty() || options.maximum_statements == 0 || options.maximum_time_assertions == 0 ||
        options.maximum_policy_entries == 0 || options.maximum_payload_bytes == 0) {
        return Result<VerifiableProvenanceBundle>::failure(
            StatusCode::InvalidArgument, "provenance bundle path or load options are invalid");
    }
    auto document = read_bounded_provenance_json(path, options);
    if (!document)
        return document.error();
    auto format = provenance_string_field(document.value(), "format");
    auto schema =
        provenance_enum_field(document.value(), "schema", std::numeric_limits<std::uint32_t>::max());
    auto checksum = provenance_string_field(document.value(), "checksum");
    const auto* payload = document.value().find("payload");
    if (!format || !schema || !checksum || payload == nullptr || !payload->is_object()) {
        return Result<VerifiableProvenanceBundle>::failure(StatusCode::CorruptData,
                                                           "provenance bundle document is incomplete");
    }
    if (format.value() != "rbfsafe-verifiable-provenance-bundle") {
        return Result<VerifiableProvenanceBundle>::failure(
            StatusCode::IncompatibleFormat, "provenance bundle format is not recognized", format.value());
    }
    if (schema.value() != 1) {
        return Result<VerifiableProvenanceBundle>::failure(StatusCode::IncompatibleFormat,
                                                           "provenance bundle schema is not supported");
    }
    if (!internal::valid_sha256(checksum.value()) ||
        checksum.value() != internal::sha256(payload->dump(false))) {
        return Result<VerifiableProvenanceBundle>::failure(
            StatusCode::CorruptData, "provenance bundle checksum mismatch", path.string());
    }
    auto storage = provenance_decimal_field(*payload, "storage_schema");
    auto stored_id = provenance_string_field(*payload, "id");
    auto trust = provenance_string_field(*payload, "trust_bundle_id");
    auto trust_sequence = provenance_decimal_field(*payload, "trust_bundle_sequence");
    const auto* subject_json = payload->find("subject_key");
    const auto* hardware_policy_json_value = payload->find("hardware_policy");
    const auto* freshness_policy_json_value = payload->find("freshness_policy");
    auto statements_json =
        provenance_array_field(*payload, "hardware_statements", options.maximum_statements);
    auto assertions_json =
        provenance_array_field(*payload, "time_assertions", options.maximum_time_assertions);
    if (!statements_json)
        return statements_json.error();
    if (!assertions_json)
        return assertions_json.error();
    if (!storage || !stored_id || !trust || !trust_sequence || subject_json == nullptr ||
        hardware_policy_json_value == nullptr || freshness_policy_json_value == nullptr) {
        return Result<VerifiableProvenanceBundle>::failure(StatusCode::CorruptData,
                                                           "provenance bundle payload is incomplete");
    }
    auto subject = decode_provenance_service_key(*subject_json);
    auto hardware_policy =
        decode_provenance_hardware_policy(*hardware_policy_json_value, options.maximum_policy_entries);
    auto freshness_policy =
        decode_provenance_freshness_policy(*freshness_policy_json_value, options.maximum_policy_entries);
    if (!subject)
        return subject.error();
    if (!hardware_policy)
        return hardware_policy.error();
    if (!freshness_policy)
        return freshness_policy.error();
    std::vector<HardwareKeyAttestationStatement> statements;
    statements.reserve(statements_json.value()->size());
    for (const auto& item : *statements_json.value()) {
        if (options.cancellation.cancelled()) {
            return Result<VerifiableProvenanceBundle>::failure(StatusCode::Cancelled,
                                                               "provenance bundle load was cancelled");
        }
        auto decoded = decode_provenance_statement(item, options.maximum_policy_entries);
        if (!decoded)
            return decoded.error();
        statements.push_back(std::move(decoded).value());
    }
    std::vector<ExternalTimeAssertion> assertions;
    assertions.reserve(assertions_json.value()->size());
    for (const auto& item : *assertions_json.value()) {
        if (options.cancellation.cancelled()) {
            return Result<VerifiableProvenanceBundle>::failure(StatusCode::Cancelled,
                                                               "provenance bundle load was cancelled");
        }
        auto decoded = decode_provenance_time_assertion(item);
        if (!decoded)
            return decoded.error();
        assertions.push_back(std::move(decoded).value());
    }
    if (storage.value() != 1) {
        return Result<VerifiableProvenanceBundle>::failure(
            StatusCode::IncompatibleFormat, "provenance payload storage schema is unsupported");
    }

    VerifiableProvenanceBundle bundle;
    bundle.storage_schema_ = static_cast<std::uint32_t>(storage.value());
    bundle.id_ = std::move(stored_id).value();
    bundle.trust_bundle_id_ = std::move(trust).value();
    bundle.trust_bundle_sequence_ = trust_sequence.value();
    bundle.subject_key_ = std::move(subject).value();
    bundle.hardware_policy_ = std::move(hardware_policy).value();
    bundle.freshness_policy_ = std::move(freshness_policy).value();
    bundle.hardware_statements_ = std::move(statements);
    bundle.time_assertions_ = std::move(assertions);
    if (!bundle.valid()) {
        return Result<VerifiableProvenanceBundle>::failure(
            StatusCode::CorruptData, "provenance bundle identity or structure is invalid", bundle.id_);
    }
    return bundle;
}

Result<void> VerifiableProvenanceBundle::save(const std::filesystem::path& path,
                                              const SaveOptions& options) const {
    return save_verifiable_provenance_bundle(*this, path, options);
}

Result<VerifiableProvenanceBundle>
VerifiableProvenanceBundle::load(const std::filesystem::path& path,
                                 const VerifiableProvenanceBundleLoadOptions& options) {
    return load_verifiable_provenance_bundle(path, options);
}

} // namespace rbfsafe
