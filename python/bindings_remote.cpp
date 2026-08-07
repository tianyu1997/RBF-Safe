#include "binding_support.h"

#include <rbfsafe/modules/assurance.h>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <string>
#include <utility>

namespace rbfsafe::python_binding {
namespace {

std::span<const std::byte> bytes_view(const std::string& value) {
    return std::as_bytes(std::span(value.data(), value.size()));
}

} // namespace

void bind_remote(py::module_& module) {
    py::enum_<ArtifactTransferOperation>(module, "ArtifactTransferOperation")
        .value("FETCH", ArtifactTransferOperation::Fetch)
        .value("PUBLISH", ArtifactTransferOperation::Publish);

    py::enum_<ArtifactTransferAuthentication>(module, "ArtifactTransferAuthentication")
        .value("NONE", ArtifactTransferAuthentication::None)
        .value("HMAC_SHA256", ArtifactTransferAuthentication::HmacSha256)
        .value("ED25519", ArtifactTransferAuthentication::Ed25519);

    py::class_<ArtifactTransferAttestation>(module, "ArtifactTransferAttestation")
        .def_readonly("id", &ArtifactTransferAttestation::id)
        .def_readonly("service_id", &ArtifactTransferAttestation::service_id)
        .def_readonly("key_id", &ArtifactTransferAttestation::key_id)
        .def_readonly("algorithm", &ArtifactTransferAttestation::algorithm)
        .def_readonly("operation", &ArtifactTransferAttestation::operation)
        .def_readonly("request_id", &ArtifactTransferAttestation::request_id)
        .def_readonly("response_id", &ArtifactTransferAttestation::response_id)
        .def_readonly("artifact_id", &ArtifactTransferAttestation::artifact_id)
        .def_readonly("payload_digest", &ArtifactTransferAttestation::payload_digest)
        .def_readonly("payload_bytes", &ArtifactTransferAttestation::payload_bytes)
        .def_readonly("service_sequence", &ArtifactTransferAttestation::service_sequence)
        .def_readonly("authentication_tag", &ArtifactTransferAttestation::authentication_tag);

    py::class_<ArtifactFetchRequest>(module, "ArtifactFetchRequest")
        .def_readonly("sequence", &ArtifactFetchRequest::sequence)
        .def_readonly("id", &ArtifactFetchRequest::id)
        .def_readonly("service_id", &ArtifactFetchRequest::service_id)
        .def_readonly("memory_id", &ArtifactFetchRequest::memory_id)
        .def_readonly("artifact_id", &ArtifactFetchRequest::artifact_id)
        .def_readonly("artifact_generation", &ArtifactFetchRequest::artifact_generation)
        .def_readonly("artifact_state", &ArtifactFetchRequest::artifact_state)
        .def_readonly("artifact_content_digest", &ArtifactFetchRequest::artifact_content_digest)
        .def_readonly("locator", &ArtifactFetchRequest::locator)
        .def_readonly("media_type", &ArtifactFetchRequest::media_type)
        .def_readonly("maximum_payload_bytes", &ArtifactFetchRequest::maximum_payload_bytes)
        .def_readonly("response_authentication", &ArtifactFetchRequest::response_authentication);

    py::class_<ArtifactFetchResponse>(module, "ArtifactFetchResponse")
        .def_readonly("id", &ArtifactFetchResponse::id)
        .def_readonly("request_id", &ArtifactFetchResponse::request_id)
        .def_readonly("service_id", &ArtifactFetchResponse::service_id)
        .def_readonly("artifact_id", &ArtifactFetchResponse::artifact_id)
        .def_readonly("artifact_generation", &ArtifactFetchResponse::artifact_generation)
        .def_readonly("artifact_state", &ArtifactFetchResponse::artifact_state)
        .def_readonly("artifact_content_digest", &ArtifactFetchResponse::artifact_content_digest)
        .def_readonly("payload_digest", &ArtifactFetchResponse::payload_digest)
        .def_readonly("payload_bytes", &ArtifactFetchResponse::payload_bytes)
        .def_readonly("media_type", &ArtifactFetchResponse::media_type)
        .def_readonly("service_sequence", &ArtifactFetchResponse::service_sequence)
        .def_readonly("service_attestation", &ArtifactFetchResponse::service_attestation);

    py::class_<ArtifactPublishRequest>(module, "ArtifactPublishRequest")
        .def_readonly("sequence", &ArtifactPublishRequest::sequence)
        .def_readonly("id", &ArtifactPublishRequest::id)
        .def_readonly("service_id", &ArtifactPublishRequest::service_id)
        .def_readonly("memory_id", &ArtifactPublishRequest::memory_id)
        .def_readonly("artifact_id", &ArtifactPublishRequest::artifact_id)
        .def_readonly("artifact_generation", &ArtifactPublishRequest::artifact_generation)
        .def_readonly("artifact_state", &ArtifactPublishRequest::artifact_state)
        .def_readonly("artifact_content_digest", &ArtifactPublishRequest::artifact_content_digest)
        .def_readonly("locator", &ArtifactPublishRequest::locator)
        .def_readonly("payload_digest", &ArtifactPublishRequest::payload_digest)
        .def_readonly("payload_bytes", &ArtifactPublishRequest::payload_bytes)
        .def_readonly("media_type", &ArtifactPublishRequest::media_type)
        .def_readonly("receipt_authentication", &ArtifactPublishRequest::receipt_authentication);

    py::class_<ArtifactPublishReceipt>(module, "ArtifactPublishReceipt")
        .def_readonly("id", &ArtifactPublishReceipt::id)
        .def_readonly("request_id", &ArtifactPublishReceipt::request_id)
        .def_readonly("service_id", &ArtifactPublishReceipt::service_id)
        .def_readonly("artifact_id", &ArtifactPublishReceipt::artifact_id)
        .def_readonly("artifact_generation", &ArtifactPublishReceipt::artifact_generation)
        .def_readonly("artifact_state", &ArtifactPublishReceipt::artifact_state)
        .def_readonly("artifact_content_digest", &ArtifactPublishReceipt::artifact_content_digest)
        .def_readonly("payload_digest", &ArtifactPublishReceipt::payload_digest)
        .def_readonly("payload_bytes", &ArtifactPublishReceipt::payload_bytes)
        .def_readonly("media_type", &ArtifactPublishReceipt::media_type)
        .def_readonly("service_sequence", &ArtifactPublishReceipt::service_sequence)
        .def_readonly("service_attestation", &ArtifactPublishReceipt::service_attestation);

    py::class_<VerifiedArtifactTransfer>(module, "VerifiedArtifactTransfer")
        .def_readonly("id", &VerifiedArtifactTransfer::id)
        .def_readonly("operation", &VerifiedArtifactTransfer::operation)
        .def_readonly("request_id", &VerifiedArtifactTransfer::request_id)
        .def_readonly("response_id", &VerifiedArtifactTransfer::response_id)
        .def_readonly("service_id", &VerifiedArtifactTransfer::service_id)
        .def_readonly("memory_id", &VerifiedArtifactTransfer::memory_id)
        .def_readonly("artifact_id", &VerifiedArtifactTransfer::artifact_id)
        .def_readonly("artifact_generation", &VerifiedArtifactTransfer::artifact_generation)
        .def_readonly("artifact_state", &VerifiedArtifactTransfer::artifact_state)
        .def_readonly("artifact_content_digest", &VerifiedArtifactTransfer::artifact_content_digest)
        .def_readonly("payload_digest", &VerifiedArtifactTransfer::payload_digest)
        .def_readonly("payload_bytes", &VerifiedArtifactTransfer::payload_bytes)
        .def_readonly("media_type", &VerifiedArtifactTransfer::media_type)
        .def_readonly("service_sequence", &VerifiedArtifactTransfer::service_sequence)
        .def_readonly("authentication", &VerifiedArtifactTransfer::authentication)
        .def_readonly("attestation_id", &VerifiedArtifactTransfer::attestation_id)
        .def_readonly("verification_key_id", &VerifiedArtifactTransfer::verification_key_id)
        .def_readonly("trust_bundle_id", &VerifiedArtifactTransfer::trust_bundle_id);

    py::class_<RemoteArtifactOptions>(module, "RemoteArtifactOptions")
        .def(py::init<>())
        .def_readwrite("maximum_payload_bytes", &RemoteArtifactOptions::maximum_payload_bytes)
        .def_readwrite("require_active_artifact", &RemoteArtifactOptions::require_active_artifact)
        .def_readwrite("cancellation", &RemoteArtifactOptions::cancellation);

    py::class_<ArtifactTransferRecord>(module, "ArtifactTransferRecord")
        .def_readonly("sequence", &ArtifactTransferRecord::sequence)
        .def_readonly("id", &ArtifactTransferRecord::id)
        .def_readonly("parent_id", &ArtifactTransferRecord::parent_id)
        .def_readonly("transfer", &ArtifactTransferRecord::transfer);

    py::class_<ArtifactTransferJournalLoadOptions>(module, "ArtifactTransferJournalLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_records", &ArtifactTransferJournalLoadOptions::maximum_records)
        .def_readwrite("maximum_payload_bytes", &ArtifactTransferJournalLoadOptions::maximum_payload_bytes);

    py::class_<ArtifactTransferJournal>(module, "ArtifactTransferJournal")
        .def(py::init<>())
        .def_property_readonly("records", &ArtifactTransferJournal::records)
        .def_property_readonly("current_record_id", &ArtifactTransferJournal::current_record_id)
        .def_property_readonly("identity", &ArtifactTransferJournal::identity)
        .def("valid", &ArtifactTransferJournal::valid)
        .def(
            "append",
            [](ArtifactTransferJournal& journal, VerifiedArtifactTransfer transfer,
               const std::string& expected_current_record_id, std::size_t maximum_records) {
                return unwrap(
                    journal.append(std::move(transfer), expected_current_record_id, maximum_records));
            },
            py::arg("transfer"), py::arg("expected_current_record_id"),
            py::arg("maximum_records") = 1'000'000)
        .def(
            "save",
            [](const ArtifactTransferJournal& journal, const std::filesystem::path& directory,
               const SaveOptions& options) { unwrap_void(journal.save(directory, options)); },
            py::arg("directory"), py::arg("options") = SaveOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& directory, const ArtifactTransferJournalLoadOptions& options) {
                return unwrap(ArtifactTransferJournal::load(directory, options));
            },
            py::arg("directory"), py::arg("options") = ArtifactTransferJournalLoadOptions{});

