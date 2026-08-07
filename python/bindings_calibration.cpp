#include "binding_support.h"

#include <rbfsafe/modules/applications.h>

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

    py::enum_<PolicyCalibrationDriftStatus>(module, "PolicyCalibrationDriftStatus")
        .value("INSUFFICIENT_DATA", PolicyCalibrationDriftStatus::InsufficientData)
        .value("STABLE", PolicyCalibrationDriftStatus::Stable)
        .value("DRIFT_DETECTED", PolicyCalibrationDriftStatus::DriftDetected);

    py::enum_<PolicyCalibrationDriftReason>(module, "PolicyCalibrationDriftReason")
        .value("INSUFFICIENT_TOTAL_SAMPLES", PolicyCalibrationDriftReason::InsufficientTotalSamples)
        .value("INSUFFICIENT_BIN_SAMPLES", PolicyCalibrationDriftReason::InsufficientBinSamples)
        .value("CONFIDENCE_DISTRIBUTION_SHIFT", PolicyCalibrationDriftReason::ConfidenceDistributionShift)
        .value("EXPECTED_CALIBRATION_ERROR_EXCEEDED",
               PolicyCalibrationDriftReason::ExpectedCalibrationErrorExceeded)
        .value("OVERALL_SUCCESS_RATE_DROP_EXCEEDED",
               PolicyCalibrationDriftReason::OverallSuccessRateDropExceeded)
        .value("BIN_SUCCESS_RATE_DROP_EXCEEDED", PolicyCalibrationDriftReason::BinSuccessRateDropExceeded);

    py::class_<PolicyCalibrationWindowBinInput>(module, "PolicyCalibrationWindowBinInput")
        .def(py::init<>())
        .def(py::init<std::uint64_t, std::uint64_t>(), py::arg("samples"), py::arg("successes"))
        .def_readwrite("samples", &PolicyCalibrationWindowBinInput::samples)
        .def_readwrite("successes", &PolicyCalibrationWindowBinInput::successes);

    py::class_<PolicyCalibrationWindowInput>(module, "PolicyCalibrationWindowInput")
        .def(py::init<>())
        .def_readwrite("window_id", &PolicyCalibrationWindowInput::window_id)
        .def_readwrite("sequence", &PolicyCalibrationWindowInput::sequence)
        .def_readwrite("source_digest", &PolicyCalibrationWindowInput::source_digest)
        .def_readwrite("bins", &PolicyCalibrationWindowInput::bins);

    py::class_<PolicyCalibrationDriftOptions>(module, "PolicyCalibrationDriftOptions")
        .def(py::init<>())
        .def_readwrite("minimum_total_samples", &PolicyCalibrationDriftOptions::minimum_total_samples)
        .def_readwrite("minimum_bin_samples", &PolicyCalibrationDriftOptions::minimum_bin_samples)
        .def_readwrite("maximum_total_variation_distance",
                       &PolicyCalibrationDriftOptions::maximum_total_variation_distance)
        .def_readwrite("maximum_expected_calibration_error",
                       &PolicyCalibrationDriftOptions::maximum_expected_calibration_error)
        .def_readwrite("maximum_overall_success_rate_drop",
                       &PolicyCalibrationDriftOptions::maximum_overall_success_rate_drop)
        .def_readwrite("maximum_bin_success_rate_drop",
                       &PolicyCalibrationDriftOptions::maximum_bin_success_rate_drop);

    py::class_<PolicyCalibrationWindowBin>(module, "PolicyCalibrationWindowBin")
        .def_readonly("samples", &PolicyCalibrationWindowBin::samples)
        .def_readonly("successes", &PolicyCalibrationWindowBin::successes)
        .def_readonly("baseline_fraction", &PolicyCalibrationWindowBin::baseline_fraction)
        .def_readonly("observed_fraction", &PolicyCalibrationWindowBin::observed_fraction)
        .def_readonly("baseline_success_rate", &PolicyCalibrationWindowBin::baseline_success_rate)
        .def_readonly("outcome_rate_available", &PolicyCalibrationWindowBin::outcome_rate_available)
        .def_readonly("observed_success_rate", &PolicyCalibrationWindowBin::observed_success_rate)
        .def_readonly("success_rate_drop", &PolicyCalibrationWindowBin::success_rate_drop)
        .def_readonly("absolute_calibration_error", &PolicyCalibrationWindowBin::absolute_calibration_error);

    py::class_<PolicyCalibrationDriftReport>(module, "PolicyCalibrationDriftReport")
        .def_readonly("id", &PolicyCalibrationDriftReport::id)
        .def_readonly("profile_id", &PolicyCalibrationDriftReport::profile_id)
        .def_readonly("window_id", &PolicyCalibrationDriftReport::window_id)
        .def_readonly("window_sequence", &PolicyCalibrationDriftReport::window_sequence)
        .def_readonly("source_digest", &PolicyCalibrationDriftReport::source_digest)
        .def_readonly("options", &PolicyCalibrationDriftReport::options)
        .def_readonly("status", &PolicyCalibrationDriftReport::status)
        .def_readonly("reasons", &PolicyCalibrationDriftReport::reasons)
        .def_readonly("sample_count", &PolicyCalibrationDriftReport::sample_count)
        .def_readonly("total_variation_distance", &PolicyCalibrationDriftReport::total_variation_distance)
        .def_readonly("baseline_success_rate", &PolicyCalibrationDriftReport::baseline_success_rate)
        .def_readonly("observed_success_rate", &PolicyCalibrationDriftReport::observed_success_rate)
        .def_readonly("overall_success_rate_drop", &PolicyCalibrationDriftReport::overall_success_rate_drop)
        .def_readonly("expected_calibration_error", &PolicyCalibrationDriftReport::expected_calibration_error)
        .def_readonly("maximum_calibration_error", &PolicyCalibrationDriftReport::maximum_calibration_error)
        .def_readonly("maximum_bin_success_rate_drop",
                      &PolicyCalibrationDriftReport::maximum_bin_success_rate_drop)
        .def_readonly("bins", &PolicyCalibrationDriftReport::bins);

    module.def(
        "assess_policy_calibration_drift",
        [](const PolicyCalibrationProfile& profile, PolicyCalibrationWindowInput window,
           const PolicyCalibrationDriftOptions& options) {
            return unwrap(assess_policy_calibration_drift(profile, std::move(window), options));
        },
        py::arg("profile"), py::arg("window"), py::arg("options") = PolicyCalibrationDriftOptions{});

    py::enum_<PolicyCalibrationLifecycleState>(module, "PolicyCalibrationLifecycleState")
        .value("PENDING_REVIEW", PolicyCalibrationLifecycleState::PendingReview)
        .value("ACTIVE", PolicyCalibrationLifecycleState::Active)
        .value("QUARANTINED", PolicyCalibrationLifecycleState::Quarantined)
        .value("RETIRED", PolicyCalibrationLifecycleState::Retired);

    py::enum_<PolicyCalibrationLifecycleEventType>(module, "PolicyCalibrationLifecycleEventType")
        .value("REGISTERED", PolicyCalibrationLifecycleEventType::Registered)
        .value("DRIFT_ASSESSED", PolicyCalibrationLifecycleEventType::DriftAssessed)
        .value("STATE_TRANSITION", PolicyCalibrationLifecycleEventType::StateTransition);

    py::class_<PolicyCalibrationLifecycleEvent>(module, "PolicyCalibrationLifecycleEvent")
        .def_readonly("id", &PolicyCalibrationLifecycleEvent::id)
        .def_readonly("parent_id", &PolicyCalibrationLifecycleEvent::parent_id)
        .def_readonly("sequence", &PolicyCalibrationLifecycleEvent::sequence)
        .def_readonly("type", &PolicyCalibrationLifecycleEvent::type)
        .def_readonly("previous_state", &PolicyCalibrationLifecycleEvent::previous_state)
        .def_readonly("current_state", &PolicyCalibrationLifecycleEvent::current_state)
        .def_readonly("report_id", &PolicyCalibrationLifecycleEvent::report_id)
        .def_readonly("detail", &PolicyCalibrationLifecycleEvent::detail);

    py::class_<PolicyCalibrationLifecycleSummary>(module, "PolicyCalibrationLifecycleSummary")
        .def_readonly("assessments", &PolicyCalibrationLifecycleSummary::assessments)
        .def_readonly("stable", &PolicyCalibrationLifecycleSummary::stable)
        .def_readonly("insufficient_data", &PolicyCalibrationLifecycleSummary::insufficient_data)
        .def_readonly("drift_detected", &PolicyCalibrationLifecycleSummary::drift_detected)
        .def_readonly("transitions", &PolicyCalibrationLifecycleSummary::transitions);

    py::class_<PolicyCalibrationLifecycleLoadOptions>(module, "PolicyCalibrationLifecycleLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_reports", &PolicyCalibrationLifecycleLoadOptions::maximum_reports)
        .def_readwrite("maximum_events", &PolicyCalibrationLifecycleLoadOptions::maximum_events)
        .def_readwrite("maximum_total_bins", &PolicyCalibrationLifecycleLoadOptions::maximum_total_bins)
        .def_readwrite("maximum_payload_bytes",
                       &PolicyCalibrationLifecycleLoadOptions::maximum_payload_bytes);

    py::class_<PolicyCalibrationLifecycle>(module, "PolicyCalibrationLifecycle")
        .def_static(
            "create",
            [](const PolicyCalibrationProfile& profile) {
                return unwrap(PolicyCalibrationLifecycle::create(profile));
            },
            py::arg("profile"))
        .def_property_readonly("profile_id", &PolicyCalibrationLifecycle::profile_id)
        .def_property_readonly("state", &PolicyCalibrationLifecycle::state)
        .def_property_readonly("generation", &PolicyCalibrationLifecycle::generation)
        .def_property_readonly("current_event_id", &PolicyCalibrationLifecycle::current_event_id)
        .def_property_readonly("latest_report_id", &PolicyCalibrationLifecycle::latest_report_id)
        .def_property_readonly("reports", &PolicyCalibrationLifecycle::reports)
        .def_property_readonly("events", &PolicyCalibrationLifecycle::events)
        .def(
            "assess",
            [](PolicyCalibrationLifecycle& lifecycle, const PolicyCalibrationProfile& profile,
               PolicyCalibrationWindowInput window, std::string expected_current_event_id,
               const PolicyCalibrationDriftOptions& options, std::size_t maximum_reports,
               std::size_t maximum_events) {
                return unwrap(lifecycle.assess(profile, std::move(window), expected_current_event_id, options,
                                               maximum_reports, maximum_events));
            },
            py::arg("profile"), py::arg("window"), py::arg("expected_current_event_id"),
            py::arg("options") = PolicyCalibrationDriftOptions{}, py::arg("maximum_reports") = 100'000,
            py::arg("maximum_events") = 1'000'000)
        .def(
            "transition",
            [](PolicyCalibrationLifecycle& lifecycle, const PolicyCalibrationProfile& profile,
               std::string expected_current_event_id, PolicyCalibrationLifecycleState target_state,
               std::string detail, std::size_t maximum_events) {
                return unwrap(lifecycle.transition(profile, expected_current_event_id, target_state,
                                                   std::move(detail), maximum_events));
            },
            py::arg("profile"), py::arg("expected_current_event_id"), py::arg("target_state"),
            py::arg("detail"), py::arg("maximum_events") = 1'000'000)
        .def("latest_report",
             [](const PolicyCalibrationLifecycle& lifecycle) { return unwrap(lifecycle.latest_report()); })
        .def(
            "report",
            [](const PolicyCalibrationLifecycle& lifecycle, std::string report_id) {
                return unwrap(lifecycle.report(report_id));
            },
            py::arg("report_id"))
        .def_property_readonly("summary", &PolicyCalibrationLifecycle::summary)
        .def_property_readonly("deployment_ready", &PolicyCalibrationLifecycle::deployment_ready)
        .def("valid", &PolicyCalibrationLifecycle::valid, py::arg("profile"))
        .def(
            "save",
            [](const PolicyCalibrationLifecycle& lifecycle, const std::filesystem::path& path,
               const PolicyCalibrationProfile& profile,
               const SaveOptions& options) { unwrap_void(lifecycle.save(path, profile, options)); },
            py::arg("path"), py::arg("profile"), py::arg("options") = SaveOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& path, const PolicyCalibrationProfile& profile,
               const PolicyCalibrationLifecycleLoadOptions& options) {
                return unwrap(PolicyCalibrationLifecycle::load(path, profile, options));
            },
            py::arg("path"), py::arg("profile"),
            py::arg("options") = PolicyCalibrationLifecycleLoadOptions{});

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
        .def_readonly("lifecycle_event_id", &CalibratedPolicyBatchReport::lifecycle_event_id)
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
        .def(
            "check_proposals_guarded",
            [](CalibratedPolicySafetyGate& gate, const PolicyCalibrationProfile& profile,
               const PolicyCalibrationLifecycle& lifecycle, std::string expected_lifecycle_event_id,
               std::string expected_scope_id, std::string expected_policy_model_digest,
               const SerialRobotModel& robot, const SceneSnapshot& scene, const SafeAtlas& atlas,
               const Configuration& current, const std::vector<PolicyProposal>& proposals,
               const CalibratedPolicyGateOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return gate.check_proposals_guarded(
                        profile, lifecycle, expected_lifecycle_event_id, expected_scope_id,
                        expected_policy_model_digest, robot, scene, atlas, view(current), proposals, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("profile"), py::arg("lifecycle"), py::arg("expected_lifecycle_event_id"),
            py::arg("expected_scope_id"), py::arg("expected_policy_model_digest"), py::arg("robot"),
            py::arg("scene"), py::arg("atlas"), py::arg("current"), py::arg("proposals"),
            py::arg("options") = CalibratedPolicyGateOptions{})
        .def_property_readonly("telemetry", &CalibratedPolicySafetyGate::telemetry)
        .def("reset_telemetry", &CalibratedPolicySafetyGate::reset_telemetry);

    module.def("policy_calibration_drift_status_name", &policy_calibration_drift_status_name);
    module.def("policy_calibration_drift_reason_name", &policy_calibration_drift_reason_name);
    module.def("policy_calibration_lifecycle_state_name", &policy_calibration_lifecycle_state_name);
    module.def("policy_calibration_lifecycle_event_type_name", &policy_calibration_lifecycle_event_type_name);
}

} // namespace rbfsafe::python_binding
