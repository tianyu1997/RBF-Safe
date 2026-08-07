#include "binding_support.h"

#include <rbfsafe/modules/assurance.h>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

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

} // namespace

void bind_deployment(py::module_& module) {
    py::enum_<DeploymentReviewRole>(module, "DeploymentReviewRole")
        .value("SAFETY", DeploymentReviewRole::Safety)
        .value("CONTROLS", DeploymentReviewRole::Controls)
        .value("OPERATIONS", DeploymentReviewRole::Operations)
        .value("SECURITY", DeploymentReviewRole::Security);

    py::class_<DeploymentReviewPolicy>(module, "DeploymentReviewPolicy")
        .def(py::init<>())
        .def_readwrite("minimum_approvals", &DeploymentReviewPolicy::minimum_approvals)
        .def_readwrite("require_distinct_services", &DeploymentReviewPolicy::require_distinct_services)
        .def_readwrite("required_roles", &DeploymentReviewPolicy::required_roles);

    py::class_<DeploymentRuntimeConstraints>(module, "DeploymentRuntimeConstraints")
        .def(py::init<>())
        .def_readwrite("maximum_observation_age_ns",
                       &DeploymentRuntimeConstraints::maximum_observation_age_ns)
        .def_readwrite("maximum_command_latency_ns",
                       &DeploymentRuntimeConstraints::maximum_command_latency_ns)
        .def_readwrite("maximum_control_period_ns", &DeploymentRuntimeConstraints::maximum_control_period_ns)
        .def_readwrite("maximum_consecutive_missed_cycles",
                       &DeploymentRuntimeConstraints::maximum_consecutive_missed_cycles)
        .def_readwrite("require_runtime_monitor", &DeploymentRuntimeConstraints::require_runtime_monitor)
        .def_readwrite("require_fail_closed_transport",
                       &DeploymentRuntimeConstraints::require_fail_closed_transport)
        .def_readwrite("require_authenticated_artifacts",
                       &DeploymentRuntimeConstraints::require_authenticated_artifacts);

    py::class_<DeploymentProfileInput>(module, "DeploymentProfileInput")
        .def(py::init<>())
        .def_readwrite("deployment_id", &DeploymentProfileInput::deployment_id)
        .def_readwrite("robot_digest", &DeploymentProfileInput::robot_digest)
        .def_readwrite("controller_digest", &DeploymentProfileInput::controller_digest)
        .def_readwrite("platform_digest", &DeploymentProfileInput::platform_digest)
        .def_readwrite("runtime_digest", &DeploymentProfileInput::runtime_digest)
        .def_readwrite("trust_root_bundle_id", &DeploymentProfileInput::trust_root_bundle_id)
        .def_readwrite("trust_checkpoint_id", &DeploymentProfileInput::trust_checkpoint_id)
        .def_readwrite("trust_bundle_id", &DeploymentProfileInput::trust_bundle_id)
        .def_readwrite("trust_bundle_sequence", &DeploymentProfileInput::trust_bundle_sequence)
        .def_readwrite("runtime_constraints", &DeploymentProfileInput::runtime_constraints)
        .def_readwrite("review_policy", &DeploymentProfileInput::review_policy);

    py::class_<DeploymentProfile>(module, "DeploymentProfile")
        .def_static(
            "create",
            [](DeploymentProfileInput input) { return unwrap(DeploymentProfile::create(std::move(input))); },
            py::arg("input"))
        .def_readonly("storage_schema", &DeploymentProfile::storage_schema)
        .def_readonly("id", &DeploymentProfile::id)
        .def_readonly("deployment_id", &DeploymentProfile::deployment_id)
        .def_readonly("robot_digest", &DeploymentProfile::robot_digest)
        .def_readonly("controller_digest", &DeploymentProfile::controller_digest)
        .def_readonly("platform_digest", &DeploymentProfile::platform_digest)
        .def_readonly("runtime_digest", &DeploymentProfile::runtime_digest)
        .def_readonly("trust_root_bundle_id", &DeploymentProfile::trust_root_bundle_id)
        .def_readonly("trust_checkpoint_id", &DeploymentProfile::trust_checkpoint_id)
        .def_readonly("trust_bundle_id", &DeploymentProfile::trust_bundle_id)
        .def_readonly("trust_bundle_sequence", &DeploymentProfile::trust_bundle_sequence)
        .def_readonly("runtime_constraints", &DeploymentProfile::runtime_constraints)
        .def_readonly("review_policy", &DeploymentProfile::review_policy)
        .def("valid", &DeploymentProfile::valid);

    py::class_<DeploymentProfileApproval>(module, "DeploymentProfileApproval")
        .def_readonly("id", &DeploymentProfileApproval::id)
        .def_readonly("profile_id", &DeploymentProfileApproval::profile_id)
        .def_readonly("signer_service_id", &DeploymentProfileApproval::signer_service_id)
        .def_readonly("signer_key_id", &DeploymentProfileApproval::signer_key_id)
        .def_readonly("role", &DeploymentProfileApproval::role)
        .def_readonly("algorithm", &DeploymentProfileApproval::algorithm)
        .def_readonly("authentication_tag", &DeploymentProfileApproval::authentication_tag);

    py::class_<DeploymentProfileApprovalSet>(module, "DeploymentProfileApprovalSet")
        .def_readonly("id", &DeploymentProfileApprovalSet::id)
        .def_readonly("profile_id", &DeploymentProfileApprovalSet::profile_id)
        .def_readonly("approvals", &DeploymentProfileApprovalSet::approvals);

    py::class_<DeploymentRuntimeSnapshot>(module, "DeploymentRuntimeSnapshot")
        .def(py::init<>())
        .def_readwrite("deployment_id", &DeploymentRuntimeSnapshot::deployment_id)
        .def_readwrite("robot_digest", &DeploymentRuntimeSnapshot::robot_digest)
        .def_readwrite("controller_digest", &DeploymentRuntimeSnapshot::controller_digest)
        .def_readwrite("platform_digest", &DeploymentRuntimeSnapshot::platform_digest)
        .def_readwrite("runtime_digest", &DeploymentRuntimeSnapshot::runtime_digest)
        .def_readwrite("observation_age_ns", &DeploymentRuntimeSnapshot::observation_age_ns)
        .def_readwrite("command_latency_ns", &DeploymentRuntimeSnapshot::command_latency_ns)
        .def_readwrite("control_period_ns", &DeploymentRuntimeSnapshot::control_period_ns)
        .def_readwrite("consecutive_missed_cycles", &DeploymentRuntimeSnapshot::consecutive_missed_cycles)
        .def_readwrite("runtime_monitor_active", &DeploymentRuntimeSnapshot::runtime_monitor_active)
        .def_readwrite("fail_closed_transport_active",
                       &DeploymentRuntimeSnapshot::fail_closed_transport_active)
        .def_readwrite("authenticated_artifacts", &DeploymentRuntimeSnapshot::authenticated_artifacts);

    py::enum_<DeploymentConstraintViolation>(module, "DeploymentConstraintViolation")
        .value("DEPLOYMENT_IDENTITY_MISMATCH", DeploymentConstraintViolation::DeploymentIdentityMismatch)
        .value("ROBOT_IDENTITY_MISMATCH", DeploymentConstraintViolation::RobotIdentityMismatch)
        .value("CONTROLLER_IDENTITY_MISMATCH", DeploymentConstraintViolation::ControllerIdentityMismatch)
        .value("PLATFORM_IDENTITY_MISMATCH", DeploymentConstraintViolation::PlatformIdentityMismatch)
        .value("RUNTIME_IDENTITY_MISMATCH", DeploymentConstraintViolation::RuntimeIdentityMismatch)
        .value("OBSERVATION_AGE_EXCEEDED", DeploymentConstraintViolation::ObservationAgeExceeded)
        .value("COMMAND_LATENCY_EXCEEDED", DeploymentConstraintViolation::CommandLatencyExceeded)
        .value("CONTROL_PERIOD_EXCEEDED", DeploymentConstraintViolation::ControlPeriodExceeded)
        .value("MISSED_CYCLE_LIMIT_EXCEEDED", DeploymentConstraintViolation::MissedCycleLimitExceeded)
        .value("RUNTIME_MONITOR_REQUIRED", DeploymentConstraintViolation::RuntimeMonitorRequired)
        .value("FAIL_CLOSED_TRANSPORT_REQUIRED", DeploymentConstraintViolation::FailClosedTransportRequired)
        .value("AUTHENTICATED_ARTIFACTS_REQUIRED",
               DeploymentConstraintViolation::AuthenticatedArtifactsRequired);

    py::enum_<DeploymentProfileAssessmentStatus>(module, "DeploymentProfileAssessmentStatus")
        .value("CONFORMANT", DeploymentProfileAssessmentStatus::Conformant)
        .value("NONCONFORMANT", DeploymentProfileAssessmentStatus::Nonconformant);

    py::class_<DeploymentProfileAssessment>(module, "DeploymentProfileAssessment")
        .def_readonly("id", &DeploymentProfileAssessment::id)
        .def_readonly("profile_id", &DeploymentProfileAssessment::profile_id)
        .def_readonly("approval_set_id", &DeploymentProfileAssessment::approval_set_id)
        .def_readonly("runtime_snapshot_id", &DeploymentProfileAssessment::runtime_snapshot_id)
        .def_readonly("status", &DeploymentProfileAssessment::status)
        .def_readonly("violations", &DeploymentProfileAssessment::violations)
        .def_readonly("evidence", &DeploymentProfileAssessment::evidence)
        .def("valid", &DeploymentProfileAssessment::valid)
        .def_property_readonly("authorizes_execution", &DeploymentProfileAssessment::authorizes_execution);

    py::class_<ReviewedDeploymentProfileLoadOptions>(module, "ReviewedDeploymentProfileLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_approvals", &ReviewedDeploymentProfileLoadOptions::maximum_approvals)
        .def_readwrite("maximum_required_roles",
                       &ReviewedDeploymentProfileLoadOptions::maximum_required_roles)
        .def_readwrite("maximum_payload_bytes", &ReviewedDeploymentProfileLoadOptions::maximum_payload_bytes);

    py::class_<ReviewedDeploymentProfile>(module, "ReviewedDeploymentProfile")
        .def_static(
            "create",
            [](DeploymentProfile profile, DeploymentProfileApprovalSet approval_set,
               const ServiceTrustHistory& history, const ServiceTrustCheckpoint& checkpoint,
               const std::string& expected_checkpoint_id) {
                return unwrap(ReviewedDeploymentProfile::create(std::move(profile), std::move(approval_set),
                                                                history, checkpoint, expected_checkpoint_id));
            },
            py::arg("profile"), py::arg("approval_set"), py::arg("trust_history"),
            py::arg("trust_checkpoint"), py::arg("expected_checkpoint_id"))
        .def_property_readonly("profile", &ReviewedDeploymentProfile::profile)
        .def_property_readonly("approval_set", &ReviewedDeploymentProfile::approval_set)
        .def("valid", &ReviewedDeploymentProfile::valid)
        .def_property_readonly("authorizes_execution", &ReviewedDeploymentProfile::authorizes_execution)
        .def(
            "assess",
            [](const ReviewedDeploymentProfile& reviewed, const DeploymentRuntimeSnapshot& snapshot) {
                return unwrap(reviewed.assess(snapshot));
            },
            py::arg("snapshot"))
        .def(
            "save",
            [](const ReviewedDeploymentProfile& reviewed, const std::filesystem::path& path,
               const SaveOptions& options) { unwrap_void(reviewed.save(path, options)); },
            py::arg("path"), py::arg("options") = SaveOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& path, const ServiceTrustHistory& history,
               const ServiceTrustCheckpoint& checkpoint, const std::string& expected_checkpoint_id,
               const ReviewedDeploymentProfileLoadOptions& options) {
                return unwrap(ReviewedDeploymentProfile::load(path, history, checkpoint,
                                                              expected_checkpoint_id, options));
            },
            py::arg("path"), py::arg("trust_history"), py::arg("trust_checkpoint"),
            py::arg("expected_checkpoint_id"), py::arg("options") = ReviewedDeploymentProfileLoadOptions{});

    module.def("valid_deployment_review_policy", &valid_deployment_review_policy);
    module.def("valid_deployment_runtime_constraints", &valid_deployment_runtime_constraints);
    module.def("valid_deployment_profile_approval", &valid_deployment_profile_approval);
    module.def("valid_deployment_profile_approval_set", &valid_deployment_profile_approval_set);
    module.def("valid_deployment_runtime_snapshot", &valid_deployment_runtime_snapshot);

    module.def(
        "sign_deployment_profile_approval",
        [](const DeploymentProfile& profile, std::string signer_service_id, std::string signer_key_id,
           DeploymentReviewRole role, const py::bytes& secret_key) {
            const SensitiveBytes secret_copy(secret_key);
            return unwrap(rbfsafe::sign_deployment_profile_approval(
                profile, std::move(signer_service_id), std::move(signer_key_id), role, secret_copy.view()));
        },
        py::arg("profile"), py::arg("signer_service_id"), py::arg("signer_key_id"), py::arg("role"),
        py::arg("ed25519_secret_key"));

    module.def(
        "assemble_deployment_profile_approvals",
        [](const DeploymentProfile& profile, std::vector<DeploymentProfileApproval> approvals) {
            return unwrap(rbfsafe::assemble_deployment_profile_approvals(profile, std::move(approvals)));
        },
        py::arg("profile"), py::arg("approvals"));

    module.def(
        "verify_deployment_profile_approvals",
        [](const DeploymentProfile& profile, const DeploymentProfileApprovalSet& approval_set,
           const ServiceTrustBundle& trust_bundle) {
            unwrap_void(rbfsafe::verify_deployment_profile_approvals(profile, approval_set, trust_bundle));
        },
        py::arg("profile"), py::arg("approval_set"), py::arg("trust_bundle"));

    module.def("deployment_review_role_name", &deployment_review_role_name);
    module.def("deployment_constraint_violation_name", &deployment_constraint_violation_name);
    module.def("deployment_profile_assessment_status_name", &deployment_profile_assessment_status_name);
}

} // namespace rbfsafe::python_binding
