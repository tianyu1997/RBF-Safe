#include "binding_support.h"

#include <rbfsafe/modules/assurance.h>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <span>
#include <string>
#include <utility>

namespace rbfsafe::python_binding {
namespace {

std::span<const std::byte> witness_bytes_view(const std::string& value) {
    return std::as_bytes(std::span(value.data(), value.size()));
}

class WitnessSensitiveBytes {
  public:
    explicit WitnessSensitiveBytes(const py::bytes& value) : value_(static_cast<std::string>(value)) {}
    WitnessSensitiveBytes(const WitnessSensitiveBytes&) = delete;
    WitnessSensitiveBytes& operator=(const WitnessSensitiveBytes&) = delete;
    ~WitnessSensitiveBytes() {
        volatile char* current = value_.data();
        for (std::size_t index = 0; index < value_.size(); ++index)
            current[index] = 0;
    }

    std::span<const std::byte> view() const { return witness_bytes_view(value_); }

  private:
    std::string value_;
};

} // namespace

void bind_witness(py::module_& module) {
    py::class_<TransparencyCheckpointWitnessPolicy>(module, "TransparencyCheckpointWitnessPolicy")
        .def(py::init<>())
        .def_readwrite("minimum_witnesses", &TransparencyCheckpointWitnessPolicy::minimum_witnesses)
        .def_readwrite("require_distinct_services",
                       &TransparencyCheckpointWitnessPolicy::require_distinct_services)
        .def_readwrite("exclude_log_signer", &TransparencyCheckpointWitnessPolicy::exclude_log_signer);

    py::class_<TransparencyCheckpointCosignature>(module, "TransparencyCheckpointCosignature")
        .def_readonly("storage_schema", &TransparencyCheckpointCosignature::storage_schema)
        .def_readonly("id", &TransparencyCheckpointCosignature::id)
        .def_readonly("log_id", &TransparencyCheckpointCosignature::log_id)
        .def_readonly("checkpoint_id", &TransparencyCheckpointCosignature::checkpoint_id)
        .def_readonly("tree_size", &TransparencyCheckpointCosignature::tree_size)
        .def_readonly("root_hash", &TransparencyCheckpointCosignature::root_hash)
        .def_readonly("trust_bundle_id", &TransparencyCheckpointCosignature::trust_bundle_id)
        .def_readonly("trust_bundle_sequence", &TransparencyCheckpointCosignature::trust_bundle_sequence)
        .def_readonly("witness_service_id", &TransparencyCheckpointCosignature::witness_service_id)
        .def_readonly("witness_key_id", &TransparencyCheckpointCosignature::witness_key_id)
        .def_readonly("algorithm", &TransparencyCheckpointCosignature::algorithm)
        .def_readonly("authentication_tag", &TransparencyCheckpointCosignature::authentication_tag)
        .def("valid", &TransparencyCheckpointCosignature::valid);

    py::class_<WitnessedTransparencyCheckpoint>(module, "WitnessedTransparencyCheckpoint")
        .def_readonly("storage_schema", &WitnessedTransparencyCheckpoint::storage_schema)
        .def_readonly("id", &WitnessedTransparencyCheckpoint::id)
        .def_readonly("log_id", &WitnessedTransparencyCheckpoint::log_id)
        .def_readonly("trust_bundle_id", &WitnessedTransparencyCheckpoint::trust_bundle_id)
        .def_readonly("trust_bundle_sequence", &WitnessedTransparencyCheckpoint::trust_bundle_sequence)
        .def_readonly("policy", &WitnessedTransparencyCheckpoint::policy)
        .def_readonly("checkpoint", &WitnessedTransparencyCheckpoint::checkpoint)
        .def_readonly("cosignatures", &WitnessedTransparencyCheckpoint::cosignatures)
        .def("valid", &WitnessedTransparencyCheckpoint::valid)
        .def_property_readonly("evidence", &WitnessedTransparencyCheckpoint::evidence)
        .def_property_readonly("authorizes_execution",
                               &WitnessedTransparencyCheckpoint::authorizes_execution);

    py::class_<TransparencyCheckpointGossip>(module, "TransparencyCheckpointGossip")
        .def_readonly("storage_schema", &TransparencyCheckpointGossip::storage_schema)
        .def_readonly("id", &TransparencyCheckpointGossip::id)
        .def_readonly("log_id", &TransparencyCheckpointGossip::log_id)
        .def_readonly("sender_sequence", &TransparencyCheckpointGossip::sender_sequence)
        .def_readonly("parent_gossip_id", &TransparencyCheckpointGossip::parent_gossip_id)
        .def_readonly("recipient_service_id", &TransparencyCheckpointGossip::recipient_service_id)
        .def_readonly("sender_service_id", &TransparencyCheckpointGossip::sender_service_id)
        .def_readonly("sender_key_id", &TransparencyCheckpointGossip::sender_key_id)
        .def_readonly("trust_bundle_id", &TransparencyCheckpointGossip::trust_bundle_id)
        .def_readonly("trust_bundle_sequence", &TransparencyCheckpointGossip::trust_bundle_sequence)
        .def_readonly("witnessed_checkpoint", &TransparencyCheckpointGossip::witnessed_checkpoint)
        .def_readonly("consistency_proof", &TransparencyCheckpointGossip::consistency_proof)
        .def_readonly("algorithm", &TransparencyCheckpointGossip::algorithm)
        .def_readonly("authentication_tag", &TransparencyCheckpointGossip::authentication_tag)
        .def("valid", &TransparencyCheckpointGossip::valid)
        .def_property_readonly("evidence", &TransparencyCheckpointGossip::evidence)
        .def_property_readonly("authorizes_execution", &TransparencyCheckpointGossip::authorizes_execution);

    py::enum_<TransparencyGossipConflictType>(module, "TransparencyGossipConflictType")
        .value("SAME_SIZE_EQUIVOCATION", TransparencyGossipConflictType::SameSizeEquivocation)
        .value("INVALID_CONSISTENCY_PROOF", TransparencyGossipConflictType::InvalidConsistencyProof);

    py::class_<TransparencyGossipConflict>(module, "TransparencyGossipConflict")
        .def_readonly("id", &TransparencyGossipConflict::id)
        .def_readonly("type", &TransparencyGossipConflict::type)
        .def_readonly("first_gossip_id", &TransparencyGossipConflict::first_gossip_id)
        .def_readonly("second_gossip_id", &TransparencyGossipConflict::second_gossip_id)
        .def_readonly("first_checkpoint_id", &TransparencyGossipConflict::first_checkpoint_id)
        .def_readonly("second_checkpoint_id", &TransparencyGossipConflict::second_checkpoint_id)
        .def_readonly("first_tree_size", &TransparencyGossipConflict::first_tree_size)
        .def_readonly("second_tree_size", &TransparencyGossipConflict::second_tree_size)
        .def_readonly("consistency_proof_id", &TransparencyGossipConflict::consistency_proof_id)
        .def("valid", &TransparencyGossipConflict::valid);

    py::enum_<TransparencyGossipStatus>(module, "TransparencyGossipStatus")
        .value("CONSISTENT", TransparencyGossipStatus::Consistent)
        .value("INCOMPLETE", TransparencyGossipStatus::Incomplete)
        .value("SPLIT_VIEW", TransparencyGossipStatus::SplitView);

    py::class_<TransparencyGossipAuditReport>(module, "TransparencyGossipAuditReport")
        .def_readonly("id", &TransparencyGossipAuditReport::id)
        .def_readonly("log_id", &TransparencyGossipAuditReport::log_id)
        .def_readonly("trust_bundle_id", &TransparencyGossipAuditReport::trust_bundle_id)
        .def_readonly("trust_bundle_sequence", &TransparencyGossipAuditReport::trust_bundle_sequence)
        .def_readonly("status", &TransparencyGossipAuditReport::status)
        .def_readonly("authenticated_gossip_count",
                      &TransparencyGossipAuditReport::authenticated_gossip_count)
        .def_readonly("unique_checkpoint_count", &TransparencyGossipAuditReport::unique_checkpoint_count)
        .def_readonly("linked_checkpoint_pairs", &TransparencyGossipAuditReport::linked_checkpoint_pairs)
        .def_readonly("unlinked_checkpoint_pairs", &TransparencyGossipAuditReport::unlinked_checkpoint_pairs)
        .def_readonly("conflicts", &TransparencyGossipAuditReport::conflicts)
        .def("valid", &TransparencyGossipAuditReport::valid)
        .def_property_readonly("evidence", &TransparencyGossipAuditReport::evidence)
        .def_property_readonly("authorizes_execution", &TransparencyGossipAuditReport::authorizes_execution);

    py::class_<TransparencyGossipAuditOptions>(module, "TransparencyGossipAuditOptions")
        .def(py::init<>())
        .def_readwrite("maximum_gossip_messages", &TransparencyGossipAuditOptions::maximum_gossip_messages)
        .def_readwrite("maximum_unique_checkpoints",
                       &TransparencyGossipAuditOptions::maximum_unique_checkpoints)
        .def_readwrite("maximum_pair_checks", &TransparencyGossipAuditOptions::maximum_pair_checks)
        .def_readwrite("maximum_graph_steps", &TransparencyGossipAuditOptions::maximum_graph_steps)
        .def_readwrite("cancellation", &TransparencyGossipAuditOptions::cancellation);

    py::class_<TransparencyGossipRecord>(module, "TransparencyGossipRecord")
        .def_readonly("storage_schema", &TransparencyGossipRecord::storage_schema)
        .def_readonly("sequence", &TransparencyGossipRecord::sequence)
        .def_readonly("id", &TransparencyGossipRecord::id)
        .def_readonly("parent_id", &TransparencyGossipRecord::parent_id)
        .def_readonly("log_id", &TransparencyGossipRecord::log_id)
        .def_readonly("trust_bundle_id", &TransparencyGossipRecord::trust_bundle_id)
        .def_readonly("gossip", &TransparencyGossipRecord::gossip)
        .def("valid", &TransparencyGossipRecord::valid);

    py::class_<TransparencyGossipArchiveLoadOptions>(module, "TransparencyGossipArchiveLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_records", &TransparencyGossipArchiveLoadOptions::maximum_records)
        .def_readwrite("maximum_witnesses_per_checkpoint",
                       &TransparencyGossipArchiveLoadOptions::maximum_witnesses_per_checkpoint)
        .def_readwrite("maximum_total_witnesses",
                       &TransparencyGossipArchiveLoadOptions::maximum_total_witnesses)
        .def_readwrite("maximum_proof_subtrees",
                       &TransparencyGossipArchiveLoadOptions::maximum_proof_subtrees)
        .def_readwrite("maximum_total_proof_subtrees",
                       &TransparencyGossipArchiveLoadOptions::maximum_total_proof_subtrees)
        .def_readwrite("maximum_unique_checkpoints",
                       &TransparencyGossipArchiveLoadOptions::maximum_unique_checkpoints)
        .def_readwrite("maximum_pair_checks", &TransparencyGossipArchiveLoadOptions::maximum_pair_checks)
        .def_readwrite("maximum_graph_steps", &TransparencyGossipArchiveLoadOptions::maximum_graph_steps)
        .def_readwrite("maximum_manifest_bytes",
                       &TransparencyGossipArchiveLoadOptions::maximum_manifest_bytes)
        .def_readwrite("maximum_record_bytes", &TransparencyGossipArchiveLoadOptions::maximum_record_bytes)
        .def_readwrite("cancellation", &TransparencyGossipArchiveLoadOptions::cancellation);

    py::class_<TransparencyGossipArchive>(module, "TransparencyGossipArchive")
        .def_static(
            "create",
            [](const std::filesystem::path& directory, TransparencyLogIdentity identity,
               const ServiceTrustBundle& trust_bundle) {
                return unwrap(
                    TransparencyGossipArchive::create(directory, std::move(identity), trust_bundle));
            },
            py::arg("directory"), py::arg("log_identity"), py::arg("trust_bundle"))
        .def_static(
            "open",
            [](const std::filesystem::path& directory, const TransparencyLogIdentity& expected_identity,
               const ServiceTrustBundle& trust_bundle, const std::string& expected_trust_bundle_id,
               const std::string& expected_head_record_id,
               const TransparencyGossipArchiveLoadOptions& options) {
                return unwrap(TransparencyGossipArchive::open(directory, expected_identity, trust_bundle,
                                                              expected_trust_bundle_id,
                                                              expected_head_record_id, options));
            },
            py::arg("directory"), py::arg("expected_log_identity"), py::arg("trust_bundle"),
            py::arg("expected_trust_bundle_id"), py::arg("expected_head_record_id"),
            py::arg("options") = TransparencyGossipArchiveLoadOptions{})
        .def_property_readonly("directory", &TransparencyGossipArchive::directory)
        .def_property_readonly("log_identity", &TransparencyGossipArchive::log_identity)
        .def_property_readonly("trust_bundle_id", &TransparencyGossipArchive::trust_bundle_id)
        .def_property_readonly("trust_bundle_sequence", &TransparencyGossipArchive::trust_bundle_sequence)
        .def_property_readonly("records", &TransparencyGossipArchive::records)
        .def_property_readonly("current_record_id", &TransparencyGossipArchive::current_record_id)
        .def("valid", &TransparencyGossipArchive::valid)
        .def_property_readonly("evidence", &TransparencyGossipArchive::evidence)
        .def_property_readonly("authorizes_execution", &TransparencyGossipArchive::authorizes_execution)
        .def(
            "publish",
            [](TransparencyGossipArchive& archive, TransparencyCheckpointGossip gossip,
               const std::string& expected_head_record_id) {
                return unwrap(archive.publish(std::move(gossip), expected_head_record_id));
            },
            py::arg("gossip"), py::arg("expected_head_record_id"))
        .def("audit", [](const TransparencyGossipArchive& archive) { return unwrap(archive.audit()); });

    module.def("valid_transparency_checkpoint_witness_policy", &valid_transparency_checkpoint_witness_policy);
    module.def(
        "sign_transparency_checkpoint_witness",
        [](const TransparencyLogIdentity& identity, const TransparencyLogCheckpoint& checkpoint,
           const ServiceTrustBundle& trust_bundle, std::string witness_service_id, std::string witness_key_id,
           const py::bytes& secret_key) {
            const WitnessSensitiveBytes copy(secret_key);
            return unwrap(rbfsafe::sign_transparency_checkpoint_witness(
                identity, checkpoint, trust_bundle, std::move(witness_service_id), std::move(witness_key_id),
                copy.view()));
        },
        py::arg("identity"), py::arg("checkpoint"), py::arg("trust_bundle"), py::arg("witness_service_id"),
        py::arg("witness_key_id"), py::arg("ed25519_secret_key"));
    module.def(
        "verify_transparency_checkpoint_witness",
        [](const TransparencyLogIdentity& identity, const TransparencyLogCheckpoint& checkpoint,
           const TransparencyCheckpointCosignature& cosignature, const ServiceTrustBundle& trust_bundle) {
            unwrap_void(rbfsafe::verify_transparency_checkpoint_witness(identity, checkpoint, cosignature,
                                                                        trust_bundle));
        },
        py::arg("identity"), py::arg("checkpoint"), py::arg("cosignature"), py::arg("trust_bundle"));
    module.def(
        "assemble_witnessed_transparency_checkpoint",
        [](const TransparencyLogIdentity& identity, TransparencyLogCheckpoint checkpoint,
           TransparencyCheckpointWitnessPolicy policy,
           std::vector<TransparencyCheckpointCosignature> cosignatures,
           const ServiceTrustBundle& trust_bundle) {
            return unwrap(rbfsafe::assemble_witnessed_transparency_checkpoint(
                identity, std::move(checkpoint), policy, std::move(cosignatures), trust_bundle));
        },
        py::arg("identity"), py::arg("checkpoint"), py::arg("policy"), py::arg("cosignatures"),
        py::arg("trust_bundle"));
    module.def(
        "verify_witnessed_transparency_checkpoint",
        [](const TransparencyLogIdentity& identity, const WitnessedTransparencyCheckpoint& checkpoint,
           const ServiceTrustBundle& trust_bundle) {
            unwrap_void(
                rbfsafe::verify_witnessed_transparency_checkpoint(identity, checkpoint, trust_bundle));
        },
        py::arg("identity"), py::arg("witnessed_checkpoint"), py::arg("trust_bundle"));
    module.def(
        "sign_transparency_checkpoint_gossip",
        [](const TransparencyLogIdentity& identity, WitnessedTransparencyCheckpoint witnessed_checkpoint,
           std::optional<TransparencyCompactConsistencyProof> consistency_proof,
           std::string recipient_service_id, std::uint64_t sender_sequence, std::string parent_gossip_id,
           const ServiceTrustBundle& trust_bundle, std::string sender_service_id, std::string sender_key_id,
           const py::bytes& secret_key) {
            const WitnessSensitiveBytes copy(secret_key);
            return unwrap(rbfsafe::sign_transparency_checkpoint_gossip(
                identity, std::move(witnessed_checkpoint), std::move(consistency_proof),
                std::move(recipient_service_id), sender_sequence, std::move(parent_gossip_id), trust_bundle,
                std::move(sender_service_id), std::move(sender_key_id), copy.view()));
        },
        py::arg("identity"), py::arg("witnessed_checkpoint"), py::arg("consistency_proof"),
        py::arg("recipient_service_id"), py::arg("sender_sequence"), py::arg("parent_gossip_id"),
        py::arg("trust_bundle"), py::arg("sender_service_id"), py::arg("sender_key_id"),
        py::arg("ed25519_secret_key"));
    module.def(
        "verify_transparency_checkpoint_gossip",
        [](const TransparencyLogIdentity& identity, const TransparencyCheckpointGossip& gossip,
           const ServiceTrustBundle& trust_bundle) {
            unwrap_void(rbfsafe::verify_transparency_checkpoint_gossip(identity, gossip, trust_bundle));
        },
        py::arg("identity"), py::arg("gossip"), py::arg("trust_bundle"));
    module.def(
        "audit_transparency_checkpoint_gossip",
        [](const TransparencyLogIdentity& identity, const std::vector<TransparencyCheckpointGossip>& gossip,
           const ServiceTrustBundle& trust_bundle, const TransparencyGossipAuditOptions& options) {
            return unwrap(
                rbfsafe::audit_transparency_checkpoint_gossip(identity, gossip, trust_bundle, options));
        },
        py::arg("identity"), py::arg("gossip"), py::arg("trust_bundle"),
        py::arg("options") = TransparencyGossipAuditOptions{});
    module.def("transparency_gossip_conflict_type_name", &transparency_gossip_conflict_type_name);
    module.def("transparency_gossip_status_name", &transparency_gossip_status_name);
}

} // namespace rbfsafe::python_binding
