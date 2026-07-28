#include "binding_support.h"

#include <rbfsafe/coordination.h>

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
}

} // namespace rbfsafe::python_binding
