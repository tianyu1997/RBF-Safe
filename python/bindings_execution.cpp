#include "binding_support.h"

#include <rbfsafe/execution.h>

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

void bind_execution(py::module_& module) {
    py::enum_<ExecutionEndpointRole>(module, "ExecutionEndpointRole")
        .value("CONTROLLER", ExecutionEndpointRole::Controller)
        .value("RUNTIME_MONITOR", ExecutionEndpointRole::RuntimeMonitor);

    py::class_<ExecutionEndpointKey>(module, "ExecutionEndpointKey")
        .def_readonly("id", &ExecutionEndpointKey::id)
        .def_readonly("service_id", &ExecutionEndpointKey::service_id)
        .def_readonly("role", &ExecutionEndpointKey::role)
        .def_readonly("algorithm", &ExecutionEndpointKey::algorithm)
        .def_property_readonly("public_key",
                               [](const ExecutionEndpointKey& key) { return array_bytes(key.public_key); });

    py::class_<ExecutionCommand>(module, "ExecutionCommand")
        .def_readonly("index", &ExecutionCommand::index)
        .def_readonly("scheduled_offset_ns", &ExecutionCommand::scheduled_offset_ns)
        .def_readonly("configuration", &ExecutionCommand::configuration);

    py::class_<ExecutionCommandSequence>(module, "ExecutionCommandSequence")
        .def_static(
            "create",
            [](const SafeAtlas& atlas, std::vector<Configuration> configurations,
               std::vector<std::uint64_t> scheduled_offsets_ns, const TrajectoryAuditOptions& options) {
                return unwrap(ExecutionCommandSequence::create(atlas, std::move(configurations),
                                                               std::move(scheduled_offsets_ns), options));
            },
            py::arg("atlas"), py::arg("configurations"), py::arg("scheduled_offsets_ns"),
            py::arg("options") = TrajectoryAuditOptions{})
        .def_readonly("storage_schema", &ExecutionCommandSequence::storage_schema)
        .def_readonly("id", &ExecutionCommandSequence::id)
        .def_readonly("atlas_id", &ExecutionCommandSequence::atlas_id)
        .def_readonly("robot_digest", &ExecutionCommandSequence::robot_digest)
        .def_readonly("scene_digest", &ExecutionCommandSequence::scene_digest)
        .def_readonly("connectivity_certificate_id", &ExecutionCommandSequence::connectivity_certificate_id)
        .def_readonly("dimension", &ExecutionCommandSequence::dimension)
        .def_readonly("commands", &ExecutionCommandSequence::commands)
        .def_readonly("region_sequence", &ExecutionCommandSequence::region_sequence)
        .def("valid", &ExecutionCommandSequence::valid)
        .def(
            "verify_compatible",
            [](const ExecutionCommandSequence& sequence, const SafeAtlas& atlas,
               const TrajectoryAuditOptions& options) {
                unwrap_void(sequence.verify_compatible(atlas, options));
            },
            py::arg("atlas"), py::arg("options") = TrajectoryAuditOptions{});

    py::class_<ExecutionSessionLimits>(module, "ExecutionSessionLimits")
        .def(py::init<>())
        .def_readwrite("maximum_start_delay_ns", &ExecutionSessionLimits::maximum_start_delay_ns)
        .def_readwrite("maximum_duration_ns", &ExecutionSessionLimits::maximum_duration_ns)
        .def_readwrite("maximum_commands", &ExecutionSessionLimits::maximum_commands);

    py::class_<ExecutionSessionRequestInput>(module, "ExecutionSessionRequestInput")
        .def(py::init<>())
        .def_readwrite("session_nonce", &ExecutionSessionRequestInput::session_nonce)
        .def_readwrite("controller", &ExecutionSessionRequestInput::controller)
        .def_readwrite("runtime_monitor", &ExecutionSessionRequestInput::runtime_monitor)
        .def_readwrite("limits", &ExecutionSessionRequestInput::limits);

    py::class_<ExecutionSessionRequest>(module, "ExecutionSessionRequest")
        .def_static(
            "create",
            [](const ReviewedDeploymentProfile& reviewed, const ExecutionCommandSequence& sequence,
               ExecutionSessionRequestInput input) {
                return unwrap(ExecutionSessionRequest::create(reviewed, sequence, std::move(input)));
            },
            py::arg("reviewed_profile"), py::arg("command_sequence"), py::arg("input"))
        .def_readonly("storage_schema", &ExecutionSessionRequest::storage_schema)
        .def_readonly("id", &ExecutionSessionRequest::id)
        .def_readonly("session_nonce", &ExecutionSessionRequest::session_nonce)
        .def_readonly("reviewed_profile_id", &ExecutionSessionRequest::reviewed_profile_id)
        .def_readonly("reviewed_profile_approval_set_id",
                      &ExecutionSessionRequest::reviewed_profile_approval_set_id)
        .def_readonly("trust_root_bundle_id", &ExecutionSessionRequest::trust_root_bundle_id)
        .def_readonly("trust_checkpoint_id", &ExecutionSessionRequest::trust_checkpoint_id)
        .def_readonly("trust_bundle_id", &ExecutionSessionRequest::trust_bundle_id)
        .def_readonly("trust_bundle_sequence", &ExecutionSessionRequest::trust_bundle_sequence)
        .def_readonly("command_sequence_id", &ExecutionSessionRequest::command_sequence_id)
        .def_readonly("atlas_id", &ExecutionSessionRequest::atlas_id)
        .def_readonly("robot_digest", &ExecutionSessionRequest::robot_digest)
        .def_readonly("scene_digest", &ExecutionSessionRequest::scene_digest)
        .def_readonly("command_count", &ExecutionSessionRequest::command_count)
        .def_readonly("controller", &ExecutionSessionRequest::controller)
        .def_readonly("runtime_monitor", &ExecutionSessionRequest::runtime_monitor)
        .def_readonly("limits", &ExecutionSessionRequest::limits)
        .def("valid", &ExecutionSessionRequest::valid);

    py::class_<ExecutionSessionApproval>(module, "ExecutionSessionApproval")
        .def_readonly("id", &ExecutionSessionApproval::id)
        .def_readonly("request_id", &ExecutionSessionApproval::request_id)
        .def_readonly("signer_service_id", &ExecutionSessionApproval::signer_service_id)
        .def_readonly("signer_key_id", &ExecutionSessionApproval::signer_key_id)
        .def_readonly("role", &ExecutionSessionApproval::role)
        .def_readonly("algorithm", &ExecutionSessionApproval::algorithm)
        .def_readonly("authentication_tag", &ExecutionSessionApproval::authentication_tag);

    py::class_<ExecutionSessionApprovalSet>(module, "ExecutionSessionApprovalSet")
        .def_readonly("id", &ExecutionSessionApprovalSet::id)
        .def_readonly("request_id", &ExecutionSessionApprovalSet::request_id)
        .def_readonly("approvals", &ExecutionSessionApprovalSet::approvals);

    py::class_<ExecutionControllerAcknowledgement>(module, "ExecutionControllerAcknowledgement")
        .def_readonly("id", &ExecutionControllerAcknowledgement::id)
        .def_readonly("request_id", &ExecutionControllerAcknowledgement::request_id)
        .def_readonly("command_sequence_id", &ExecutionControllerAcknowledgement::command_sequence_id)
        .def_readonly("controller_service_id", &ExecutionControllerAcknowledgement::controller_service_id)
        .def_readonly("controller_key_id", &ExecutionControllerAcknowledgement::controller_key_id)
        .def_readonly("accepted_command_count", &ExecutionControllerAcknowledgement::accepted_command_count)
        .def_readonly("algorithm", &ExecutionControllerAcknowledgement::algorithm)
        .def_readonly("authentication_tag", &ExecutionControllerAcknowledgement::authentication_tag);

    py::enum_<ExecutionMonitorState>(module, "ExecutionMonitorState")
        .value("ARMED_CERTIFIED_SEQUENCE", ExecutionMonitorState::ArmedCertifiedSequence)
        .value("DISARMED", ExecutionMonitorState::Disarmed)
        .value("FAULT", ExecutionMonitorState::Fault);

    py::class_<ExecutionRuntimeObservationInput>(module, "ExecutionRuntimeObservationInput")
        .def(py::init<>())
        .def_readwrite("runtime", &ExecutionRuntimeObservationInput::runtime)
        .def_readwrite("observation_sequence", &ExecutionRuntimeObservationInput::observation_sequence)
        .def_readwrite("observed_monotonic_ns", &ExecutionRuntimeObservationInput::observed_monotonic_ns)
        .def_readwrite("monitor_state", &ExecutionRuntimeObservationInput::monitor_state);

    py::class_<ExecutionRuntimeObservation>(module, "ExecutionRuntimeObservation")
        .def_static(
            "create",
            [](const ExecutionSessionRequest& request, ExecutionRuntimeObservationInput input) {
                return unwrap(ExecutionRuntimeObservation::create(request, std::move(input)));
            },
            py::arg("request"), py::arg("input"))
        .def_readonly("id", &ExecutionRuntimeObservation::id)
        .def_readonly("request_id", &ExecutionRuntimeObservation::request_id)
        .def_readonly("command_sequence_id", &ExecutionRuntimeObservation::command_sequence_id)
        .def_readonly("runtime", &ExecutionRuntimeObservation::runtime)
        .def_readonly("observation_sequence", &ExecutionRuntimeObservation::observation_sequence)
        .def_readonly("observed_monotonic_ns", &ExecutionRuntimeObservation::observed_monotonic_ns)
        .def_readonly("monitor_state", &ExecutionRuntimeObservation::monitor_state)
        .def("valid", &ExecutionRuntimeObservation::valid);

    py::class_<ExecutionMonitorAcknowledgement>(module, "ExecutionMonitorAcknowledgement")
        .def_readonly("id", &ExecutionMonitorAcknowledgement::id)
        .def_readonly("observation", &ExecutionMonitorAcknowledgement::observation)
        .def_readonly("monitor_service_id", &ExecutionMonitorAcknowledgement::monitor_service_id)
        .def_readonly("monitor_key_id", &ExecutionMonitorAcknowledgement::monitor_key_id)
        .def_readonly("algorithm", &ExecutionMonitorAcknowledgement::algorithm)
        .def_readonly("authentication_tag", &ExecutionMonitorAcknowledgement::authentication_tag);

    py::class_<ExecutionCommandAuthorization>(module, "ExecutionCommandAuthorization")
        .def_readonly("id", &ExecutionCommandAuthorization::id)
        .def_readonly("session_id", &ExecutionCommandAuthorization::session_id)
        .def_readonly("command_sequence_id", &ExecutionCommandAuthorization::command_sequence_id)
        .def_readonly("command_index", &ExecutionCommandAuthorization::command_index)
        .def_readonly("command_digest", &ExecutionCommandAuthorization::command_digest)
        .def_readonly("dispatch_monotonic_ns", &ExecutionCommandAuthorization::dispatch_monotonic_ns)
        .def_readonly("valid_from_monotonic_ns", &ExecutionCommandAuthorization::valid_from_monotonic_ns)
        .def_readonly("valid_through_monotonic_ns",
                      &ExecutionCommandAuthorization::valid_through_monotonic_ns)
        .def_readonly("evidence", &ExecutionCommandAuthorization::evidence)
        .def("valid", &ExecutionCommandAuthorization::valid)
        .def_property_readonly("open_ended", &ExecutionCommandAuthorization::open_ended);

    py::class_<BoundedExecutionSessionLoadOptions>(module, "BoundedExecutionSessionLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_commands", &BoundedExecutionSessionLoadOptions::maximum_commands)
        .def_readwrite("maximum_dimension", &BoundedExecutionSessionLoadOptions::maximum_dimension)
        .def_readwrite("maximum_region_sequence",
                       &BoundedExecutionSessionLoadOptions::maximum_region_sequence)
        .def_readwrite("maximum_approvals", &BoundedExecutionSessionLoadOptions::maximum_approvals)
        .def_readwrite("maximum_payload_bytes", &BoundedExecutionSessionLoadOptions::maximum_payload_bytes);

    py::class_<BoundedExecutionSession>(module, "BoundedExecutionSession")
        .def_static(
            "create",
            [](ExecutionSessionRequest request, ExecutionCommandSequence sequence,
               ExecutionSessionApprovalSet approvals, ExecutionControllerAcknowledgement controller,
               ExecutionMonitorAcknowledgement monitor, const ReviewedDeploymentProfile& reviewed,
               const ServiceTrustBundle& bundle, const SafeAtlas& atlas) {
                return unwrap(BoundedExecutionSession::create(std::move(request), std::move(sequence),
                                                              std::move(approvals), std::move(controller),
                                                              std::move(monitor), reviewed, bundle, atlas));
            },
            py::arg("request"), py::arg("command_sequence"), py::arg("approval_set"),
            py::arg("controller_acknowledgement"), py::arg("monitor_acknowledgement"),
            py::arg("reviewed_profile"), py::arg("trust_bundle"), py::arg("atlas"))
        .def_property_readonly("id", &BoundedExecutionSession::id)
        .def_property_readonly("request", &BoundedExecutionSession::request)
        .def_property_readonly("command_sequence", &BoundedExecutionSession::command_sequence)
        .def_property_readonly("approval_set", &BoundedExecutionSession::approval_set)
        .def_property_readonly("controller_acknowledgement",
                               &BoundedExecutionSession::controller_acknowledgement)
        .def_property_readonly("monitor_acknowledgement", &BoundedExecutionSession::monitor_acknowledgement)
        .def_property_readonly("valid_from_monotonic_ns", &BoundedExecutionSession::valid_from_monotonic_ns)
        .def_property_readonly("start_deadline_monotonic_ns",
                               &BoundedExecutionSession::start_deadline_monotonic_ns)
        .def_property_readonly("valid_through_monotonic_ns",
                               &BoundedExecutionSession::valid_through_monotonic_ns)
        .def("valid", &BoundedExecutionSession::valid)
        .def_property_readonly("evidence", &BoundedExecutionSession::evidence)
        .def_property_readonly("authorizes_execution", &BoundedExecutionSession::authorizes_execution)
        .def(
            "authorize_command",
            [](const BoundedExecutionSession& session, std::uint64_t command_index,
               const Configuration& configuration, std::uint64_t dispatch_monotonic_ns) {
                return unwrap(
                    session.authorize_command(command_index, view(configuration), dispatch_monotonic_ns));
            },
            py::arg("command_index"), py::arg("configuration"), py::arg("dispatch_monotonic_ns"))
        .def(
            "save",
            [](const BoundedExecutionSession& session, const std::filesystem::path& path,
               const SaveOptions& options) { unwrap_void(session.save(path, options)); },
            py::arg("path"), py::arg("options") = SaveOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& path, const ReviewedDeploymentProfile& reviewed,
               const ServiceTrustHistory& history, const ServiceTrustCheckpoint& checkpoint,
               const std::string& expected_checkpoint_id, const SafeAtlas& atlas,
               const BoundedExecutionSessionLoadOptions& options) {
                return unwrap(BoundedExecutionSession::load(path, reviewed, history, checkpoint,
                                                            expected_checkpoint_id, atlas, options));
            },
            py::arg("path"), py::arg("reviewed_profile"), py::arg("trust_history"),
            py::arg("trust_checkpoint"), py::arg("expected_checkpoint_id"), py::arg("atlas"),
            py::arg("options") = BoundedExecutionSessionLoadOptions{});

    module.def("valid_execution_endpoint_key", &valid_execution_endpoint_key);
    module.def(
        "make_execution_endpoint_key",
        [](std::string service_id, ExecutionEndpointRole role, const py::bytes& public_key) {
            const auto copy = static_cast<std::string>(public_key);
            return unwrap(
                rbfsafe::make_execution_endpoint_key(std::move(service_id), role, bytes_view(copy)));
        },
        py::arg("service_id"), py::arg("role"), py::arg("ed25519_public_key"));
    module.def("valid_execution_session_limits", &valid_execution_session_limits);
    module.def("valid_execution_session_approval", &valid_execution_session_approval);
    module.def("valid_execution_session_approval_set", &valid_execution_session_approval_set);
    module.def(
        "sign_execution_session_approval",
        [](const ExecutionSessionRequest& request, const DeploymentProfileApproval& profile_approval,
           const py::bytes& secret_key) {
            const SensitiveBytes copy(secret_key);
            return unwrap(rbfsafe::sign_execution_session_approval(request, profile_approval, copy.view()));
        },
        py::arg("request"), py::arg("profile_approval"), py::arg("ed25519_secret_key"));
    module.def(
        "assemble_execution_session_approvals",
        [](const ExecutionSessionRequest& request, const ReviewedDeploymentProfile& reviewed,
           std::vector<ExecutionSessionApproval> approvals) {
            return unwrap(
                rbfsafe::assemble_execution_session_approvals(request, reviewed, std::move(approvals)));
        },
        py::arg("request"), py::arg("reviewed_profile"), py::arg("approvals"));
    module.def(
        "verify_execution_session_approvals",
        [](const ExecutionSessionRequest& request, const ReviewedDeploymentProfile& reviewed,
           const ExecutionSessionApprovalSet& approvals, const ServiceTrustBundle& bundle) {
            unwrap_void(rbfsafe::verify_execution_session_approvals(request, reviewed, approvals, bundle));
        },
        py::arg("request"), py::arg("reviewed_profile"), py::arg("approval_set"), py::arg("trust_bundle"));
    module.def(
        "sign_execution_controller_acknowledgement",
        [](const ExecutionSessionRequest& request, const py::bytes& secret_key) {
            const SensitiveBytes copy(secret_key);
            return unwrap(rbfsafe::sign_execution_controller_acknowledgement(request, copy.view()));
        },
        py::arg("request"), py::arg("ed25519_secret_key"));
    module.def(
        "verify_execution_controller_acknowledgement",
        [](const ExecutionSessionRequest& request,
           const ExecutionControllerAcknowledgement& acknowledgement) {
            unwrap_void(rbfsafe::verify_execution_controller_acknowledgement(request, acknowledgement));
        },
        py::arg("request"), py::arg("acknowledgement"));
    module.def(
        "sign_execution_monitor_acknowledgement",
        [](const ExecutionSessionRequest& request, ExecutionRuntimeObservation observation,
           const py::bytes& secret_key) {
            const SensitiveBytes copy(secret_key);
            return unwrap(rbfsafe::sign_execution_monitor_acknowledgement(request, std::move(observation),
                                                                          copy.view()));
        },
        py::arg("request"), py::arg("observation"), py::arg("ed25519_secret_key"));
    module.def(
        "verify_execution_monitor_acknowledgement",
        [](const ExecutionSessionRequest& request, const ExecutionMonitorAcknowledgement& acknowledgement) {
            unwrap_void(rbfsafe::verify_execution_monitor_acknowledgement(request, acknowledgement));
        },
        py::arg("request"), py::arg("acknowledgement"));
    module.def("execution_endpoint_role_name", &execution_endpoint_role_name);
    module.def("execution_monitor_state_name", &execution_monitor_state_name);
}

} // namespace rbfsafe::python_binding