    module.def("valid_artifact_fetch_request", &valid_artifact_fetch_request);
    module.def("valid_artifact_fetch_response", &valid_artifact_fetch_response);
    module.def("valid_artifact_publish_request", &valid_artifact_publish_request);
    module.def("valid_artifact_publish_receipt", &valid_artifact_publish_receipt);
    module.def("valid_artifact_transfer_attestation", &valid_artifact_transfer_attestation);
    module.def("valid_verified_artifact_transfer", &valid_verified_artifact_transfer);

    module.def(
        "prepare_artifact_fetch",
        [](const SafetyMemory& memory, const std::string& artifact_id, std::string service_id,
           std::uint64_t sequence, std::string media_type,
           ArtifactTransferAuthentication response_authentication, const RemoteArtifactOptions& options) {
            return unwrap(rbfsafe::prepare_artifact_fetch(memory, artifact_id, std::move(service_id),
                                                          sequence, std::move(media_type),
                                                          response_authentication, options));
        },
        py::arg("memory"), py::arg("artifact_id"), py::arg("service_id"), py::arg("sequence"),
        py::arg("media_type") = "application/octet-stream",
        py::arg("response_authentication") = ArtifactTransferAuthentication::HmacSha256,
        py::arg("options") = RemoteArtifactOptions{});

