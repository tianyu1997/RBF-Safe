#include "binding_support.h"

#include <rbfsafe/provenance.h>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rbfsafe::python_binding {
namespace {

std::span<const std::byte> provenance_bytes_view(const std::string& value) {
    return std::as_bytes(std::span(value.data(), value.size()));
}

class ProvenanceSensitiveBytes {
  public:
    explicit ProvenanceSensitiveBytes(const py::bytes& value) : value_(static_cast<std::string>(value)) {}
    ProvenanceSensitiveBytes(const ProvenanceSensitiveBytes&) = delete;
    ProvenanceSensitiveBytes& operator=(const ProvenanceSensitiveBytes&) = delete;
    ~ProvenanceSensitiveBytes() {
        volatile char* current = value_.data();
        for (std::size_t index = 0; index < value_.size(); ++index)
            current[index] = 0;
    }

    std::span<const std::byte> view() const { return provenance_bytes_view(value_); }

  private:
    std::string value_;
};

py::bytes provenance_public_key_bytes(const std::array<std::byte, kEd25519PublicKeyBytes>& value) {
    return py::bytes(reinterpret_cast<const char*>(value.data()), value.size());
}

void set_provenance_public_key(HardwareKeyAttestationInput& input, const py::bytes& value) {
    const auto bytes = static_cast<std::string>(value);
    if (bytes.size() != input.subject_public_key.size())
        throw py::value_error("subject_public_key must contain exactly 32 bytes");
    std::memcpy(input.subject_public_key.data(), bytes.data(), bytes.size());
}

} // namespace

