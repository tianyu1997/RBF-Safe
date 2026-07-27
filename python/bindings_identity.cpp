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
        .def_readonly("allow_publish", &ServicePublicKey::allow_publish)
        .def_readonly("allow_rotate", &ServicePublicKey::allow_rotate);

    py::class_<ServiceTrustBundleLoadOptions>(module, "ServiceTrustBundleLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_keys", &ServiceTrustBundleLoadOptions::maximum_keys)
        .def_readwrite("maximum_payload_bytes", &ServiceTrustBundleLoadOptions::maximum_payload_bytes);

    py::class_<ServiceTrustRotationPolicy>(module, "ServiceTrustRotationPolicy")
        .def(py::init<>())
        .def_readwrite("minimum_signatures", &ServiceTrustRotationPolicy::minimum_signatures)
        .def_readwrite("require_distinct_services", &ServiceTrustRotationPolicy::require_distinct_services);

    py::class_<ServiceTrustBundle>(module, "ServiceTrustBundle")
        .def_static(
            "create",
            [](std::uint64_t sequence, std::string parent_id, std::vector<ServicePublicKey> keys) {
                return unwrap(ServiceTrustBundle::create(sequence, std::move(parent_id), std::move(keys)));
            },
            py::arg("sequence"), py::arg("parent_id"), py::arg("keys"))
        .def_static(
            "create_with_rotation_policy",
            [](std::uint64_t sequence, std::string parent_id, std::vector<ServicePublicKey> keys,
               const ServiceTrustRotationPolicy& rotation_policy) {
                return unwrap(ServiceTrustBundle::create_with_rotation_policy(
                    sequence, std::move(parent_id), std::move(keys), rotation_policy));
            },
            py::arg("sequence"), py::arg("parent_id"), py::arg("keys"), py::arg("rotation_policy"))
        .def_property_readonly("storage_schema", &ServiceTrustBundle::storage_schema)
        .def_property_readonly("sequence", &ServiceTrustBundle::sequence)
        .def_property_readonly("id", &ServiceTrustBundle::id)
        .def_property_readonly("parent_id", &ServiceTrustBundle::parent_id)
        .def_property_readonly("keys", &ServiceTrustBundle::keys)
        .def_property_readonly("rotation_policy", &ServiceTrustBundle::rotation_policy)
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

    py::class_<ServiceTrustBundleAuthorization>(module, "ServiceTrustBundleAuthorization")
        .def_readonly("id", &ServiceTrustBundleAuthorization::id)
        .def_readonly("predecessor_bundle_id", &ServiceTrustBundleAuthorization::predecessor_bundle_id)
        .def_readonly("successor_bundle_id", &ServiceTrustBundleAuthorization::successor_bundle_id)
        .def_readonly("predecessor_sequence", &ServiceTrustBundleAuthorization::predecessor_sequence)
        .def_readonly("successor_sequence", &ServiceTrustBundleAuthorization::successor_sequence)
        .def_readonly("signer_service_id", &ServiceTrustBundleAuthorization::signer_service_id)
        .def_readonly("signer_key_id", &ServiceTrustBundleAuthorization::signer_key_id)
        .def_readonly("algorithm", &ServiceTrustBundleAuthorization::algorithm)
        .def_readonly("authentication_tag", &ServiceTrustBundleAuthorization::authentication_tag);

    py::class_<ServiceTrustBundleAuthorizationSet>(module, "ServiceTrustBundleAuthorizationSet")
        .def_readonly("id", &ServiceTrustBundleAuthorizationSet::id)
        .def_readonly("predecessor_bundle_id", &ServiceTrustBundleAuthorizationSet::predecessor_bundle_id)
        .def_readonly("successor_bundle_id", &ServiceTrustBundleAuthorizationSet::successor_bundle_id)
        .def_readonly("predecessor_sequence", &ServiceTrustBundleAuthorizationSet::predecessor_sequence)
        .def_readonly("successor_sequence", &ServiceTrustBundleAuthorizationSet::successor_sequence)
        .def_readonly("authorizations", &ServiceTrustBundleAuthorizationSet::authorizations);

    py::enum_<ServiceTrustRotationEventType>(module, "ServiceTrustRotationEventType")
        .value("ROOT_PINNED", ServiceTrustRotationEventType::RootPinned)
        .value("SUCCESSOR_AUTHORIZED", ServiceTrustRotationEventType::SuccessorAuthorized);

    py::class_<ServiceTrustRotationRecord>(module, "ServiceTrustRotationRecord")
        .def_readonly("storage_schema", &ServiceTrustRotationRecord::storage_schema)
        .def_readonly("sequence", &ServiceTrustRotationRecord::sequence)
        .def_readonly("id", &ServiceTrustRotationRecord::id)
        .def_readonly("parent_id", &ServiceTrustRotationRecord::parent_id)
        .def_readonly("type", &ServiceTrustRotationRecord::type)
        .def_readonly("bundle_id", &ServiceTrustRotationRecord::bundle_id)
        .def_readonly("authorization", &ServiceTrustRotationRecord::authorization)
        .def_readonly("authorization_set", &ServiceTrustRotationRecord::authorization_set);

    py::class_<ServiceTrustHistoryLoadOptions>(module, "ServiceTrustHistoryLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_bundles", &ServiceTrustHistoryLoadOptions::maximum_bundles)
        .def_readwrite("maximum_keys_per_bundle", &ServiceTrustHistoryLoadOptions::maximum_keys_per_bundle)
        .def_readwrite("maximum_total_keys", &ServiceTrustHistoryLoadOptions::maximum_total_keys)
        .def_readwrite("maximum_signatures_per_rotation",
                       &ServiceTrustHistoryLoadOptions::maximum_signatures_per_rotation)
        .def_readwrite("maximum_metadata_bytes", &ServiceTrustHistoryLoadOptions::maximum_metadata_bytes)
        .def_readwrite("maximum_bundle_bytes", &ServiceTrustHistoryLoadOptions::maximum_bundle_bytes)
        .def_readwrite("cancellation", &ServiceTrustHistoryLoadOptions::cancellation);

    py::class_<ServiceTrustCheckpointSignature>(module, "ServiceTrustCheckpointSignature")
        .def_readonly("signer_service_id", &ServiceTrustCheckpointSignature::signer_service_id)
        .def_readonly("signer_key_id", &ServiceTrustCheckpointSignature::signer_key_id)
        .def_readonly("algorithm", &ServiceTrustCheckpointSignature::algorithm)
        .def_readonly("authentication_tag", &ServiceTrustCheckpointSignature::authentication_tag);

    py::class_<ServiceTrustCheckpointLoadOptions>(module, "ServiceTrustCheckpointLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_signatures", &ServiceTrustCheckpointLoadOptions::maximum_signatures)
        .def_readwrite("maximum_payload_bytes", &ServiceTrustCheckpointLoadOptions::maximum_payload_bytes);

    py::class_<ServiceTrustCheckpoint>(module, "ServiceTrustCheckpoint")
        .def_readonly("storage_schema", &ServiceTrustCheckpoint::storage_schema)
        .def_readonly("id", &ServiceTrustCheckpoint::id)
        .def_readonly("root_bundle_id", &ServiceTrustCheckpoint::root_bundle_id)
        .def_readonly("head_bundle_id", &ServiceTrustCheckpoint::head_bundle_id)
        .def_readonly("head_sequence", &ServiceTrustCheckpoint::head_sequence)
        .def_readonly("head_record_id", &ServiceTrustCheckpoint::head_record_id)
        .def_readonly("signatures", &ServiceTrustCheckpoint::signatures)
        .def("valid", &ServiceTrustCheckpoint::valid)
        .def(
            "save",
            [](const ServiceTrustCheckpoint& checkpoint, const std::filesystem::path& path,
               const SaveOptions& options) { unwrap_void(checkpoint.save(path, options)); },
            py::arg("path"), py::arg("options") = SaveOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& path, const ServiceTrustCheckpointLoadOptions& options) {
                return unwrap(ServiceTrustCheckpoint::load(path, options));
            },
            py::arg("path"), py::arg("options") = ServiceTrustCheckpointLoadOptions{});

    py::class_<ServiceTrustHistory>(module, "ServiceTrustHistory")
        .def_static(
            "create",
            [](const std::filesystem::path& directory, const ServiceTrustBundle& root_bundle,
               const std::string& expected_root_bundle_id) {
                return unwrap(ServiceTrustHistory::create(directory, root_bundle, expected_root_bundle_id));
            },
            py::arg("directory"), py::arg("root_bundle"), py::arg("expected_root_bundle_id"))
        .def_static(
            "open",
            [](const std::filesystem::path& directory, const std::string& expected_root_bundle_id,
               const std::string& expected_head_bundle_id, const ServiceTrustHistoryLoadOptions& options) {
                return unwrap(ServiceTrustHistory::open(directory, expected_root_bundle_id,
                                                        expected_head_bundle_id, options));
            },
            py::arg("directory"), py::arg("expected_root_bundle_id"), py::arg("expected_head_bundle_id"),
            py::arg("options") = ServiceTrustHistoryLoadOptions{})
        .def_static(
            "open",
            [](const std::filesystem::path& directory, const std::string& expected_root_bundle_id,
               const ServiceTrustCheckpoint& checkpoint, const std::string& expected_checkpoint_id,
               const ServiceTrustHistoryLoadOptions& options) {
                return unwrap(ServiceTrustHistory::open(directory, expected_root_bundle_id, checkpoint,
                                                        expected_checkpoint_id, options));
            },
            py::arg("directory"), py::arg("expected_root_bundle_id"), py::arg("checkpoint"),
            py::arg("expected_checkpoint_id"), py::arg("options") = ServiceTrustHistoryLoadOptions{})
        .def_property_readonly("directory", &ServiceTrustHistory::directory)
        .def_property_readonly("storage_schema", &ServiceTrustHistory::storage_schema)
        .def_property_readonly("root_bundle_id", &ServiceTrustHistory::root_bundle_id)
        .def_property_readonly("current_bundle_id", &ServiceTrustHistory::current_bundle_id)
        .def_property_readonly("records", &ServiceTrustHistory::records)
        .def("valid", &ServiceTrustHistory::valid)
        .def("current_bundle",
             [](const ServiceTrustHistory& history) { return unwrap(history.current_bundle()); })
        .def("bundle", [](const ServiceTrustHistory& history,
                          const std::string& bundle_id) { return unwrap(history.bundle(bundle_id)); })
        .def(
            "publish",
            [](ServiceTrustHistory& history, const ServiceTrustBundle& successor,
               const ServiceTrustBundleAuthorization& authorization,
               const std::string& expected_head_bundle_id, std::size_t maximum_bundles) {
                return unwrap(
                    history.publish(successor, authorization, expected_head_bundle_id, maximum_bundles));
            },
            py::arg("successor"), py::arg("authorization"), py::arg("expected_head_bundle_id"),
            py::arg("maximum_bundles") = 100'000)
        .def(
            "publish",
            [](ServiceTrustHistory& history, const ServiceTrustBundle& successor,
               const ServiceTrustBundleAuthorizationSet& authorization_set,
               const std::string& expected_head_bundle_id, std::size_t maximum_bundles) {
                return unwrap(
                    history.publish(successor, authorization_set, expected_head_bundle_id, maximum_bundles));
            },
            py::arg("successor"), py::arg("authorization_set"), py::arg("expected_head_bundle_id"),
            py::arg("maximum_bundles") = 100'000);

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
           std::uint64_t valid_through_sequence, ServiceKeyState state, bool allow_fetch, bool allow_publish,
           bool allow_rotate) {
            const auto public_copy = static_cast<std::string>(public_key);
            return unwrap(rbfsafe::make_service_public_key(std::move(service_id), bytes_view(public_copy),
                                                           valid_from_sequence, valid_through_sequence, state,
                                                           allow_fetch, allow_publish, allow_rotate));
        },
        py::arg("service_id"), py::arg("public_key"), py::arg("valid_from_sequence") = 1,
        py::arg("valid_through_sequence") = 0, py::arg("state") = ServiceKeyState::Pending,
        py::arg("allow_fetch") = true, py::arg("allow_publish") = true, py::arg("allow_rotate") = false);

    module.def("valid_service_public_key", &valid_service_public_key);
    module.def("valid_service_trust_rotation_policy", &valid_service_trust_rotation_policy);

    module.def(
        "rotate_service_trust_bundle",
        [](const ServiceTrustBundle& previous, std::vector<ServicePublicKey> keys) {
            return unwrap(rbfsafe::rotate_service_trust_bundle(previous, std::move(keys)));
        },
        py::arg("previous"), py::arg("keys"));

    module.def("valid_service_trust_bundle_authorization", &valid_service_trust_bundle_authorization);
    module.def("valid_service_trust_bundle_authorization_set", &valid_service_trust_bundle_authorization_set);
    module.def("valid_service_trust_checkpoint_signature", &valid_service_trust_checkpoint_signature);

    module.def(
        "authorize_service_trust_bundle_successor",
        [](const ServiceTrustBundle& predecessor, const ServiceTrustBundle& successor,
           std::string signer_service_id, std::string signer_key_id, const py::bytes& secret_key) {
            const SensitiveBytes secret_copy(secret_key);
            return unwrap(rbfsafe::authorize_service_trust_bundle_successor(
                predecessor, successor, std::move(signer_service_id), std::move(signer_key_id),
                secret_copy.view()));
        },
        py::arg("predecessor"), py::arg("successor"), py::arg("signer_service_id"), py::arg("signer_key_id"),
        py::arg("ed25519_secret_key"));

    module.def(
        "verify_service_trust_bundle_successor",
        [](const ServiceTrustBundle& predecessor, const ServiceTrustBundle& successor,
           const ServiceTrustBundleAuthorization& authorization) {
            unwrap_void(
                rbfsafe::verify_service_trust_bundle_successor(predecessor, successor, authorization));
        },
        py::arg("predecessor"), py::arg("successor"), py::arg("authorization"));

    module.def(
        "assemble_service_trust_bundle_authorizations",
        [](const ServiceTrustBundle& predecessor, const ServiceTrustBundle& successor,
           std::vector<ServiceTrustBundleAuthorization> authorizations) {
            return unwrap(assemble_service_trust_bundle_authorizations(predecessor, successor,
                                                                       std::move(authorizations)));
        },
        py::arg("predecessor"), py::arg("successor"), py::arg("authorizations"));

    module.def(
        "verify_service_trust_bundle_successor",
        [](const ServiceTrustBundle& predecessor, const ServiceTrustBundle& successor,
           const ServiceTrustBundleAuthorizationSet& authorization_set) {
            unwrap_void(verify_service_trust_bundle_successor(predecessor, successor, authorization_set));
        },
        py::arg("predecessor"), py::arg("successor"), py::arg("authorization_set"));

    module.def(
        "sign_service_trust_checkpoint",
        [](const ServiceTrustHistory& history, std::string signer_service_id, std::string signer_key_id,
           const py::bytes& secret_key) {
            const SensitiveBytes secret_copy(secret_key);
            return unwrap(rbfsafe::sign_service_trust_checkpoint(
                history, std::move(signer_service_id), std::move(signer_key_id), secret_copy.view()));
        },
        py::arg("history"), py::arg("signer_service_id"), py::arg("signer_key_id"),
        py::arg("ed25519_secret_key"));

    module.def(
        "assemble_service_trust_checkpoint",
        [](const ServiceTrustHistory& history, std::vector<ServiceTrustCheckpointSignature> signatures) {
            return unwrap(rbfsafe::assemble_service_trust_checkpoint(history, std::move(signatures)));
        },
        py::arg("history"), py::arg("signatures"));

    module.def(
        "verify_service_trust_checkpoint",
        [](const ServiceTrustHistory& history, const ServiceTrustCheckpoint& checkpoint,
           const std::string& expected_checkpoint_id) {
            unwrap_void(
                rbfsafe::verify_service_trust_checkpoint(history, checkpoint, expected_checkpoint_id));
        },
        py::arg("history"), py::arg("checkpoint"), py::arg("expected_checkpoint_id"));

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
    module.def("service_trust_rotation_event_type_name", &service_trust_rotation_event_type_name);
}

} // namespace rbfsafe::python_binding
