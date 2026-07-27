#include "binding_support.h"

#include <rbfsafe/identity.h>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <array>
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

void bind_identity(py::module_& module) {
    py::enum_<ServiceKeyState>(module, "ServiceKeyState")
        .value("PENDING", ServiceKeyState::Pending)
        .value("ACTIVE", ServiceKeyState::Active)
        .value("RETIRED", ServiceKeyState::Retired)
        .value("REVOKED", ServiceKeyState::Revoked);

    py::class_<Ed25519KeyPair>(module, "Ed25519KeyPair")
        .def_property_readonly("public_key",
                               [](const Ed25519KeyPair& pair) { return array_bytes(pair.public_key); })
        .def_property_readonly("secret_key",
                               [](const Ed25519KeyPair& pair) { return array_bytes(pair.secret_key); });

    py::class_<ServicePublicKey>(module, "ServicePublicKey")
        .def_readonly("id", &ServicePublicKey::id)
        .def_readonly("service_id", &ServicePublicKey::service_id)
        .def_readonly("algorithm", &ServicePublicKey::algorithm)
        .def_property_readonly("public_key",
                               [](const ServicePublicKey& key) { return array_bytes(key.public_key); })
        .def_readonly("valid_from_sequence", &ServicePublicKey::valid_from_sequence)
        .def_readonly("valid_through_sequence", &ServicePublicKey::valid_through_sequence)
        .def_readonly("state", &ServicePublicKey::state)
        .def_readonly("allow_fetch", &ServicePublicKey::allow_fetch)
        .def_readonly("allow_publish", &ServicePublicKey::allow_publish);

    py::class_<ServiceTrustBundleLoadOptions>(module, "ServiceTrustBundleLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_keys", &ServiceTrustBundleLoadOptions::maximum_keys)
        .def_readwrite("maximum_payload_bytes", &ServiceTrustBundleLoadOptions::maximum_payload_bytes);

    py::class_<ServiceTrustBundle>(module, "ServiceTrustBundle")
        .def_static(
            "create",
            [](std::uint64_t sequence, std::string parent_id, std::vector<ServicePublicKey> keys) {
                return unwrap(ServiceTrustBundle::create(sequence, std::move(parent_id), std::move(keys)));
            },
            py::arg("sequence"), py::arg("parent_id"), py::arg("keys"))
        .def_property_readonly("sequence", &ServiceTrustBundle::sequence)
        .def_property_readonly("id", &ServiceTrustBundle::id)
        .def_property_readonly("parent_id", &ServiceTrustBundle::parent_id)
        .def_property_readonly("keys", &ServiceTrustBundle::keys)
        .def("valid", &ServiceTrustBundle::valid)
        .def(
            "key",
            [](const ServiceTrustBundle& bundle, const std::string& service_id, const std::string& key_id) {
                return unwrap(bundle.key(service_id, key_id));
            },
            py::arg("service_id"), py::arg("key_id"))
        .def(
            "save",
            [](const ServiceTrustBundle& bundle, const std::filesystem::path& path,
               const SaveOptions& options) { unwrap_void(bundle.save(path, options)); },
            py::arg("path"), py::arg("options") = SaveOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& path, const ServiceTrustBundleLoadOptions& options) {
                return unwrap(ServiceTrustBundle::load(path, options));
            },
            py::arg("path"), py::arg("options") = ServiceTrustBundleLoadOptions{});

    module.def(
        "ed25519_key_pair_from_seed",
        [](const py::bytes& seed) {
            const SensitiveBytes seed_copy(seed);
            return unwrap(rbfsafe::ed25519_key_pair_from_seed(seed_copy.view()));
        },
        py::arg("seed"));

    module.def(
        "ed25519_sign",
        [](const py::bytes& message, const py::bytes& secret_key) {
            const auto message_copy = static_cast<std::string>(message);
            const SensitiveBytes secret_copy(secret_key);
            return array_bytes(unwrap(rbfsafe::ed25519_sign(bytes_view(message_copy), secret_copy.view())));
        },
        py::arg("message"), py::arg("secret_key"));

    module.def(
        "ed25519_verify",
        [](const py::bytes& message, const py::bytes& signature, const py::bytes& public_key) {
            const auto message_copy = static_cast<std::string>(message);
            const auto signature_copy = static_cast<std::string>(signature);
            const auto public_copy = static_cast<std::string>(public_key);
            unwrap_void(rbfsafe::ed25519_verify(bytes_view(message_copy), bytes_view(signature_copy),
                                                bytes_view(public_copy)));
        },
        py::arg("message"), py::arg("signature"), py::arg("public_key"));

    module.def(
        "make_service_public_key",
        [](std::string service_id, const py::bytes& public_key, std::uint64_t valid_from_sequence,
           std::uint64_t valid_through_sequence, ServiceKeyState state, bool allow_fetch,
           bool allow_publish) {
            const auto public_copy = static_cast<std::string>(public_key);
            return unwrap(rbfsafe::make_service_public_key(std::move(service_id), bytes_view(public_copy),
                                                           valid_from_sequence, valid_through_sequence, state,
                                                           allow_fetch, allow_publish));
        },
        py::arg("service_id"), py::arg("public_key"), py::arg("valid_from_sequence") = 1,
        py::arg("valid_through_sequence") = 0, py::arg("state") = ServiceKeyState::Pending,
        py::arg("allow_fetch") = true, py::arg("allow_publish") = true);

    module.def("valid_service_public_key", &valid_service_public_key);

    module.def(
        "rotate_service_trust_bundle",
        [](const ServiceTrustBundle& previous, std::vector<ServicePublicKey> keys) {
            return unwrap(rbfsafe::rotate_service_trust_bundle(previous, std::move(keys)));
        },
        py::arg("previous"), py::arg("keys"));

    module.def(
        "trusted_service_public_key",
        [](const ServiceTrustBundle& bundle, const std::string& service_id, const std::string& key_id,
           ArtifactTransferOperation operation, std::uint64_t service_sequence) {
            return unwrap(
                rbfsafe::trusted_service_public_key(bundle, service_id, key_id, operation, service_sequence));
        },
        py::arg("bundle"), py::arg("service_id"), py::arg("key_id"), py::arg("operation"),
        py::arg("service_sequence"));

    module.def(
        "sign_artifact_fetch_response",
        [](ArtifactFetchResponse response, std::string key_id, const py::bytes& secret_key) {
            const SensitiveBytes secret_copy(secret_key);
            return unwrap(rbfsafe::sign_artifact_fetch_response(std::move(response), std::move(key_id),
                                                                secret_copy.view()));
        },
        py::arg("response"), py::arg("key_id"), py::arg("ed25519_secret_key"));

    module.def(
        "sign_artifact_publish_receipt",
        [](ArtifactPublishReceipt receipt, std::string key_id, const py::bytes& secret_key) {
            const SensitiveBytes secret_copy(secret_key);
            return unwrap(rbfsafe::sign_artifact_publish_receipt(std::move(receipt), std::move(key_id),
                                                                 secret_copy.view()));
        },
        py::arg("receipt"), py::arg("key_id"), py::arg("ed25519_secret_key"));

    module.def(
        "verify_artifact_fetch_offline",
        [](const SafetyMemory& memory, const ArtifactFetchRequest& request,
           const ArtifactFetchResponse& response, const py::bytes& payload,
           const ServiceTrustBundle& trust_bundle, const RemoteArtifactOptions& options) {
            const auto payload_copy = static_cast<std::string>(payload);
            return unwrap(rbfsafe::verify_artifact_fetch_offline(
                memory, request, response, bytes_view(payload_copy), trust_bundle, options));
        },
        py::arg("memory"), py::arg("request"), py::arg("response"), py::arg("payload"),
        py::arg("trust_bundle"), py::arg("options") = RemoteArtifactOptions{});

    module.def(
        "verify_artifact_publish_offline",
        [](const SafetyMemory& memory, const ArtifactPublishRequest& request,
           const ArtifactPublishReceipt& receipt, const py::bytes& payload,
           const ServiceTrustBundle& trust_bundle, const RemoteArtifactOptions& options) {
            const auto payload_copy = static_cast<std::string>(payload);
            return unwrap(rbfsafe::verify_artifact_publish_offline(
                memory, request, receipt, bytes_view(payload_copy), trust_bundle, options));
        },
        py::arg("memory"), py::arg("request"), py::arg("receipt"), py::arg("payload"),
        py::arg("trust_bundle"), py::arg("options") = RemoteArtifactOptions{});

    module.def("service_key_state_name", &service_key_state_name);
}

} // namespace rbfsafe::python_binding
