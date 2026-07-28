#include "binding_support.h"

#include <rbfsafe/transparency.h>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <array>
#include <span>
#include <string>
#include <utility>

namespace rbfsafe::python_binding {
namespace {

std::span<const std::byte> bytes_view(const std::string& value) {
    return std::as_bytes(std::span(value.data(), value.size()));
}

class SensitiveBytes {
  public:
    explicit SensitiveBytes(const py::bytes& value) : value_(static_cast<std::string>(value)) {}
    SensitiveBytes(const SensitiveBytes&) = delete;
    SensitiveBytes& operator=(const SensitiveBytes&) = delete;
    ~SensitiveBytes() {
        volatile char* current = value_.data();
        for (std::size_t index = 0; index < value_.size(); ++index)
            current[index] = 0;
    }

    std::span<const std::byte> view() const { return bytes_view(value_); }

  private:
    std::string value_;
};

template <std::size_t Size> py::bytes array_bytes(const std::array<std::byte, Size>& value) {
    return py::bytes(reinterpret_cast<const char*>(value.data()), value.size());
}

} // namespace

void bind_transparency(py::module_& module) {
    py::class_<DeploymentTransparencyAnchor>(module, "DeploymentTransparencyAnchor")
        .def_static(
            "create",
            [](const ReviewedDeploymentProfile& reviewed, const ServiceTrustHistory& history,
               const ServiceTrustCheckpoint& checkpoint, const std::string& expected_checkpoint_id) {
                return unwrap(DeploymentTransparencyAnchor::create(reviewed, history, checkpoint,
                                                                   expected_checkpoint_id));
            },
            py::arg("reviewed_profile"), py::arg("trust_history"), py::arg("trust_checkpoint"),
            py::arg("expected_checkpoint_id"))
        .def_readonly("storage_schema", &DeploymentTransparencyAnchor::storage_schema)
        .def_readonly("id", &DeploymentTransparencyAnchor::id)
        .def_readonly("deployment_id", &DeploymentTransparencyAnchor::deployment_id)
        .def_readonly("reviewed_profile_id", &DeploymentTransparencyAnchor::reviewed_profile_id)
        .def_readonly("approval_set_id", &DeploymentTransparencyAnchor::approval_set_id)
        .def_readonly("robot_digest", &DeploymentTransparencyAnchor::robot_digest)
        .def_readonly("controller_digest", &DeploymentTransparencyAnchor::controller_digest)
        .def_readonly("platform_digest", &DeploymentTransparencyAnchor::platform_digest)
        .def_readonly("runtime_digest", &DeploymentTransparencyAnchor::runtime_digest)
        .def_readonly("trust_root_bundle_id", &DeploymentTransparencyAnchor::trust_root_bundle_id)
        .def_readonly("trust_checkpoint_id", &DeploymentTransparencyAnchor::trust_checkpoint_id)
        .def_readonly("trust_bundle_id", &DeploymentTransparencyAnchor::trust_bundle_id)
        .def_readonly("trust_bundle_sequence", &DeploymentTransparencyAnchor::trust_bundle_sequence)
        .def_readonly("trust_head_record_id", &DeploymentTransparencyAnchor::trust_head_record_id)
        .def("valid", &DeploymentTransparencyAnchor::valid)
        .def_property_readonly("evidence", &DeploymentTransparencyAnchor::evidence)
        .def_property_readonly("authorizes_execution", &DeploymentTransparencyAnchor::authorizes_execution);

    py::class_<IndependentRuntimeObservationInput>(module, "IndependentRuntimeObservationInput")
        .def(py::init<>())
        .def_readwrite("runtime", &IndependentRuntimeObservationInput::runtime)
        .def_readwrite("observation_sequence", &IndependentRuntimeObservationInput::observation_sequence)
        .def_readwrite("observed_monotonic_ns", &IndependentRuntimeObservationInput::observed_monotonic_ns)
        .def_readwrite("monitor_state", &IndependentRuntimeObservationInput::monitor_state)
        .def_readwrite("configuration_digest", &IndependentRuntimeObservationInput::configuration_digest);

    py::class_<IndependentRuntimeObservation>(module, "IndependentRuntimeObservation")
        .def_static(
            "create",
            [](const BoundedExecutionSession& session, const ExecutionLedger& ledger,
               const ExecutionCommandAuthorization& authorization, IndependentRuntimeObservationInput input) {
                return unwrap(
                    IndependentRuntimeObservation::create(session, ledger, authorization, std::move(input)));
            },
            py::arg("session"), py::arg("ledger"), py::arg("authorization"), py::arg("input"))
        .def_readonly("storage_schema", &IndependentRuntimeObservation::storage_schema)
        .def_readonly("id", &IndependentRuntimeObservation::id)
        .def_readonly("session_id", &IndependentRuntimeObservation::session_id)
        .def_readonly("ledger_id", &IndependentRuntimeObservation::ledger_id)
        .def_readonly("ledger_record_id", &IndependentRuntimeObservation::ledger_record_id)
        .def_readonly("authorization_id", &IndependentRuntimeObservation::authorization_id)
        .def_readonly("command_sequence_id", &IndependentRuntimeObservation::command_sequence_id)
        .def_readonly("command_index", &IndependentRuntimeObservation::command_index)
        .def_readonly("command_digest", &IndependentRuntimeObservation::command_digest)
        .def_readonly("runtime", &IndependentRuntimeObservation::runtime)
        .def_readonly("observation_sequence", &IndependentRuntimeObservation::observation_sequence)
        .def_readonly("observed_monotonic_ns", &IndependentRuntimeObservation::observed_monotonic_ns)
        .def_readonly("monitor_state", &IndependentRuntimeObservation::monitor_state)
        .def_readonly("configuration_digest", &IndependentRuntimeObservation::configuration_digest)
        .def("valid", &IndependentRuntimeObservation::valid)
        .def_property_readonly("evidence", &IndependentRuntimeObservation::evidence)
        .def_property_readonly("authorizes_execution", &IndependentRuntimeObservation::authorizes_execution);

    py::class_<RuntimeObservationAttestation>(module, "RuntimeObservationAttestation")
        .def_readonly("storage_schema", &RuntimeObservationAttestation::storage_schema)
        .def_readonly("id", &RuntimeObservationAttestation::id)
        .def_readonly("observation_id", &RuntimeObservationAttestation::observation_id)
        .def_readonly("source_service_id", &RuntimeObservationAttestation::source_service_id)
        .def_readonly("source_key_id", &RuntimeObservationAttestation::source_key_id)
        .def_readonly("algorithm", &RuntimeObservationAttestation::algorithm)
        .def_readonly("authentication_tag", &RuntimeObservationAttestation::authentication_tag)
        .def("valid", &RuntimeObservationAttestation::valid);

    py::class_<RuntimeObservationPolicy>(module, "RuntimeObservationPolicy")
        .def(py::init<>())
        .def_readwrite("minimum_attestations", &RuntimeObservationPolicy::minimum_attestations)
        .def_readwrite("require_distinct_services", &RuntimeObservationPolicy::require_distinct_services)
        .def_readwrite("exclude_controller_service", &RuntimeObservationPolicy::exclude_controller_service);

    py::class_<RuntimeObservationAttestationSet>(module, "RuntimeObservationAttestationSet")
        .def_readonly("storage_schema", &RuntimeObservationAttestationSet::storage_schema)
        .def_readonly("id", &RuntimeObservationAttestationSet::id)
        .def_readonly("observation", &RuntimeObservationAttestationSet::observation)
        .def_readonly("policy", &RuntimeObservationAttestationSet::policy)
        .def_readonly("attestations", &RuntimeObservationAttestationSet::attestations)
        .def("valid", &RuntimeObservationAttestationSet::valid)
        .def_property_readonly("evidence", &RuntimeObservationAttestationSet::evidence)
        .def_property_readonly("authorizes_execution",
                               &RuntimeObservationAttestationSet::authorizes_execution);

    py::class_<TransparencyLogIdentity>(module, "TransparencyLogIdentity")
        .def_static(
            "create",
            [](std::string log_namespace, std::string signer_service_id, std::string signer_key_id,
               const py::bytes& public_key) {
                const auto copy = static_cast<std::string>(public_key);
                return unwrap(TransparencyLogIdentity::create(std::move(log_namespace),
                                                              std::move(signer_service_id),
                                                              std::move(signer_key_id), bytes_view(copy)));
            },
            py::arg("log_namespace"), py::arg("signer_service_id"), py::arg("signer_key_id"),
            py::arg("signer_public_key"))
        .def_readonly("storage_schema", &TransparencyLogIdentity::storage_schema)
        .def_readonly("id", &TransparencyLogIdentity::id)
        .def_readonly("log_namespace", &TransparencyLogIdentity::log_namespace)
        .def_readonly("signer_service_id", &TransparencyLogIdentity::signer_service_id)
        .def_readonly("signer_key_id", &TransparencyLogIdentity::signer_key_id)
        .def_property_readonly(
            "signer_public_key",
            [](const TransparencyLogIdentity& identity) { return array_bytes(identity.signer_public_key); })
        .def("valid", &TransparencyLogIdentity::valid);

    py::enum_<TransparencyLeafKind>(module, "TransparencyLeafKind")
        .value("DEPLOYMENT_ANCHOR", TransparencyLeafKind::DeploymentAnchor)
        .value("RUNTIME_OBSERVATION", TransparencyLeafKind::RuntimeObservation);

    py::class_<TransparencyLogLeaf>(module, "TransparencyLogLeaf")
        .def_readonly("storage_schema", &TransparencyLogLeaf::storage_schema)
        .def_readonly("index", &TransparencyLogLeaf::index)
        .def_readonly("id", &TransparencyLogLeaf::id)
        .def_readonly("log_id", &TransparencyLogLeaf::log_id)
        .def_readonly("kind", &TransparencyLogLeaf::kind)
        .def_readonly("deployment_anchor", &TransparencyLogLeaf::deployment_anchor)
        .def_readonly("runtime_observation", &TransparencyLogLeaf::runtime_observation)
        .def("valid", &TransparencyLogLeaf::valid);

    py::class_<TransparencyLogCheckpoint>(module, "TransparencyLogCheckpoint")
        .def_readonly("storage_schema", &TransparencyLogCheckpoint::storage_schema)
        .def_readonly("id", &TransparencyLogCheckpoint::id)
        .def_readonly("log_id", &TransparencyLogCheckpoint::log_id)
        .def_readonly("tree_size", &TransparencyLogCheckpoint::tree_size)
        .def_readonly("root_hash", &TransparencyLogCheckpoint::root_hash)
        .def_readonly("previous_checkpoint_id", &TransparencyLogCheckpoint::previous_checkpoint_id)
        .def_readonly("signer_service_id", &TransparencyLogCheckpoint::signer_service_id)
        .def_readonly("signer_key_id", &TransparencyLogCheckpoint::signer_key_id)
        .def_readonly("algorithm", &TransparencyLogCheckpoint::algorithm)
        .def_readonly("authentication_tag", &TransparencyLogCheckpoint::authentication_tag)
        .def("valid", &TransparencyLogCheckpoint::valid);

    py::class_<TransparencyLogRecord>(module, "TransparencyLogRecord")
        .def_readonly("storage_schema", &TransparencyLogRecord::storage_schema)
        .def_readonly("sequence", &TransparencyLogRecord::sequence)
        .def_readonly("id", &TransparencyLogRecord::id)
        .def_readonly("parent_id", &TransparencyLogRecord::parent_id)
        .def_readonly("log_id", &TransparencyLogRecord::log_id)
        .def_readonly("leaf", &TransparencyLogRecord::leaf)
        .def_readonly("checkpoint", &TransparencyLogRecord::checkpoint)
        .def("valid", &TransparencyLogRecord::valid);

    py::class_<TransparencyInclusionProof>(module, "TransparencyInclusionProof")
        .def_readonly("storage_schema", &TransparencyInclusionProof::storage_schema)
        .def_readonly("id", &TransparencyInclusionProof::id)
        .def_readonly("log_id", &TransparencyInclusionProof::log_id)
        .def_readonly("checkpoint_id", &TransparencyInclusionProof::checkpoint_id)
        .def_readonly("leaf_id", &TransparencyInclusionProof::leaf_id)
        .def_readonly("leaf_index", &TransparencyInclusionProof::leaf_index)
        .def_readonly("tree_size", &TransparencyInclusionProof::tree_size)
        .def_readonly("root_hash", &TransparencyInclusionProof::root_hash)
        .def_readonly("sibling_hashes", &TransparencyInclusionProof::sibling_hashes)
        .def("valid", &TransparencyInclusionProof::valid);

    py::class_<TransparencyConsistencyWitness>(module, "TransparencyConsistencyWitness")
        .def_readonly("storage_schema", &TransparencyConsistencyWitness::storage_schema)
        .def_readonly("id", &TransparencyConsistencyWitness::id)
        .def_readonly("log_id", &TransparencyConsistencyWitness::log_id)
        .def_readonly("old_checkpoint_id", &TransparencyConsistencyWitness::old_checkpoint_id)
        .def_readonly("new_checkpoint_id", &TransparencyConsistencyWitness::new_checkpoint_id)
        .def_readonly("old_tree_size", &TransparencyConsistencyWitness::old_tree_size)
        .def_readonly("new_tree_size", &TransparencyConsistencyWitness::new_tree_size)
        .def_readonly("old_root_hash", &TransparencyConsistencyWitness::old_root_hash)
        .def_readonly("new_root_hash", &TransparencyConsistencyWitness::new_root_hash)
        .def_readonly("ordered_leaf_ids", &TransparencyConsistencyWitness::ordered_leaf_ids)
        .def("valid", &TransparencyConsistencyWitness::valid);

    py::class_<TransparencyMerkleSubtree>(module, "TransparencyMerkleSubtree")
        .def_readonly("level", &TransparencyMerkleSubtree::level)
        .def_readonly("hash", &TransparencyMerkleSubtree::hash)
        .def("valid", &TransparencyMerkleSubtree::valid);

    py::class_<TransparencyCompactConsistencyProof>(module, "TransparencyCompactConsistencyProof")
        .def_readonly("storage_schema", &TransparencyCompactConsistencyProof::storage_schema)
        .def_readonly("id", &TransparencyCompactConsistencyProof::id)
        .def_readonly("log_id", &TransparencyCompactConsistencyProof::log_id)
        .def_readonly("old_checkpoint_id", &TransparencyCompactConsistencyProof::old_checkpoint_id)
        .def_readonly("new_checkpoint_id", &TransparencyCompactConsistencyProof::new_checkpoint_id)
        .def_readonly("old_tree_size", &TransparencyCompactConsistencyProof::old_tree_size)
        .def_readonly("new_tree_size", &TransparencyCompactConsistencyProof::new_tree_size)
        .def_readonly("old_root_hash", &TransparencyCompactConsistencyProof::old_root_hash)
        .def_readonly("new_root_hash", &TransparencyCompactConsistencyProof::new_root_hash)
        .def_readonly("old_frontier", &TransparencyCompactConsistencyProof::old_frontier)
        .def_readonly("appended_subtrees", &TransparencyCompactConsistencyProof::appended_subtrees)
        .def("valid", &TransparencyCompactConsistencyProof::valid);

    py::class_<TransparencyLogAuditReport>(module, "TransparencyLogAuditReport")
        .def_readonly("id", &TransparencyLogAuditReport::id)
        .def_readonly("log_id", &TransparencyLogAuditReport::log_id)
        .def_readonly("current_checkpoint_id", &TransparencyLogAuditReport::current_checkpoint_id)
        .def_readonly("current_root_hash", &TransparencyLogAuditReport::current_root_hash)
        .def_readonly("verified_records", &TransparencyLogAuditReport::verified_records)
        .def_readonly("deployment_anchor_count", &TransparencyLogAuditReport::deployment_anchor_count)
        .def_readonly("runtime_observation_count", &TransparencyLogAuditReport::runtime_observation_count)
        .def("valid", &TransparencyLogAuditReport::valid)
        .def_property_readonly("evidence", &TransparencyLogAuditReport::evidence)
        .def_property_readonly("authorizes_execution", &TransparencyLogAuditReport::authorizes_execution);

    py::class_<TransparencyLogLoadOptions>(module, "TransparencyLogLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_records", &TransparencyLogLoadOptions::maximum_records)
        .def_readwrite("maximum_attestations_per_observation",
                       &TransparencyLogLoadOptions::maximum_attestations_per_observation)
        .def_readwrite("maximum_total_attestations", &TransparencyLogLoadOptions::maximum_total_attestations)
        .def_readwrite("maximum_manifest_bytes", &TransparencyLogLoadOptions::maximum_manifest_bytes)
        .def_readwrite("maximum_record_bytes", &TransparencyLogLoadOptions::maximum_record_bytes)
        .def_readwrite("cancellation", &TransparencyLogLoadOptions::cancellation);

    py::class_<TransparencyLog>(module, "TransparencyLog")
        .def_static(
            "create",
            [](const std::filesystem::path& directory, TransparencyLogIdentity identity) {
                return unwrap(TransparencyLog::create(directory, std::move(identity)));
            },
            py::arg("directory"), py::arg("identity"))
        .def_static(
            "open",
            [](const std::filesystem::path& directory, const TransparencyLogIdentity& identity,
               const std::string& expected_checkpoint_id, const TransparencyLogLoadOptions& options) {
                return unwrap(TransparencyLog::open(directory, identity, expected_checkpoint_id, options));
            },
            py::arg("directory"), py::arg("identity"), py::arg("expected_checkpoint_id"),
            py::arg("options") = TransparencyLogLoadOptions{})
        .def_property_readonly("directory", &TransparencyLog::directory)
        .def_property_readonly("identity", &TransparencyLog::identity)
        .def_property_readonly("records", &TransparencyLog::records)
        .def_property_readonly("current_checkpoint_id", &TransparencyLog::current_checkpoint_id)
        .def_property_readonly("current_root_hash", &TransparencyLog::current_root_hash)
        .def("valid", &TransparencyLog::valid)
        .def_property_readonly("evidence", &TransparencyLog::evidence)
        .def_property_readonly("authorizes_execution", &TransparencyLog::authorizes_execution)
        .def(
            "publish_deployment_anchor",
            [](TransparencyLog& log, DeploymentTransparencyAnchor anchor, const py::bytes& secret_key,
               const std::string& expected_checkpoint_id) {
                const SensitiveBytes copy(secret_key);
                return unwrap(
                    log.publish_deployment_anchor(std::move(anchor), copy.view(), expected_checkpoint_id));
            },
            py::arg("anchor"), py::arg("signer_secret_key"), py::arg("expected_current_checkpoint_id"))
        .def(
            "publish_runtime_observation",
            [](TransparencyLog& log, RuntimeObservationAttestationSet observation,
               const py::bytes& secret_key, const std::string& expected_checkpoint_id) {
                const SensitiveBytes copy(secret_key);
                return unwrap(log.publish_runtime_observation(std::move(observation), copy.view(),
                                                              expected_checkpoint_id));
            },
            py::arg("observation"), py::arg("signer_secret_key"), py::arg("expected_current_checkpoint_id"))
        .def(
            "inclusion_proof",
            [](const TransparencyLog& log, std::uint64_t leaf_index) {
                return unwrap(log.inclusion_proof(leaf_index));
            },
            py::arg("leaf_index"))
        .def(
            "consistency_witness",
            [](const TransparencyLog& log, std::uint64_t old_tree_size) {
                return unwrap(log.consistency_witness(old_tree_size));
            },
            py::arg("old_tree_size"))
        .def(
            "compact_consistency_proof",
            [](const TransparencyLog& log, std::uint64_t old_tree_size) {
                return unwrap(log.compact_consistency_proof(old_tree_size));
            },
            py::arg("old_tree_size"))
        .def("audit", [](const TransparencyLog& log) { return unwrap(log.audit()); });

    module.def("valid_runtime_observation_policy", &valid_runtime_observation_policy);
    module.def(
        "sign_runtime_observation",
        [](const IndependentRuntimeObservation& observation, std::string source_service_id,
           std::string source_key_id, const py::bytes& secret_key) {
            const SensitiveBytes copy(secret_key);
            return unwrap(rbfsafe::sign_runtime_observation(observation, std::move(source_service_id),
                                                            std::move(source_key_id), copy.view()));
        },
        py::arg("observation"), py::arg("source_service_id"), py::arg("source_key_id"),
        py::arg("ed25519_secret_key"));
    module.def(
        "verify_runtime_observation_attestation",
        [](const IndependentRuntimeObservation& observation, const RuntimeObservationAttestation& attestation,
           const ServiceTrustBundle& trust_bundle) {
            unwrap_void(
                rbfsafe::verify_runtime_observation_attestation(observation, attestation, trust_bundle));
        },
        py::arg("observation"), py::arg("attestation"), py::arg("trust_bundle"));
    module.def(
        "assemble_runtime_observation_attestations",
        [](const BoundedExecutionSession& session, IndependentRuntimeObservation observation,
           RuntimeObservationPolicy policy, std::vector<RuntimeObservationAttestation> attestations,
           const ServiceTrustBundle& trust_bundle) {
            return unwrap(rbfsafe::assemble_runtime_observation_attestations(
                session, std::move(observation), policy, std::move(attestations), trust_bundle));
        },
        py::arg("session"), py::arg("observation"), py::arg("policy"), py::arg("attestations"),
        py::arg("trust_bundle"));
    module.def(
        "verify_runtime_observation_attestations",
        [](const BoundedExecutionSession& session, const RuntimeObservationAttestationSet& attestation_set,
           const ServiceTrustBundle& trust_bundle) {
            unwrap_void(
                rbfsafe::verify_runtime_observation_attestations(session, attestation_set, trust_bundle));
        },
        py::arg("session"), py::arg("attestation_set"), py::arg("trust_bundle"));
    module.def(
        "verify_transparency_log_checkpoint",
        [](const TransparencyLogIdentity& identity, const TransparencyLogCheckpoint& checkpoint) {
            unwrap_void(rbfsafe::verify_transparency_log_checkpoint(identity, checkpoint));
        },
        py::arg("identity"), py::arg("checkpoint"));
    module.def(
        "verify_transparency_inclusion",
        [](const TransparencyLogIdentity& identity, const TransparencyLogCheckpoint& checkpoint,
           const TransparencyLogLeaf& leaf, const TransparencyInclusionProof& proof) {
            unwrap_void(rbfsafe::verify_transparency_inclusion(identity, checkpoint, leaf, proof));
        },
        py::arg("identity"), py::arg("checkpoint"), py::arg("leaf"), py::arg("proof"));
    module.def(
        "verify_transparency_consistency",
        [](const TransparencyLogIdentity& identity, const TransparencyLogCheckpoint& old_checkpoint,
           const TransparencyLogCheckpoint& new_checkpoint, const TransparencyConsistencyWitness& witness) {
            unwrap_void(
                rbfsafe::verify_transparency_consistency(identity, old_checkpoint, new_checkpoint, witness));
        },
        py::arg("identity"), py::arg("old_checkpoint"), py::arg("new_checkpoint"), py::arg("witness"));
    module.def(
        "verify_transparency_compact_consistency",
        [](const TransparencyLogIdentity& identity, const TransparencyLogCheckpoint& old_checkpoint,
           const TransparencyLogCheckpoint& new_checkpoint,
           const TransparencyCompactConsistencyProof& proof) {
            unwrap_void(rbfsafe::verify_transparency_compact_consistency(identity, old_checkpoint,
                                                                         new_checkpoint, proof));
        },
        py::arg("identity"), py::arg("old_checkpoint"), py::arg("new_checkpoint"), py::arg("proof"));
    module.def("transparency_leaf_kind_name", &transparency_leaf_kind_name);
}

} // namespace rbfsafe::python_binding
