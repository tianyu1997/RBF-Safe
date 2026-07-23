#include "binding_support.h"

#include <rbfsafe/trust.h>

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

void bind_trust(py::module_& module) {
    py::enum_<ArtifactAuthenticationAlgorithm>(module, "ArtifactAuthenticationAlgorithm")
        .value("HMAC_SHA256", ArtifactAuthenticationAlgorithm::HmacSha256);

    py::class_<ArtifactAttestation>(module, "ArtifactAttestation")
        .def_readonly("sequence", &ArtifactAttestation::sequence)
        .def_readonly("id", &ArtifactAttestation::id)
        .def_readonly("service_id", &ArtifactAttestation::service_id)
        .def_readonly("key_id", &ArtifactAttestation::key_id)
        .def_readonly("algorithm", &ArtifactAttestation::algorithm)
        .def_readonly("artifact_id", &ArtifactAttestation::artifact_id)
        .def_readonly("artifact_generation", &ArtifactAttestation::artifact_generation)
        .def_readonly("artifact_state", &ArtifactAttestation::artifact_state)
        .def_readonly("artifact_content_digest", &ArtifactAttestation::artifact_content_digest)
        .def_readonly("payload_digest", &ArtifactAttestation::payload_digest)
        .def_readonly("payload_bytes", &ArtifactAttestation::payload_bytes)
        .def_readonly("media_type", &ArtifactAttestation::media_type)
        .def_readonly("authentication_tag", &ArtifactAttestation::authentication_tag);

    py::class_<ArtifactVerificationOptions>(module, "ArtifactVerificationOptions")
        .def(py::init<>())
        .def_readwrite("maximum_payload_bytes", &ArtifactVerificationOptions::maximum_payload_bytes)
        .def_readwrite("cancellation", &ArtifactVerificationOptions::cancellation);

    module.def("valid_artifact_attestation", &valid_artifact_attestation);
    module.def(
        "attest_artifact",
        [](const MemoryArtifact& artifact, const py::bytes& payload, std::string service_id,
           std::string key_id, const py::bytes& hmac_key, std::uint64_t sequence, std::string media_type) {
            const auto payload_copy = static_cast<std::string>(payload);
            const auto key_copy = static_cast<std::string>(hmac_key);
            return unwrap(rbfsafe::attest_artifact(artifact, bytes_view(payload_copy), std::move(service_id),
                                                   std::move(key_id), bytes_view(key_copy), sequence,
                                                   std::move(media_type)));
        },
        py::arg("artifact"), py::arg("payload"), py::arg("service_id"), py::arg("key_id"),
        py::arg("hmac_key"), py::arg("sequence"), py::arg("media_type") = "application/octet-stream");
    module.def(
        "attest_artifact_file",
        [](const MemoryArtifact& artifact, const std::filesystem::path& payload_path, std::string service_id,
           std::string key_id, const py::bytes& hmac_key, std::uint64_t sequence, std::string media_type,
           const ArtifactVerificationOptions& options) {
            const auto key_copy = static_cast<std::string>(hmac_key);
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::attest_artifact_file(artifact, payload_path, std::move(service_id),
                                                     std::move(key_id), bytes_view(key_copy), sequence,
                                                     std::move(media_type), options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("artifact"), py::arg("payload_path"), py::arg("service_id"), py::arg("key_id"),
        py::arg("hmac_key"), py::arg("sequence"), py::arg("media_type") = "application/octet-stream",
        py::arg("options") = ArtifactVerificationOptions{});
    module.def(
        "verify_artifact",
        [](const MemoryArtifact& artifact, const py::bytes& payload, const ArtifactAttestation& attestation,
           std::string expected_service_id, std::string expected_key_id, const py::bytes& hmac_key) {
            const auto payload_copy = static_cast<std::string>(payload);
            const auto key_copy = static_cast<std::string>(hmac_key);
            unwrap_void(rbfsafe::verify_artifact(artifact, bytes_view(payload_copy), attestation,
                                                 expected_service_id, expected_key_id, bytes_view(key_copy)));
        },
        py::arg("artifact"), py::arg("payload"), py::arg("attestation"), py::arg("expected_service_id"),
        py::arg("expected_key_id"), py::arg("hmac_key"));
    module.def(
        "verify_artifact_file",
        [](const MemoryArtifact& artifact, const std::filesystem::path& payload_path,
           const ArtifactAttestation& attestation, std::string expected_service_id,
           std::string expected_key_id, const py::bytes& hmac_key,
           const ArtifactVerificationOptions& options) {
            const auto key_copy = static_cast<std::string>(hmac_key);
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::verify_artifact_file(artifact, payload_path, attestation, expected_service_id,
                                                     expected_key_id, bytes_view(key_copy), options);
            }();
            unwrap_void(std::move(result));
        },
        py::arg("artifact"), py::arg("payload_path"), py::arg("attestation"), py::arg("expected_service_id"),
        py::arg("expected_key_id"), py::arg("hmac_key"), py::arg("options") = ArtifactVerificationOptions{});
    module.def(
        "save_artifact_attestation",
        [](const ArtifactAttestation& attestation, const std::filesystem::path& path,
           const SaveOptions& options) {
            unwrap_void(rbfsafe::save_artifact_attestation(attestation, path, options));
        },
        py::arg("attestation"), py::arg("path"), py::arg("options") = SaveOptions{});
    module.def(
        "load_artifact_attestation",
        [](const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
            return unwrap(rbfsafe::load_artifact_attestation(path, maximum_bytes));
        },
        py::arg("path"), py::arg("maximum_bytes") = 65'536ULL);
    module.def("artifact_authentication_algorithm_name", &artifact_authentication_algorithm_name);
}

} // namespace rbfsafe::python_binding