    module.def(
        "make_artifact_fetch_response",
        [](const ArtifactFetchRequest& request, const py::bytes& payload, std::uint64_t service_sequence) {
            const auto payload_copy = static_cast<std::string>(payload);
            return unwrap(
                rbfsafe::make_artifact_fetch_response(request, bytes_view(payload_copy), service_sequence));
        },
        py::arg("request"), py::arg("payload"), py::arg("service_sequence"));

    module.def(
        "authenticate_artifact_fetch_response",
        [](ArtifactFetchResponse response, std::string key_id, const py::bytes& hmac_key) {
            const auto key_copy = static_cast<std::string>(hmac_key);
            return unwrap(rbfsafe::authenticate_artifact_fetch_response(
                std::move(response), std::move(key_id), bytes_view(key_copy)));
        },
        py::arg("response"), py::arg("key_id"), py::arg("hmac_key"));

    module.def(
        "verify_artifact_fetch",
        [](const SafetyMemory& memory, const ArtifactFetchRequest& request,
           const ArtifactFetchResponse& response, const py::bytes& payload, std::string expected_key_id,
           const py::bytes& hmac_key, const RemoteArtifactOptions& options) {
            const auto payload_copy = static_cast<std::string>(payload);
            const auto key_copy = static_cast<std::string>(hmac_key);
            return unwrap(rbfsafe::verify_artifact_fetch(memory, request, response, bytes_view(payload_copy),
                                                         expected_key_id, bytes_view(key_copy), options));
        },
        py::arg("memory"), py::arg("request"), py::arg("response"), py::arg("payload"),
        py::arg("expected_key_id") = "", py::arg("hmac_key") = py::bytes(),
        py::arg("options") = RemoteArtifactOptions{});

