#include "binding_support.h"

#include <rbfsafe/calibration.h>

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace rbfsafe::python_binding {

void bind_calibration(py::module_& module) {
    py::class_<PolicyCalibrationBinInput>(module, "PolicyCalibrationBinInput")
        .def(py::init<>())
        .def(py::init<double, double, double, std::uint64_t, std::uint64_t>(), py::arg("lower_confidence"),
             py::arg("upper_confidence"), py::arg("mean_confidence"), py::arg("samples"),
             py::arg("successes"))
        .def_readwrite("lower_confidence", &PolicyCalibrationBinInput::lower_confidence)
        .def_readwrite("upper_confidence", &PolicyCalibrationBinInput::upper_confidence)
        .def_readwrite("mean_confidence", &PolicyCalibrationBinInput::mean_confidence)
        .def_readwrite("samples", &PolicyCalibrationBinInput::samples)
        .def_readwrite("successes", &PolicyCalibrationBinInput::successes);

    py::class_<PolicyCalibrationProfileInput>(module, "PolicyCalibrationProfileInput")
        .def(py::init<>())
        .def_readwrite("policy_id", &PolicyCalibrationProfileInput::policy_id)
        .def_readwrite("policy_model_digest", &PolicyCalibrationProfileInput::policy_model_digest)
        .def_readwrite("scope_id", &PolicyCalibrationProfileInput::scope_id)
        .def_readwrite("task_id", &PolicyCalibrationProfileInput::task_id)
        .def_readwrite("dataset_digest", &PolicyCalibrationProfileInput::dataset_digest)
        .def_readwrite("method", &PolicyCalibrationProfileInput::method)
        .def_readwrite("method_version", &PolicyCalibrationProfileInput::method_version)
        .def_readwrite("outcome_definition", &PolicyCalibrationProfileInput::outcome_definition)
        .def_readwrite("state_uncertainty_unit", &PolicyCalibrationProfileInput::state_uncertainty_unit)
        .def_readwrite("action_uncertainty_unit", &PolicyCalibrationProfileInput::action_uncertainty_unit)
        .def_readwrite("bins", &PolicyCalibrationProfileInput::bins);

    py::class_<PolicyCalibrationBin>(module, "PolicyCalibrationBin")
        .def_readonly("lower_confidence", &PolicyCalibrationBin::lower_confidence)
        .def_readonly("upper_confidence", &PolicyCalibrationBin::upper_confidence)
        .def_readonly("mean_confidence", &PolicyCalibrationBin::mean_confidence)
        .def_readonly("samples", &PolicyCalibrationBin::samples)
        .def_readonly("successes", &PolicyCalibrationBin::successes)
        .def_readonly("observed_success_rate", &PolicyCalibrationBin::observed_success_rate)
        .def_readonly("lower_confidence_bound_95", &PolicyCalibrationBin::lower_confidence_bound_95)
        .def_readonly("absolute_calibration_error", &PolicyCalibrationBin::absolute_calibration_error);

    py::class_<PolicyCalibrationLookup>(module, "PolicyCalibrationLookup")
        .def_readonly("profile_id", &PolicyCalibrationLookup::profile_id)
        .def_readonly("bin_index", &PolicyCalibrationLookup::bin_index)
        .def_readonly("raw_confidence", &PolicyCalibrationLookup::raw_confidence)
        .def_readonly("calibrated_confidence", &PolicyCalibrationLookup::calibrated_confidence)
        .def_readonly("conservative_confidence", &PolicyCalibrationLookup::conservative_confidence)
        .def_readonly("samples", &PolicyCalibrationLookup::samples);

    py::class_<PolicyCalibrationLoadOptions>(module, "PolicyCalibrationLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_bins", &PolicyCalibrationLoadOptions::maximum_bins)
        .def_readwrite("maximum_payload_bytes", &PolicyCalibrationLoadOptions::maximum_payload_bytes);

    py::class_<PolicyCalibrationProfile>(module, "PolicyCalibrationProfile")
        .def_static(
            "create",
            [](PolicyCalibrationProfileInput input) {
                return unwrap(PolicyCalibrationProfile::create(std::move(input)));
            },
            py::arg("input"))
        .def_property_readonly("id", &PolicyCalibrationProfile::id)
        .def_property_readonly("policy_id", &PolicyCalibrationProfile::policy_id)
        .def_property_readonly("policy_model_digest", &PolicyCalibrationProfile::policy_model_digest)
        .def_property_readonly("scope_id", &PolicyCalibrationProfile::scope_id)
        .def_property_readonly("task_id", &PolicyCalibrationProfile::task_id)
        .def_property_readonly("dataset_digest", &PolicyCalibrationProfile::dataset_digest)
        .def_property_readonly("method", &PolicyCalibrationProfile::method)
        .def_property_readonly("method_version", &PolicyCalibrationProfile::method_version)
        .def_property_readonly("outcome_definition", &PolicyCalibrationProfile::outcome_definition)
        .def_property_readonly("state_uncertainty_unit", &PolicyCalibrationProfile::state_uncertainty_unit)
        .def_property_readonly("action_uncertainty_unit", &PolicyCalibrationProfile::action_uncertainty_unit)
        .def_property_readonly("bins", &PolicyCalibrationProfile::bins)
        .def_property_readonly("sample_count", &PolicyCalibrationProfile::sample_count)
        .def_property_readonly("expected_calibration_error",
                               &PolicyCalibrationProfile::expected_calibration_error)
        .def_property_readonly("maximum_calibration_error",
                               &PolicyCalibrationProfile::maximum_calibration_error)
        .def("valid", &PolicyCalibrationProfile::valid)
        .def(
            "lookup",
            [](const PolicyCalibrationProfile& profile, double confidence) {
                return unwrap(profile.lookup(confidence));
            },
            py::arg("raw_confidence"))
        .def(
            "save",
            [](const PolicyCalibrationProfile& profile, const std::filesystem::path& path,
               const SaveOptions& options) { unwrap_void(profile.save(path, options)); },
            py::arg("path"), py::arg("options") = SaveOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& path, const PolicyCalibrationLoadOptions& options) {
                return unwrap(PolicyCalibrationProfile::load(path, options));
            },
            py::arg("path"), py::arg("options") = PolicyCalibrationLoadOptions{});

    py::class_<CalibratedPolicyGateOptions>(module, "CalibratedPolicyGateOptions")
        .def(py::init<>())
        .def_readwrite("minimum_total_samples", &CalibratedPolicyGateOptions::minimum_total_samples)
        .def_readwrite("minimum_bin_samples", &CalibratedPolicyGateOptions::minimum_bin_samples)
        .def_readwrite("maximum_expected_calibration_error",
                       &CalibratedPolicyGateOptions::maximum_expected_calibration_error)
        .def_readwrite("maximum_bin_calibration_error",
                       &CalibratedPolicyGateOptions::maximum_bin_calibration_error)
        .def_readwrite("policy", &CalibratedPolicyGateOptions::policy);

    py::class_<CalibratedPolicyApplication>(module, "CalibratedPolicyApplication")
        .def_readonly("id", &CalibratedPolicyApplication::id)
        .def_readonly("profile_id", &CalibratedPolicyApplication::profile_id)
        .def_readonly("raw_metadata", &CalibratedPolicyApplication::raw_metadata)
        .def_readonly("effective_metadata", &CalibratedPolicyApplication::effective_metadata)
        .def_readonly("bin_index", &CalibratedPolicyApplication::bin_index)
        .def_readonly("bin_samples", &CalibratedPolicyApplication::bin_samples)
        .def_readonly("calibrated_confidence", &CalibratedPolicyApplication::calibrated_confidence)
        .def_readonly("conservative_confidence", &CalibratedPolicyApplication::conservative_confidence);

    py::class_<CalibratedPolicyBatchReport>(module, "CalibratedPolicyBatchReport")
        .def_readonly("profile_id", &CalibratedPolicyBatchReport::profile_id)
        .def_readonly("applications", &CalibratedPolicyBatchReport::applications)
        .def_readonly("policy_report", &CalibratedPolicyBatchReport::policy_report);

    py::class_<CalibratedPolicySafetyGate>(module, "CalibratedPolicySafetyGate")
        .def(py::init<>())
        .def(
            "check_proposals",
            [](CalibratedPolicySafetyGate& gate, const PolicyCalibrationProfile& profile,
               std::string expected_scope_id, std::string expected_policy_model_digest,
               const SerialRobotModel& robot, const SceneSnapshot& scene, const SafeAtlas& atlas,
               const Configuration& current, const std::vector<PolicyProposal>& proposals,
               const CalibratedPolicyGateOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return gate.check_proposals(profile, expected_scope_id, expected_policy_model_digest,
                                                robot, scene, atlas, view(current), proposals, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("profile"), py::arg("expected_scope_id"), py::arg("expected_policy_model_digest"),
            py::arg("robot"), py::arg("scene"), py::arg("atlas"), py::arg("current"), py::arg("proposals"),
            py::arg("options") = CalibratedPolicyGateOptions{})
        .def_property_readonly("telemetry", &CalibratedPolicySafetyGate::telemetry)
        .def("reset_telemetry", &CalibratedPolicySafetyGate::reset_telemetry);
}

} // namespace rbfsafe::python_binding