void bind_provenance(py::module_& module) {
    py::enum_<HardwareAttestationScope>(module, "HardwareAttestationScope")
        .value("ARTIFACT_FETCH", HardwareAttestationScope::ArtifactFetch)
        .value("ARTIFACT_PUBLISH", HardwareAttestationScope::ArtifactPublish)
        .value("TRUST_ROTATION", HardwareAttestationScope::TrustRotation)
        .value("DEPLOYMENT_REVIEW", HardwareAttestationScope::DeploymentReview)
        .value("EXECUTION_CONTROL", HardwareAttestationScope::ExecutionControl)
        .value("RUNTIME_OBSERVATION", HardwareAttestationScope::RuntimeObservation)
        .value("TRANSPARENCY_LOG", HardwareAttestationScope::TransparencyLog)
        .value("TRANSPARENCY_WITNESS", HardwareAttestationScope::TransparencyWitness)
        .value("EXTERNAL_TIME", HardwareAttestationScope::ExternalTime);

    py::class_<HardwareAttestationAdapterPin>(module, "HardwareAttestationAdapterPin")
        .def(py::init<std::string, std::string, std::string>(), py::arg("adapter_id") = "",
             py::arg("adapter_version") = "", py::arg("statement_format") = "")
        .def_readwrite("adapter_id", &HardwareAttestationAdapterPin::adapter_id)
        .def_readwrite("adapter_version", &HardwareAttestationAdapterPin::adapter_version)
        .def_readwrite("statement_format", &HardwareAttestationAdapterPin::statement_format)
        .def("valid", &HardwareAttestationAdapterPin::valid);

    py::class_<HardwareAttestationAuthority>(module, "HardwareAttestationAuthority")
        .def(py::init<std::string, std::string>(), py::arg("service_id") = "", py::arg("key_id") = "")
        .def_readwrite("service_id", &HardwareAttestationAuthority::service_id)
        .def_readwrite("key_id", &HardwareAttestationAuthority::key_id)
        .def("valid", &HardwareAttestationAuthority::valid);

    py::class_<HardwareKeyAttestationInput>(module, "HardwareKeyAttestationInput")
        .def(py::init<>())
        .def_readwrite("sequence", &HardwareKeyAttestationInput::sequence)
        .def_readwrite("parent_statement_id", &HardwareKeyAttestationInput::parent_statement_id)
        .def_readwrite("subject_service_id", &HardwareKeyAttestationInput::subject_service_id)
        .def_readwrite("subject_key_id", &HardwareKeyAttestationInput::subject_key_id)
        .def_property(
            "subject_public_key",
            [](const HardwareKeyAttestationInput& input) {
                return provenance_public_key_bytes(input.subject_public_key);
            },
            &set_provenance_public_key)
        .def_readwrite("adapter", &HardwareKeyAttestationInput::adapter)
        .def_readwrite("vendor_id", &HardwareKeyAttestationInput::vendor_id)
        .def_readwrite("product_id", &HardwareKeyAttestationInput::product_id)
        .def_readwrite("evidence_digest", &HardwareKeyAttestationInput::evidence_digest)
        .def_readwrite("nonce_digest", &HardwareKeyAttestationInput::nonce_digest)
        .def_readwrite("scopes", &HardwareKeyAttestationInput::scopes);

    py::class_<HardwareKeyAttestationStatement>(module, "HardwareKeyAttestationStatement")
        .def_readonly("storage_schema", &HardwareKeyAttestationStatement::storage_schema)
        .def_readonly("sequence", &HardwareKeyAttestationStatement::sequence)
        .def_readonly("id", &HardwareKeyAttestationStatement::id)
        .def_readonly("parent_statement_id", &HardwareKeyAttestationStatement::parent_statement_id)
        .def_readonly("subject_service_id", &HardwareKeyAttestationStatement::subject_service_id)
        .def_readonly("subject_key_id", &HardwareKeyAttestationStatement::subject_key_id)
        .def_property_readonly("subject_public_key",
                               [](const HardwareKeyAttestationStatement& statement) {
                                   return provenance_public_key_bytes(statement.subject_public_key);
                               })
        .def_readonly("adapter", &HardwareKeyAttestationStatement::adapter)
        .def_readonly("vendor_id", &HardwareKeyAttestationStatement::vendor_id)
        .def_readonly("product_id", &HardwareKeyAttestationStatement::product_id)
        .def_readonly("evidence_digest", &HardwareKeyAttestationStatement::evidence_digest)
        .def_readonly("nonce_digest", &HardwareKeyAttestationStatement::nonce_digest)
        .def_readonly("scopes", &HardwareKeyAttestationStatement::scopes)
        .def_readonly("trust_bundle_id", &HardwareKeyAttestationStatement::trust_bundle_id)
        .def_readonly("trust_bundle_sequence", &HardwareKeyAttestationStatement::trust_bundle_sequence)
        .def_readonly("attester_service_id", &HardwareKeyAttestationStatement::attester_service_id)
        .def_readonly("attester_key_id", &HardwareKeyAttestationStatement::attester_key_id)
        .def_readonly("algorithm", &HardwareKeyAttestationStatement::algorithm)
        .def_readonly("authentication_tag", &HardwareKeyAttestationStatement::authentication_tag)
        .def("valid", &HardwareKeyAttestationStatement::valid)
        .def_property_readonly("evidence", &HardwareKeyAttestationStatement::evidence)
        .def_property_readonly("authorizes_execution",
                               &HardwareKeyAttestationStatement::authorizes_execution);

    py::class_<HardwareKeyProvenancePolicy>(module, "HardwareKeyProvenancePolicy")
        .def_static(
            "create",
            [](std::uint32_t minimum_statements, bool require_distinct_attesters,
               std::size_t maximum_chain_length, std::vector<HardwareAttestationScope> required_scopes,
               std::vector<HardwareAttestationAdapterPin> allowed_adapters,
               std::vector<HardwareAttestationAuthority> allowed_authorities,
               std::vector<std::string> allowed_vendor_ids) {
                return unwrap(HardwareKeyProvenancePolicy::create(
                    minimum_statements, require_distinct_attesters, maximum_chain_length,
                    std::move(required_scopes), std::move(allowed_adapters), std::move(allowed_authorities),
                    std::move(allowed_vendor_ids)));
            },
            py::arg("minimum_statements"), py::arg("require_distinct_attesters"),
            py::arg("maximum_chain_length"), py::arg("required_scopes"), py::arg("allowed_adapters"),
            py::arg("allowed_authorities"), py::arg("allowed_vendor_ids"))
        .def_readonly("storage_schema", &HardwareKeyProvenancePolicy::storage_schema)
        .def_readonly("id", &HardwareKeyProvenancePolicy::id)
        .def_readonly("minimum_statements", &HardwareKeyProvenancePolicy::minimum_statements)
        .def_readonly("require_distinct_attesters", &HardwareKeyProvenancePolicy::require_distinct_attesters)
        .def_readonly("maximum_chain_length", &HardwareKeyProvenancePolicy::maximum_chain_length)
        .def_readonly("required_scopes", &HardwareKeyProvenancePolicy::required_scopes)
        .def_readonly("allowed_adapters", &HardwareKeyProvenancePolicy::allowed_adapters)
        .def_readonly("allowed_authorities", &HardwareKeyProvenancePolicy::allowed_authorities)
        .def_readonly("allowed_vendor_ids", &HardwareKeyProvenancePolicy::allowed_vendor_ids)
        .def("valid", &HardwareKeyProvenancePolicy::valid);

    py::enum_<HardwareProvenanceStatus>(module, "HardwareProvenanceStatus")
        .value("SATISFIED", HardwareProvenanceStatus::Satisfied)
        .value("INCOMPLETE", HardwareProvenanceStatus::Incomplete);

    py::class_<HardwareKeyProvenanceReport>(module, "HardwareKeyProvenanceReport")
        .def_readonly("id", &HardwareKeyProvenanceReport::id)
        .def_readonly("subject_service_id", &HardwareKeyProvenanceReport::subject_service_id)
        .def_readonly("subject_key_id", &HardwareKeyProvenanceReport::subject_key_id)
        .def_readonly("trust_bundle_id", &HardwareKeyProvenanceReport::trust_bundle_id)
        .def_readonly("policy_id", &HardwareKeyProvenanceReport::policy_id)
        .def_readonly("status", &HardwareKeyProvenanceReport::status)
        .def_readonly("authenticated_statement_count",
                      &HardwareKeyProvenanceReport::authenticated_statement_count)
        .def_readonly("distinct_attester_count", &HardwareKeyProvenanceReport::distinct_attester_count)
        .def_readonly("head_statement_id", &HardwareKeyProvenanceReport::head_statement_id)
        .def_readonly("statement_ids", &HardwareKeyProvenanceReport::statement_ids)
        .def("valid", &HardwareKeyProvenanceReport::valid)
        .def_property_readonly("evidence", &HardwareKeyProvenanceReport::evidence)
        .def_property_readonly("authorizes_execution", &HardwareKeyProvenanceReport::authorizes_execution);

    py::class_<ProvenanceReplayOptions>(module, "ProvenanceReplayOptions")
        .def(py::init<>())
        .def_readwrite("maximum_statements", &ProvenanceReplayOptions::maximum_statements)
        .def_readwrite("maximum_time_assertions", &ProvenanceReplayOptions::maximum_time_assertions)
        .def_readwrite("cancellation", &ProvenanceReplayOptions::cancellation);

    py::class_<ExternalTimeAssertionInput>(module, "ExternalTimeAssertionInput")
        .def(py::init<>())
        .def_readwrite("source_sequence", &ExternalTimeAssertionInput::source_sequence)
        .def_readwrite("parent_assertion_id", &ExternalTimeAssertionInput::parent_assertion_id)
        .def_readwrite("subject_id", &ExternalTimeAssertionInput::subject_id)
        .def_readwrite("clock_id", &ExternalTimeAssertionInput::clock_id)
        .def_readwrite("asserted_time_ns", &ExternalTimeAssertionInput::asserted_time_ns)
        .def_readwrite("uncertainty_ns", &ExternalTimeAssertionInput::uncertainty_ns);

    py::class_<ExternalTimeAssertion>(module, "ExternalTimeAssertion")
        .def_readonly("storage_schema", &ExternalTimeAssertion::storage_schema)
        .def_readonly("id", &ExternalTimeAssertion::id)
        .def_readonly("source_sequence", &ExternalTimeAssertion::source_sequence)
        .def_readonly("parent_assertion_id", &ExternalTimeAssertion::parent_assertion_id)
        .def_readonly("subject_id", &ExternalTimeAssertion::subject_id)
        .def_readonly("clock_id", &ExternalTimeAssertion::clock_id)
        .def_readonly("asserted_time_ns", &ExternalTimeAssertion::asserted_time_ns)
        .def_readonly("uncertainty_ns", &ExternalTimeAssertion::uncertainty_ns)
        .def_readonly("trust_bundle_id", &ExternalTimeAssertion::trust_bundle_id)
        .def_readonly("trust_bundle_sequence", &ExternalTimeAssertion::trust_bundle_sequence)
        .def_readonly("source_service_id", &ExternalTimeAssertion::source_service_id)
        .def_readonly("source_key_id", &ExternalTimeAssertion::source_key_id)
        .def_readonly("algorithm", &ExternalTimeAssertion::algorithm)
        .def_readonly("authentication_tag", &ExternalTimeAssertion::authentication_tag)
        .def("valid", &ExternalTimeAssertion::valid)
        .def_property_readonly("evidence", &ExternalTimeAssertion::evidence)
        .def_property_readonly("authorizes_execution", &ExternalTimeAssertion::authorizes_execution);

    py::class_<ExternalTimeSource>(module, "ExternalTimeSource")
        .def(py::init<std::string, std::string>(), py::arg("service_id") = "", py::arg("key_id") = "")
        .def_readwrite("service_id", &ExternalTimeSource::service_id)
        .def_readwrite("key_id", &ExternalTimeSource::key_id)
        .def("valid", &ExternalTimeSource::valid);

    py::class_<ExternalTimeFreshnessPolicy>(module, "ExternalTimeFreshnessPolicy")
        .def_static(
            "create",
            [](std::string clock_id, std::uint64_t maximum_age_ns, std::uint64_t maximum_future_skew_ns,
               std::uint64_t maximum_uncertainty_ns, std::uint32_t minimum_sources,
               bool require_distinct_services, std::size_t maximum_assertions,
               std::vector<ExternalTimeSource> allowed_sources) {
                return unwrap(ExternalTimeFreshnessPolicy::create(
                    std::move(clock_id), maximum_age_ns, maximum_future_skew_ns, maximum_uncertainty_ns,
                    minimum_sources, require_distinct_services, maximum_assertions,
                    std::move(allowed_sources)));
            },
            py::arg("clock_id"), py::arg("maximum_age_ns"), py::arg("maximum_future_skew_ns"),
            py::arg("maximum_uncertainty_ns"), py::arg("minimum_sources"),
            py::arg("require_distinct_services"), py::arg("maximum_assertions"), py::arg("allowed_sources"))
        .def_readonly("storage_schema", &ExternalTimeFreshnessPolicy::storage_schema)
        .def_readonly("id", &ExternalTimeFreshnessPolicy::id)
        .def_readonly("clock_id", &ExternalTimeFreshnessPolicy::clock_id)
        .def_readonly("maximum_age_ns", &ExternalTimeFreshnessPolicy::maximum_age_ns)
        .def_readonly("maximum_future_skew_ns", &ExternalTimeFreshnessPolicy::maximum_future_skew_ns)
        .def_readonly("maximum_uncertainty_ns", &ExternalTimeFreshnessPolicy::maximum_uncertainty_ns)
        .def_readonly("minimum_sources", &ExternalTimeFreshnessPolicy::minimum_sources)
        .def_readonly("require_distinct_services", &ExternalTimeFreshnessPolicy::require_distinct_services)
        .def_readonly("maximum_assertions", &ExternalTimeFreshnessPolicy::maximum_assertions)
        .def_readonly("allowed_sources", &ExternalTimeFreshnessPolicy::allowed_sources)
        .def("valid", &ExternalTimeFreshnessPolicy::valid);

    py::enum_<ExternalTimeFreshnessStatus>(module, "ExternalTimeFreshnessStatus")
        .value("FRESH", ExternalTimeFreshnessStatus::Fresh)
        .value("INCOMPLETE", ExternalTimeFreshnessStatus::Incomplete)
        .value("STALE", ExternalTimeFreshnessStatus::Stale)
        .value("FUTURE", ExternalTimeFreshnessStatus::Future)
        .value("INCONSISTENT", ExternalTimeFreshnessStatus::Inconsistent);

    py::class_<ExternalTimeFreshnessReport>(module, "ExternalTimeFreshnessReport")
        .def_readonly("id", &ExternalTimeFreshnessReport::id)
        .def_readonly("subject_id", &ExternalTimeFreshnessReport::subject_id)
        .def_readonly("trust_bundle_id", &ExternalTimeFreshnessReport::trust_bundle_id)
        .def_readonly("policy_id", &ExternalTimeFreshnessReport::policy_id)
        .def_readonly("clock_id", &ExternalTimeFreshnessReport::clock_id)
        .def_readonly("status", &ExternalTimeFreshnessReport::status)
        .def_readonly("evaluated_at_ns", &ExternalTimeFreshnessReport::evaluated_at_ns)
        .def_readonly("intersection_lower_ns", &ExternalTimeFreshnessReport::intersection_lower_ns)
        .def_readonly("intersection_upper_ns", &ExternalTimeFreshnessReport::intersection_upper_ns)
        .def_readonly("authenticated_source_count", &ExternalTimeFreshnessReport::authenticated_source_count)
        .def_readonly("assertion_ids", &ExternalTimeFreshnessReport::assertion_ids)
        .def("valid", &ExternalTimeFreshnessReport::valid)
        .def_property_readonly("evidence", &ExternalTimeFreshnessReport::evidence)
        .def_property_readonly("authorizes_execution", &ExternalTimeFreshnessReport::authorizes_execution);

    py::class_<VerifiableProvenanceBundleLoadOptions>(module, "VerifiableProvenanceBundleLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_statements", &VerifiableProvenanceBundleLoadOptions::maximum_statements)
        .def_readwrite("maximum_time_assertions",
                       &VerifiableProvenanceBundleLoadOptions::maximum_time_assertions)
        .def_readwrite("maximum_policy_entries",
                       &VerifiableProvenanceBundleLoadOptions::maximum_policy_entries)
        .def_readwrite("maximum_payload_bytes", &VerifiableProvenanceBundleLoadOptions::maximum_payload_bytes)
        .def_readwrite("cancellation", &VerifiableProvenanceBundleLoadOptions::cancellation);

    py::class_<VerifiableProvenanceBundle>(module, "VerifiableProvenanceBundle")
        .def_static(
            "create",
            [](ServicePublicKey subject_key, HardwareKeyProvenancePolicy hardware_policy,
               ExternalTimeFreshnessPolicy freshness_policy,
               std::vector<HardwareKeyAttestationStatement> hardware_statements,
               std::vector<ExternalTimeAssertion> time_assertions, const ServiceTrustBundle& trust_bundle) {
                return unwrap(VerifiableProvenanceBundle::create(
                    std::move(subject_key), std::move(hardware_policy), std::move(freshness_policy),
                    std::move(hardware_statements), std::move(time_assertions), trust_bundle));
            },
            py::arg("subject_key"), py::arg("hardware_policy"), py::arg("freshness_policy"),
            py::arg("hardware_statements"), py::arg("time_assertions"), py::arg("trust_bundle"))
        .def_static(
            "load",
            [](const std::filesystem::path& path, const VerifiableProvenanceBundleLoadOptions& options) {
                return unwrap(VerifiableProvenanceBundle::load(path, options));
            },
            py::arg("path"), py::arg("options") = VerifiableProvenanceBundleLoadOptions{})
        .def_property_readonly("storage_schema", &VerifiableProvenanceBundle::storage_schema)
        .def_property_readonly("id", &VerifiableProvenanceBundle::id)
        .def_property_readonly("trust_bundle_id", &VerifiableProvenanceBundle::trust_bundle_id)
        .def_property_readonly("trust_bundle_sequence", &VerifiableProvenanceBundle::trust_bundle_sequence)
        .def_property_readonly("subject_key", &VerifiableProvenanceBundle::subject_key)
        .def_property_readonly("hardware_policy", &VerifiableProvenanceBundle::hardware_policy)
        .def_property_readonly("freshness_policy", &VerifiableProvenanceBundle::freshness_policy)
        .def_property_readonly("hardware_statements", &VerifiableProvenanceBundle::hardware_statements)
        .def_property_readonly("time_assertions", &VerifiableProvenanceBundle::time_assertions)
        .def("valid", &VerifiableProvenanceBundle::valid)
        .def_property_readonly("evidence", &VerifiableProvenanceBundle::evidence)
        .def_property_readonly("authorizes_execution", &VerifiableProvenanceBundle::authorizes_execution)
        .def(
            "save",
            [](const VerifiableProvenanceBundle& bundle, const std::filesystem::path& path,
               const SaveOptions& options) { unwrap_void(bundle.save(path, options)); },
            py::arg("path"), py::arg("options") = SaveOptions{});

    py::class_<VerifiableProvenanceAuditReport>(module, "VerifiableProvenanceAuditReport")
        .def_readonly("id", &VerifiableProvenanceAuditReport::id)
        .def_readonly("bundle_id", &VerifiableProvenanceAuditReport::bundle_id)
        .def_readonly("hardware", &VerifiableProvenanceAuditReport::hardware)
        .def_readonly("freshness", &VerifiableProvenanceAuditReport::freshness)
        .def("valid", &VerifiableProvenanceAuditReport::valid)
        .def_property_readonly("ready", &VerifiableProvenanceAuditReport::ready)
        .def_property_readonly("evidence", &VerifiableProvenanceAuditReport::evidence)
        .def_property_readonly("authorizes_execution",
                               &VerifiableProvenanceAuditReport::authorizes_execution);

    module.def(
        "sign_hardware_key_attestation_statement",
        [](HardwareKeyAttestationInput input, const ServiceTrustBundle& trust_bundle,
           std::string attester_service_id, std::string attester_key_id, const py::bytes& secret_key) {
            const ProvenanceSensitiveBytes copy(secret_key);
            return unwrap(rbfsafe::sign_hardware_key_attestation_statement(
                std::move(input), trust_bundle, std::move(attester_service_id), std::move(attester_key_id),
                copy.view()));
        },
        py::arg("input"), py::arg("trust_bundle"), py::arg("attester_service_id"), py::arg("attester_key_id"),
        py::arg("ed25519_secret_key"));
    module.def(
        "verify_hardware_key_attestation_statement",
        [](const HardwareKeyAttestationStatement& statement, const ServiceTrustBundle& trust_bundle) {
            unwrap_void(rbfsafe::verify_hardware_key_attestation_statement(statement, trust_bundle));
        },
        py::arg("statement"), py::arg("trust_bundle"));
    module.def(
        "replay_hardware_key_provenance",
        [](const std::vector<HardwareKeyAttestationStatement>& statements,
           const ServicePublicKey& expected_subject_key, const ServiceTrustBundle& trust_bundle,
           const HardwareKeyProvenancePolicy& policy, const ProvenanceReplayOptions& options) {
            return unwrap(rbfsafe::replay_hardware_key_provenance(statements, expected_subject_key,
                                                                  trust_bundle, policy, options));
        },
        py::arg("statements"), py::arg("expected_subject_key"), py::arg("trust_bundle"), py::arg("policy"),
        py::arg("options") = ProvenanceReplayOptions{});
    module.def(
        "sign_external_time_assertion",
        [](ExternalTimeAssertionInput input, const ServiceTrustBundle& trust_bundle,
           std::string source_service_id, std::string source_key_id, const py::bytes& secret_key) {
            const ProvenanceSensitiveBytes copy(secret_key);
            return unwrap(rbfsafe::sign_external_time_assertion(std::move(input), trust_bundle,
                                                                std::move(source_service_id),
                                                                std::move(source_key_id), copy.view()));
        },
        py::arg("input"), py::arg("trust_bundle"), py::arg("source_service_id"), py::arg("source_key_id"),
        py::arg("ed25519_secret_key"));
    module.def(
        "verify_external_time_assertion",
        [](const ExternalTimeAssertion& assertion, const ServiceTrustBundle& trust_bundle) {
            unwrap_void(rbfsafe::verify_external_time_assertion(assertion, trust_bundle));
        },
        py::arg("assertion"), py::arg("trust_bundle"));
    module.def(
        "evaluate_external_time_freshness",
        [](std::string subject_id, const std::vector<ExternalTimeAssertion>& assertions,
           const ServiceTrustBundle& trust_bundle, const ExternalTimeFreshnessPolicy& policy,
           std::uint64_t evaluated_at_ns, const ProvenanceReplayOptions& options) {
            return unwrap(rbfsafe::evaluate_external_time_freshness(
                std::move(subject_id), assertions, trust_bundle, policy, evaluated_at_ns, options));
        },
        py::arg("subject_id"), py::arg("assertions"), py::arg("trust_bundle"), py::arg("policy"),
        py::arg("evaluated_at_ns"), py::arg("options") = ProvenanceReplayOptions{});
    module.def(
        "replay_verifiable_provenance",
        [](const VerifiableProvenanceBundle& bundle, const ServiceTrustBundle& trust_bundle,
           std::uint64_t evaluated_at_ns, const ProvenanceReplayOptions& options) {
            return unwrap(
                rbfsafe::replay_verifiable_provenance(bundle, trust_bundle, evaluated_at_ns, options));
        },
        py::arg("bundle"), py::arg("trust_bundle"), py::arg("evaluated_at_ns"),
        py::arg("options") = ProvenanceReplayOptions{});
    module.def("hardware_attestation_scope_name", &hardware_attestation_scope_name);
    module.def("hardware_provenance_status_name", &hardware_provenance_status_name);
    module.def("external_time_freshness_status_name", &external_time_freshness_status_name);
}

} // namespace rbfsafe::python_binding