    module.def(
        "prepare_artifact_publish",
        [](const SafetyMemory& memory, const std::string& artifact_id, const py::bytes& payload,
           std::string service_id, std::uint64_t sequence, std::string media_type,
           ArtifactTransferAuthentication receipt_authentication, const RemoteArtifactOptions& options) {
            const auto payload_copy = static_cast<std::string>(payload);
            return unwrap(rbfsafe::prepare_artifact_publish(
                memory, artifact_id, bytes_view(payload_copy), std::move(service_id), sequence,
                std::move(media_type), receipt_authentication, options));
        },
        py::arg("memory"), py::arg("artifact_id"), py::arg("payload"), py::arg("service_id"),
        py::arg("sequence"), py::arg("media_type") = "application/octet-stream",
        py::arg("receipt_authentication") = ArtifactTransferAuthentication::HmacSha256,
        py::arg("options") = RemoteArtifactOptions{});

    module.def(
        "make_artifact_publish_receipt",
        [](const ArtifactPublishRequest& request, std::uint64_t service_sequence) {
            return unwrap(rbfsafe::make_artifact_publish_receipt(request, service_sequence));
        },
        py::arg("request"), py::arg("service_sequence"));

    module.def(
        "authenticate_artifact_publish_receipt",
        [](ArtifactPublishReceipt receipt, std::string key_id, const py::bytes& hmac_key) {
            const auto key_copy = static_cast<std::string>(hmac_key);
            return unwrap(rbfsafe::authenticate_artifact_publish_receipt(
                std::move(receipt), std::move(key_id), bytes_view(key_copy)));
        },
        py::arg("receipt"), py::arg("key_id"), py::arg("hmac_key"));

    module.def(
        "verify_artifact_publish",
        [](const SafetyMemory& memory, const ArtifactPublishRequest& request,
           const ArtifactPublishReceipt& receipt, const py::bytes& payload, std::string expected_key_id,
           const py::bytes& hmac_key, const RemoteArtifactOptions& options) {
            const auto payload_copy = static_cast<std::string>(payload);
            const auto key_copy = static_cast<std::string>(hmac_key);
            return unwrap(rbfsafe::verify_artifact_publish(memory, request, receipt, bytes_view(payload_copy),
                                                           expected_key_id, bytes_view(key_copy), options));
        },
        py::arg("memory"), py::arg("request"), py::arg("receipt"), py::arg("payload"),
        py::arg("expected_key_id") = "", py::arg("hmac_key") = py::bytes(),
        py::arg("options") = RemoteArtifactOptions{});

    module.def("artifact_transfer_operation_name", &artifact_transfer_operation_name);
    module.def("artifact_transfer_authentication_name", &artifact_transfer_authentication_name);
}

} // namespace rbfsafe::python_binding
