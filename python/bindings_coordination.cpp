#include "binding_support.h"

#include <rbfsafe/modules/assurance.h>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <string>
#include <utility>

namespace rbfsafe::python_binding {
namespace {

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

    std::span<const std::byte> view() const { return std::as_bytes(std::span(value_.data(), value_.size())); }

  private:
    std::string value_;
};

} // namespace

void bind_coordination(py::module_& module) {
    py::class_<OccupancyPublication>(module, "OccupancyPublication")
        .def_static(
            "load",
            [](const std::filesystem::path& path, std::uintmax_t maximum_payload_bytes) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return OccupancyPublication::load(path, maximum_payload_bytes);
                }();
                return unwrap(std::move(result));
            },
            py::arg("path"), py::arg("maximum_payload_bytes") = 1'048'576ULL)
        .def_readonly("storage_schema", &OccupancyPublication::storage_schema)
        .def_readonly("id", &OccupancyPublication::id)
        .def_readonly("stream_id", &OccupancyPublication::stream_id)
        .def_readonly("publisher_sequence", &OccupancyPublication::publisher_sequence)
        .def_readonly("parent_publication_id", &OccupancyPublication::parent_publication_id)
        .def_readonly("publisher_service_id", &OccupancyPublication::publisher_service_id)
        .def_readonly("publisher_key_id", &OccupancyPublication::publisher_key_id)
        .def_readonly("trust_bundle_id", &OccupancyPublication::trust_bundle_id)
        .def_readonly("occupancy_bundle_id", &OccupancyPublication::occupancy_bundle_id)
        .def_readonly("timeline_id", &OccupancyPublication::timeline_id)
        .def_readonly("workspace_frame_id", &OccupancyPublication::workspace_frame_id)
        .def_readonly("valid_from_tick", &OccupancyPublication::valid_from_tick)
        .def_readonly("valid_through_tick", &OccupancyPublication::valid_through_tick)
        .def_readonly("payload_digest", &OccupancyPublication::payload_digest)
        .def_readonly("payload_bytes", &OccupancyPublication::payload_bytes)
        .def_readonly("algorithm", &OccupancyPublication::algorithm)
        .def_readonly("authentication_tag", &OccupancyPublication::authentication_tag)
        .def("valid", &OccupancyPublication::valid)
        .def_property_readonly("evidence", &OccupancyPublication::evidence)
        .def_property_readonly("authorizes_execution", &OccupancyPublication::authorizes_execution)
        .def(
            "save",
            [](const OccupancyPublication& publication, const std::filesystem::path& path, bool overwrite) {
                SaveOptions options;
                options.overwrite = overwrite;
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return publication.save(path, options);
                }();
                unwrap_void(std::move(result));
            },
            py::arg("path"), py::arg("overwrite") = false);

    py::class_<VerifiedOccupancyPublication>(module, "VerifiedOccupancyPublication")
        .def_readonly("id", &VerifiedOccupancyPublication::id)
        .def_readonly("publication_id", &VerifiedOccupancyPublication::publication_id)
        .def_readonly("stream_id", &VerifiedOccupancyPublication::stream_id)
        .def_readonly("publisher_sequence", &VerifiedOccupancyPublication::publisher_sequence)
        .def_readonly("publisher_service_id", &VerifiedOccupancyPublication::publisher_service_id)
        .def_readonly("publisher_key_id", &VerifiedOccupancyPublication::publisher_key_id)
        .def_readonly("trust_bundle_id", &VerifiedOccupancyPublication::trust_bundle_id)
        .def_readonly("occupancy_bundle_id", &VerifiedOccupancyPublication::occupancy_bundle_id)
        .def_readonly("timeline_id", &VerifiedOccupancyPublication::timeline_id)
        .def_readonly("workspace_frame_id", &VerifiedOccupancyPublication::workspace_frame_id)
        .def_readonly("valid_from_tick", &VerifiedOccupancyPublication::valid_from_tick)
        .def_readonly("valid_through_tick", &VerifiedOccupancyPublication::valid_through_tick)
        .def_readonly("evaluation_tick", &VerifiedOccupancyPublication::evaluation_tick)
        .def_readonly("payload_digest", &VerifiedOccupancyPublication::payload_digest)
        .def_readonly("payload_bytes", &VerifiedOccupancyPublication::payload_bytes)
        .def("valid", &VerifiedOccupancyPublication::valid)
        .def_property_readonly("evidence", &VerifiedOccupancyPublication::evidence)
        .def_property_readonly("authorizes_execution", &VerifiedOccupancyPublication::authorizes_execution);

    py::enum_<OccupancyPublicationHistoryRelation>(module, "OccupancyPublicationHistoryRelation")
        .value("IDENTICAL", OccupancyPublicationHistoryRelation::Identical)
        .value("FIRST_EXTENDS_SECOND", OccupancyPublicationHistoryRelation::FirstExtendsSecond)
        .value("SECOND_EXTENDS_FIRST", OccupancyPublicationHistoryRelation::SecondExtendsFirst)
        .value("FORKED", OccupancyPublicationHistoryRelation::Forked)
        .export_values();

    py::class_<OccupancyPublicationHistoryRecord>(module, "OccupancyPublicationHistoryRecord")
        .def_readonly("storage_schema", &OccupancyPublicationHistoryRecord::storage_schema)
        .def_readonly("sequence", &OccupancyPublicationHistoryRecord::sequence)
        .def_readonly("id", &OccupancyPublicationHistoryRecord::id)
        .def_readonly("parent_record_id", &OccupancyPublicationHistoryRecord::parent_record_id)
        .def_readonly("publication_id", &OccupancyPublicationHistoryRecord::publication_id)
        .def_readonly("authentication_tag", &OccupancyPublicationHistoryRecord::authentication_tag)
        .def_readonly("payload_digest", &OccupancyPublicationHistoryRecord::payload_digest)
        .def_readonly("payload_bytes", &OccupancyPublicationHistoryRecord::payload_bytes)
        .def("valid", &OccupancyPublicationHistoryRecord::valid)
        .def_property_readonly("evidence", &OccupancyPublicationHistoryRecord::evidence)
        .def_property_readonly("authorizes_execution",
                               &OccupancyPublicationHistoryRecord::authorizes_execution);

    py::class_<OccupancyPublicationHistoryAudit>(module, "OccupancyPublicationHistoryAudit")
        .def_readonly("storage_schema", &OccupancyPublicationHistoryAudit::storage_schema)
        .def_readonly("id", &OccupancyPublicationHistoryAudit::id)
        .def_readonly("relation", &OccupancyPublicationHistoryAudit::relation)
        .def_readonly("stream_id", &OccupancyPublicationHistoryAudit::stream_id)
        .def_readonly("publisher_service_id", &OccupancyPublicationHistoryAudit::publisher_service_id)
        .def_readonly("trust_bundle_id", &OccupancyPublicationHistoryAudit::trust_bundle_id)
        .def_readonly("root_publication_id", &OccupancyPublicationHistoryAudit::root_publication_id)
        .def_readonly("first_head_publication_id",
                      &OccupancyPublicationHistoryAudit::first_head_publication_id)
        .def_readonly("second_head_publication_id",
                      &OccupancyPublicationHistoryAudit::second_head_publication_id)
        .def_readonly("first_publication_count", &OccupancyPublicationHistoryAudit::first_publication_count)
        .def_readonly("second_publication_count", &OccupancyPublicationHistoryAudit::second_publication_count)
        .def_readonly("common_prefix_count", &OccupancyPublicationHistoryAudit::common_prefix_count)
        .def_readonly("common_publication_id", &OccupancyPublicationHistoryAudit::common_publication_id)
        .def("valid", &OccupancyPublicationHistoryAudit::valid)
        .def_property_readonly("fork_detected", &OccupancyPublicationHistoryAudit::fork_detected)
        .def_property_readonly("evidence", &OccupancyPublicationHistoryAudit::evidence)
        .def_property_readonly("authorizes_execution",
                               &OccupancyPublicationHistoryAudit::authorizes_execution);

    py::class_<OccupancyPublicationHistoryLoadOptions>(module, "OccupancyPublicationHistoryLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_publications", &OccupancyPublicationHistoryLoadOptions::maximum_publications)
        .def_readwrite("maximum_manifest_bytes",
                       &OccupancyPublicationHistoryLoadOptions::maximum_manifest_bytes)
        .def_readwrite("maximum_record_bytes", &OccupancyPublicationHistoryLoadOptions::maximum_record_bytes)
        .def_readwrite("maximum_publication_bytes",
                       &OccupancyPublicationHistoryLoadOptions::maximum_publication_bytes)
        .def_readwrite("maximum_trust_bundle_bytes",
                       &OccupancyPublicationHistoryLoadOptions::maximum_trust_bundle_bytes)
        .def_readwrite("maximum_total_payload_bytes",
                       &OccupancyPublicationHistoryLoadOptions::maximum_total_payload_bytes)
        .def_readwrite("maximum_trust_keys", &OccupancyPublicationHistoryLoadOptions::maximum_trust_keys)
        .def_readwrite("occupancy", &OccupancyPublicationHistoryLoadOptions::occupancy);

    py::class_<OccupancyPublicationHistory>(module, "OccupancyPublicationHistory")
        .def_static(
            "create",
            [](const std::filesystem::path& directory, const OccupancyPublication& root_publication,
               const std::filesystem::path& root_payload_path, const ServiceTrustBundle& trust_bundle,
               std::string expected_stream_id, std::string expected_publisher_service_id,
               std::string expected_trust_bundle_id, std::string expected_root_publication_id,
               const OccupancyPublicationHistoryLoadOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return OccupancyPublicationHistory::create(
                        directory, root_publication, root_payload_path, trust_bundle, expected_stream_id,
                        expected_publisher_service_id, expected_trust_bundle_id, expected_root_publication_id,
                        options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("directory"), py::arg("root_publication"), py::arg("root_payload_path"),
            py::arg("trust_bundle"), py::arg("expected_stream_id"), py::arg("expected_publisher_service_id"),
            py::arg("expected_trust_bundle_id"), py::arg("expected_root_publication_id"),
            py::arg("options") = OccupancyPublicationHistoryLoadOptions{})
        .def_static(
            "open",
            [](const std::filesystem::path& directory, std::string expected_stream_id,
               std::string expected_publisher_service_id, std::string expected_trust_bundle_id,
               std::string expected_root_publication_id, std::string expected_head_publication_id,
               const OccupancyPublicationHistoryLoadOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return OccupancyPublicationHistory::open(
                        directory, expected_stream_id, expected_publisher_service_id,
                        expected_trust_bundle_id, expected_root_publication_id, expected_head_publication_id,
                        options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("directory"), py::arg("expected_stream_id"), py::arg("expected_publisher_service_id"),
            py::arg("expected_trust_bundle_id"), py::arg("expected_root_publication_id"),
            py::arg("expected_head_publication_id"),
            py::arg("options") = OccupancyPublicationHistoryLoadOptions{})
        .def_property_readonly("directory", &OccupancyPublicationHistory::directory)
        .def_property_readonly("storage_schema", &OccupancyPublicationHistory::storage_schema)
        .def_property_readonly("stream_id", &OccupancyPublicationHistory::stream_id)
        .def_property_readonly("publisher_service_id", &OccupancyPublicationHistory::publisher_service_id)
        .def_property_readonly("trust_bundle_id", &OccupancyPublicationHistory::trust_bundle_id)
        .def_property_readonly("root_publication_id", &OccupancyPublicationHistory::root_publication_id)
        .def_property_readonly("current_publication_id", &OccupancyPublicationHistory::current_publication_id)
        .def_property_readonly("timeline_id", &OccupancyPublicationHistory::timeline_id)
        .def_property_readonly("workspace_frame_id", &OccupancyPublicationHistory::workspace_frame_id)
        .def_property_readonly("records", &OccupancyPublicationHistory::records)
        .def("valid", &OccupancyPublicationHistory::valid)
        .def_property_readonly("evidence", &OccupancyPublicationHistory::evidence)
        .def_property_readonly("authorizes_execution", &OccupancyPublicationHistory::authorizes_execution)
        .def("trust_bundle",
             [](const OccupancyPublicationHistory& history) { return unwrap(history.trust_bundle()); })
        .def("current_publication",
             [](const OccupancyPublicationHistory& history) { return unwrap(history.current_publication()); })
        .def("publication",
             [](const OccupancyPublicationHistory& history, const std::string& publication_id) {
                 return unwrap(history.publication(publication_id));
             })
        .def(
            "verify",
            [](const OccupancyPublicationHistory& history, const std::string& publication_id,
               std::uint64_t evaluation_tick) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return history.verify(publication_id, evaluation_tick);
                }();
                return unwrap(std::move(result));
            },
            py::arg("publication_id"), py::arg("evaluation_tick"))
        .def(
            "publish",
            [](OccupancyPublicationHistory& history, const OccupancyPublication& publication,
               const std::filesystem::path& payload_path, const std::string& expected_head_publication_id,
               std::size_t maximum_publications) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return history.publish(publication, payload_path, expected_head_publication_id,
                                           maximum_publications);
                }();
                return unwrap(std::move(result));
            },
            py::arg("publication"), py::arg("payload_path"), py::arg("expected_head_publication_id"),
            py::arg("maximum_publications") = 100'000);

    py::class_<RotatingOccupancyPublicationHistoryLoadOptions>(
        module, "RotatingOccupancyPublicationHistoryLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_publications",
                       &RotatingOccupancyPublicationHistoryLoadOptions::maximum_publications)
        .def_readwrite("maximum_manifest_bytes",
                       &RotatingOccupancyPublicationHistoryLoadOptions::maximum_manifest_bytes)
        .def_readwrite("maximum_record_bytes",
                       &RotatingOccupancyPublicationHistoryLoadOptions::maximum_record_bytes)
        .def_readwrite("maximum_publication_bytes",
                       &RotatingOccupancyPublicationHistoryLoadOptions::maximum_publication_bytes)
        .def_readwrite("maximum_total_payload_bytes",
                       &RotatingOccupancyPublicationHistoryLoadOptions::maximum_total_payload_bytes)
        .def_readwrite("trust", &RotatingOccupancyPublicationHistoryLoadOptions::trust)
        .def_readwrite("occupancy", &RotatingOccupancyPublicationHistoryLoadOptions::occupancy);

    py::class_<RotatingOccupancyPublicationHistoryAudit>(module, "RotatingOccupancyPublicationHistoryAudit")
        .def_readonly("storage_schema", &RotatingOccupancyPublicationHistoryAudit::storage_schema)
        .def_readonly("id", &RotatingOccupancyPublicationHistoryAudit::id)
        .def_readonly("publication_relation", &RotatingOccupancyPublicationHistoryAudit::publication_relation)
        .def_readonly("trust_relation", &RotatingOccupancyPublicationHistoryAudit::trust_relation)
        .def_readonly("stream_id", &RotatingOccupancyPublicationHistoryAudit::stream_id)
        .def_readonly("publisher_service_id", &RotatingOccupancyPublicationHistoryAudit::publisher_service_id)
        .def_readonly("trust_root_bundle_id", &RotatingOccupancyPublicationHistoryAudit::trust_root_bundle_id)
        .def_readonly("root_publication_id", &RotatingOccupancyPublicationHistoryAudit::root_publication_id)
        .def_readonly("first_trust_head_bundle_id",
                      &RotatingOccupancyPublicationHistoryAudit::first_trust_head_bundle_id)
        .def_readonly("second_trust_head_bundle_id",
                      &RotatingOccupancyPublicationHistoryAudit::second_trust_head_bundle_id)
        .def_readonly("first_head_publication_id",
                      &RotatingOccupancyPublicationHistoryAudit::first_head_publication_id)
        .def_readonly("second_head_publication_id",
                      &RotatingOccupancyPublicationHistoryAudit::second_head_publication_id)
        .def_readonly("first_trust_bundle_count",
                      &RotatingOccupancyPublicationHistoryAudit::first_trust_bundle_count)
        .def_readonly("second_trust_bundle_count",
                      &RotatingOccupancyPublicationHistoryAudit::second_trust_bundle_count)
        .def_readonly("common_trust_prefix_count",
                      &RotatingOccupancyPublicationHistoryAudit::common_trust_prefix_count)
        .def_readonly("common_trust_bundle_id",
                      &RotatingOccupancyPublicationHistoryAudit::common_trust_bundle_id)
        .def_readonly("first_publication_count",
                      &RotatingOccupancyPublicationHistoryAudit::first_publication_count)
        .def_readonly("second_publication_count",
                      &RotatingOccupancyPublicationHistoryAudit::second_publication_count)
        .def_readonly("common_publication_prefix_count",
                      &RotatingOccupancyPublicationHistoryAudit::common_publication_prefix_count)
        .def_readonly("common_publication_id",
                      &RotatingOccupancyPublicationHistoryAudit::common_publication_id)
        .def("valid", &RotatingOccupancyPublicationHistoryAudit::valid)
        .def_property_readonly("fork_detected", &RotatingOccupancyPublicationHistoryAudit::fork_detected)
        .def_property_readonly("evidence", &RotatingOccupancyPublicationHistoryAudit::evidence)
        .def_property_readonly("authorizes_execution",
                               &RotatingOccupancyPublicationHistoryAudit::authorizes_execution);

    py::class_<RotatingOccupancyPublicationHistory>(module, "RotatingOccupancyPublicationHistory")
        .def_static(
            "create",
            [](const std::filesystem::path& directory, const OccupancyPublication& root_publication,
               const std::filesystem::path& root_payload_path, const ServiceTrustHistory& trust_history,
               std::string expected_stream_id, std::string expected_publisher_service_id,
               std::string expected_trust_root_bundle_id, std::string expected_trust_head_bundle_id,
               std::string expected_root_publication_id,
               const RotatingOccupancyPublicationHistoryLoadOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return RotatingOccupancyPublicationHistory::create(
                        directory, root_publication, root_payload_path, trust_history, expected_stream_id,
                        expected_publisher_service_id, expected_trust_root_bundle_id,
                        expected_trust_head_bundle_id, expected_root_publication_id, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("directory"), py::arg("root_publication"), py::arg("root_payload_path"),
            py::arg("trust_history"), py::arg("expected_stream_id"), py::arg("expected_publisher_service_id"),
            py::arg("expected_trust_root_bundle_id"), py::arg("expected_trust_head_bundle_id"),
            py::arg("expected_root_publication_id"),
            py::arg("options") = RotatingOccupancyPublicationHistoryLoadOptions{})
        .def_static(
            "open",
            [](const std::filesystem::path& directory, std::string expected_stream_id,
               std::string expected_publisher_service_id, std::string expected_trust_root_bundle_id,
               std::string expected_trust_head_bundle_id, std::string expected_root_publication_id,
               std::string expected_head_publication_id,
               const RotatingOccupancyPublicationHistoryLoadOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return RotatingOccupancyPublicationHistory::open(
                        directory, expected_stream_id, expected_publisher_service_id,
                        expected_trust_root_bundle_id, expected_trust_head_bundle_id,
                        expected_root_publication_id, expected_head_publication_id, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("directory"), py::arg("expected_stream_id"), py::arg("expected_publisher_service_id"),
            py::arg("expected_trust_root_bundle_id"), py::arg("expected_trust_head_bundle_id"),
            py::arg("expected_root_publication_id"), py::arg("expected_head_publication_id"),
            py::arg("options") = RotatingOccupancyPublicationHistoryLoadOptions{})
        .def_static(
            "open",
            [](const std::filesystem::path& directory, std::string expected_stream_id,
               std::string expected_publisher_service_id, std::string expected_trust_root_bundle_id,
               const ServiceTrustCheckpoint& checkpoint, std::string expected_checkpoint_id,
               std::string expected_root_publication_id, std::string expected_head_publication_id,
               const RotatingOccupancyPublicationHistoryLoadOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return RotatingOccupancyPublicationHistory::open(
                        directory, expected_stream_id, expected_publisher_service_id,
                        expected_trust_root_bundle_id, checkpoint, expected_checkpoint_id,
                        expected_root_publication_id, expected_head_publication_id, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("directory"), py::arg("expected_stream_id"), py::arg("expected_publisher_service_id"),
            py::arg("expected_trust_root_bundle_id"), py::arg("checkpoint"),
            py::arg("expected_checkpoint_id"), py::arg("expected_root_publication_id"),
            py::arg("expected_head_publication_id"),
            py::arg("options") = RotatingOccupancyPublicationHistoryLoadOptions{})
        .def_property_readonly("directory", &RotatingOccupancyPublicationHistory::directory)
        .def_property_readonly("storage_schema", &RotatingOccupancyPublicationHistory::storage_schema)
        .def_property_readonly("stream_id", &RotatingOccupancyPublicationHistory::stream_id)
        .def_property_readonly("publisher_service_id",
                               &RotatingOccupancyPublicationHistory::publisher_service_id)
        .def_property_readonly("trust_root_bundle_id",
                               &RotatingOccupancyPublicationHistory::trust_root_bundle_id)
        .def_property_readonly("current_trust_bundle_id",
                               &RotatingOccupancyPublicationHistory::current_trust_bundle_id)
        .def_property_readonly("root_publication_id",
                               &RotatingOccupancyPublicationHistory::root_publication_id)
        .def_property_readonly("current_publication_id",
                               &RotatingOccupancyPublicationHistory::current_publication_id)
        .def_property_readonly("timeline_id", &RotatingOccupancyPublicationHistory::timeline_id)
        .def_property_readonly("workspace_frame_id", &RotatingOccupancyPublicationHistory::workspace_frame_id)
        .def_property_readonly("records", &RotatingOccupancyPublicationHistory::records)
        .def("valid", &RotatingOccupancyPublicationHistory::valid)
        .def_property_readonly("evidence", &RotatingOccupancyPublicationHistory::evidence)
        .def_property_readonly("authorizes_execution",
                               &RotatingOccupancyPublicationHistory::authorizes_execution)
        .def("trust_history",
             [](const RotatingOccupancyPublicationHistory& history) {
                 return unwrap(history.trust_history());
             })
        .def("current_trust_bundle",
             [](const RotatingOccupancyPublicationHistory& history) {
                 return unwrap(history.current_trust_bundle());
             })
        .def("current_publication",
             [](const RotatingOccupancyPublicationHistory& history) {
                 return unwrap(history.current_publication());
             })
        .def("publication",
             [](const RotatingOccupancyPublicationHistory& history, const std::string& publication_id) {
                 return unwrap(history.publication(publication_id));
             })
        .def(
            "verify",
            [](const RotatingOccupancyPublicationHistory& history, const std::string& publication_id,
               std::uint64_t evaluation_tick) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return history.verify(publication_id, evaluation_tick);
                }();
                return unwrap(std::move(result));
            },
            py::arg("publication_id"), py::arg("evaluation_tick"))
        .def(
            "rotate_trust",
            [](RotatingOccupancyPublicationHistory& history, const ServiceTrustBundle& successor,
               const ServiceTrustBundleAuthorization& authorization,
               const std::string& expected_trust_head_bundle_id, std::size_t maximum_trust_bundles) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return history.rotate_trust(successor, authorization, expected_trust_head_bundle_id,
                                                maximum_trust_bundles);
                }();
                return unwrap(std::move(result));
            },
            py::arg("successor"), py::arg("authorization"), py::arg("expected_trust_head_bundle_id"),
            py::arg("maximum_trust_bundles") = 100'000)
        .def(
            "rotate_trust",
            [](RotatingOccupancyPublicationHistory& history, const ServiceTrustBundle& successor,
               const ServiceTrustBundleAuthorizationSet& authorization_set,
               const std::string& expected_trust_head_bundle_id, std::size_t maximum_trust_bundles) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return history.rotate_trust(successor, authorization_set, expected_trust_head_bundle_id,
                                                maximum_trust_bundles);
                }();
                return unwrap(std::move(result));
            },
            py::arg("successor"), py::arg("authorization_set"), py::arg("expected_trust_head_bundle_id"),
            py::arg("maximum_trust_bundles") = 100'000)
        .def(
            "publish",
            [](RotatingOccupancyPublicationHistory& history, const OccupancyPublication& publication,
               const std::filesystem::path& payload_path, const std::string& expected_head_publication_id,
               const std::string& expected_trust_head_bundle_id, std::size_t maximum_publications) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return history.publish(publication, payload_path, expected_head_publication_id,
                                           expected_trust_head_bundle_id, maximum_publications);
                }();
                return unwrap(std::move(result));
            },
            py::arg("publication"), py::arg("payload_path"), py::arg("expected_head_publication_id"),
            py::arg("expected_trust_head_bundle_id"), py::arg("maximum_publications") = 100'000);

    py::class_<CoordinatedReservationParticipant>(module, "CoordinatedReservationParticipant")
        .def_readonly("storage_schema", &CoordinatedReservationParticipant::storage_schema)
        .def_readonly("id", &CoordinatedReservationParticipant::id)
        .def_readonly("deployment_id", &CoordinatedReservationParticipant::deployment_id)
        .def_readonly("occupancy_id", &CoordinatedReservationParticipant::occupancy_id)
        .def_readonly("stream_id", &CoordinatedReservationParticipant::stream_id)
        .def_readonly("publisher_service_id", &CoordinatedReservationParticipant::publisher_service_id)
        .def_readonly("publisher_key_id", &CoordinatedReservationParticipant::publisher_key_id)
        .def_readonly("publisher_sequence", &CoordinatedReservationParticipant::publisher_sequence)
        .def_readonly("trust_root_bundle_id", &CoordinatedReservationParticipant::trust_root_bundle_id)
        .def_readonly("trust_head_bundle_id", &CoordinatedReservationParticipant::trust_head_bundle_id)
        .def_readonly("publication_trust_bundle_id",
                      &CoordinatedReservationParticipant::publication_trust_bundle_id)
        .def_readonly("publication_root_id", &CoordinatedReservationParticipant::publication_root_id)
        .def_readonly("publication_head_id", &CoordinatedReservationParticipant::publication_head_id)
        .def_readonly("verified_publication_id", &CoordinatedReservationParticipant::verified_publication_id)
        .def_readonly("payload_digest", &CoordinatedReservationParticipant::payload_digest)
        .def_readonly("payload_bytes", &CoordinatedReservationParticipant::payload_bytes)
        .def_readonly("valid_from_tick", &CoordinatedReservationParticipant::valid_from_tick)
        .def_readonly("valid_through_tick", &CoordinatedReservationParticipant::valid_through_tick)
        .def("valid", &CoordinatedReservationParticipant::valid)
        .def_property_readonly("evidence", &CoordinatedReservationParticipant::evidence)
        .def_property_readonly("authorizes_execution",
                               &CoordinatedReservationParticipant::authorizes_execution);

    py::class_<CoordinatedReservationAgreementLoadOptions>(module,
                                                           "CoordinatedReservationAgreementLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_participants",
                       &CoordinatedReservationAgreementLoadOptions::maximum_participants)
        .def_readwrite("maximum_payload_bytes",
                       &CoordinatedReservationAgreementLoadOptions::maximum_payload_bytes)
        .def_readwrite("cancellation", &CoordinatedReservationAgreementLoadOptions::cancellation);

    py::class_<CoordinatedReservationAgreement>(module, "CoordinatedReservationAgreement")
        .def_static(
            "load",
            [](const std::filesystem::path& path, const CoordinatedReservationAgreementLoadOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return CoordinatedReservationAgreement::load(path, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("path"), py::arg("options") = CoordinatedReservationAgreementLoadOptions{})
        .def_readonly("storage_schema", &CoordinatedReservationAgreement::storage_schema)
        .def_readonly("id", &CoordinatedReservationAgreement::id)
        .def_readonly("protocol_id", &CoordinatedReservationAgreement::protocol_id)
        .def_readonly("round", &CoordinatedReservationAgreement::round)
        .def_readonly("parent_agreement_id", &CoordinatedReservationAgreement::parent_agreement_id)
        .def_readonly("evaluation_tick", &CoordinatedReservationAgreement::evaluation_tick)
        .def_readonly("valid_from_tick", &CoordinatedReservationAgreement::valid_from_tick)
        .def_readonly("valid_through_tick", &CoordinatedReservationAgreement::valid_through_tick)
        .def_readonly("occupancy_bundle_id", &CoordinatedReservationAgreement::occupancy_bundle_id)
        .def_readonly("occupancy_report_id", &CoordinatedReservationAgreement::occupancy_report_id)
        .def_readonly("timeline_id", &CoordinatedReservationAgreement::timeline_id)
        .def_readonly("workspace_frame_id", &CoordinatedReservationAgreement::workspace_frame_id)
        .def_readonly("minimum_separation", &CoordinatedReservationAgreement::minimum_separation)
        .def_readonly("payload_digest", &CoordinatedReservationAgreement::payload_digest)
        .def_readonly("payload_bytes", &CoordinatedReservationAgreement::payload_bytes)
        .def_readonly("participants", &CoordinatedReservationAgreement::participants)
        .def("valid", &CoordinatedReservationAgreement::valid)
        .def_property_readonly("evidence", &CoordinatedReservationAgreement::evidence)
        .def_property_readonly("authorizes_execution", &CoordinatedReservationAgreement::authorizes_execution)
        .def(
            "save",
            [](const CoordinatedReservationAgreement& agreement, const std::filesystem::path& path,
               bool overwrite) {
                SaveOptions options;
                options.overwrite = overwrite;
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return agreement.save(path, options);
                }();
                unwrap_void(std::move(result));
            },
            py::arg("path"), py::arg("overwrite") = false);

    module.def(
        "make_coordinated_reservation_agreement",
        [](std::string protocol_id, std::uint64_t round, std::string parent_agreement_id,
           std::uint64_t evaluation_tick, const ContinuousFleetOccupancyBundle& occupancy_bundle,
           const std::vector<std::string>& deployment_ids,
           const std::vector<RotatingOccupancyPublicationHistory>& histories,
           const CoordinatedReservationAgreementLoadOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::make_coordinated_reservation_agreement(
                    std::move(protocol_id), round, std::move(parent_agreement_id), evaluation_tick,
                    occupancy_bundle, deployment_ids, histories, options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("protocol_id"), py::arg("round"), py::arg("parent_agreement_id"), py::arg("evaluation_tick"),
        py::arg("occupancy_bundle"), py::arg("deployment_ids"), py::arg("histories"),
        py::arg("options") = CoordinatedReservationAgreementLoadOptions{});

    module.def(
        "verify_coordinated_reservation_agreement",
        [](const CoordinatedReservationAgreement& agreement,
           const ContinuousFleetOccupancyBundle& occupancy_bundle,
           const std::vector<std::string>& deployment_ids,
           const std::vector<RotatingOccupancyPublicationHistory>& histories,
           const CoordinatedReservationAgreementLoadOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::verify_coordinated_reservation_agreement(agreement, occupancy_bundle,
                                                                         deployment_ids, histories, options);
            }();
            unwrap_void(std::move(result));
        },
        py::arg("agreement"), py::arg("occupancy_bundle"), py::arg("deployment_ids"), py::arg("histories"),
        py::arg("options") = CoordinatedReservationAgreementLoadOptions{});

    module.def(
        "verify_coordinated_reservation_successor",
        [](const CoordinatedReservationAgreement& previous, const CoordinatedReservationAgreement& successor,
           const std::vector<std::string>& deployment_ids,
           const std::vector<RotatingOccupancyPublicationHistory>& histories,
           const CoordinatedReservationAgreementLoadOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::verify_coordinated_reservation_successor(previous, successor, deployment_ids,
                                                                         histories, options);
            }();
            unwrap_void(std::move(result));
        },
        py::arg("previous"), py::arg("successor"), py::arg("deployment_ids"), py::arg("histories"),
        py::arg("options") = CoordinatedReservationAgreementLoadOptions{});

    module.def(
        "sign_continuous_fleet_occupancy_publication",
        [](const std::filesystem::path& occupancy_payload_path, const ServiceTrustBundle& trust_bundle,
           std::string stream_id, std::string publisher_service_id, std::string publisher_key_id,
           const py::bytes& secret_key, std::uint64_t publisher_sequence, std::string parent_publication_id,
           std::uint64_t valid_from_tick, std::uint64_t valid_through_tick,
           const ContinuousFleetOccupancyBundleLoadOptions& options) {
            const SensitiveBytes secret_copy(secret_key);
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::sign_continuous_fleet_occupancy_publication(
                    occupancy_payload_path, trust_bundle, std::move(stream_id),
                    std::move(publisher_service_id), std::move(publisher_key_id), secret_copy.view(),
                    publisher_sequence, std::move(parent_publication_id), valid_from_tick, valid_through_tick,
                    options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("occupancy_payload_path"), py::arg("trust_bundle"), py::arg("stream_id"),
        py::arg("publisher_service_id"), py::arg("publisher_key_id"), py::arg("ed25519_secret_key"),
        py::arg("publisher_sequence"), py::arg("parent_publication_id"), py::arg("valid_from_tick"),
        py::arg("valid_through_tick"), py::arg("options") = ContinuousFleetOccupancyBundleLoadOptions{});

    module.def(
        "verify_continuous_fleet_occupancy_publication",
        [](const std::filesystem::path& occupancy_payload_path, const OccupancyPublication& publication,
           const ServiceTrustBundle& trust_bundle, std::string expected_stream_id,
           std::string expected_publisher_service_id, std::string expected_trust_bundle_id,
           std::string expected_parent_publication_id, std::uint64_t evaluation_tick,
           const ContinuousFleetOccupancyBundleLoadOptions& options) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::verify_continuous_fleet_occupancy_publication(
                    occupancy_payload_path, publication, trust_bundle, expected_stream_id,
                    expected_publisher_service_id, expected_trust_bundle_id, expected_parent_publication_id,
                    evaluation_tick, options);
            }();
            return unwrap(std::move(result));
        },
        py::arg("occupancy_payload_path"), py::arg("publication"), py::arg("trust_bundle"),
        py::arg("expected_stream_id"), py::arg("expected_publisher_service_id"),
        py::arg("expected_trust_bundle_id"), py::arg("expected_parent_publication_id"),
        py::arg("evaluation_tick"), py::arg("options") = ContinuousFleetOccupancyBundleLoadOptions{});

    module.def(
        "verify_occupancy_publication_successor",
        [](const OccupancyPublication& previous, const OccupancyPublication& successor) {
            unwrap_void(rbfsafe::verify_occupancy_publication_successor(previous, successor));
        },
        py::arg("previous"), py::arg("successor"));

    module.def("occupancy_publication_history_relation_name", &occupancy_publication_history_relation_name,
               py::arg("relation"));

    module.def(
        "audit_occupancy_publication_histories",
        [](const OccupancyPublicationHistory& first, const OccupancyPublicationHistory& second) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::audit_occupancy_publication_histories(first, second);
            }();
            return unwrap(std::move(result));
        },
        py::arg("first"), py::arg("second"));

    module.def(
        "audit_rotating_occupancy_publication_histories",
        [](const RotatingOccupancyPublicationHistory& first,
           const RotatingOccupancyPublicationHistory& second) {
            auto result = [&]() {
                py::gil_scoped_release release;
                return rbfsafe::audit_rotating_occupancy_publication_histories(first, second);
            }();
            return unwrap(std::move(result));
        },
        py::arg("first"), py::arg("second"));
}

} // namespace rbfsafe::python_binding
