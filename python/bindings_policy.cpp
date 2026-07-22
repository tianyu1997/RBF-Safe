#include "binding_support.h"

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <filesystem>
#include <utility>
#include <vector>

namespace rbfsafe::python_binding {

void bind_policy(py::module_& module) {
    py::enum_<PolicySelectionMode>(module, "PolicySelectionMode")
        .value("INPUT_ORDER", PolicySelectionMode::InputOrder)
        .value("HIGHEST_CONFIDENCE", PolicySelectionMode::HighestConfidence)
        .value("LOWEST_UNCERTAINTY", PolicySelectionMode::LowestUncertainty);

    py::enum_<PolicyGateReason>(module, "PolicyGateReason")
        .value("SHIELD_ACCEPTED", PolicyGateReason::ShieldAccepted)
        .value("SHIELD_REPAIRED", PolicyGateReason::ShieldRepaired)
        .value("CONFIDENCE_BELOW_MINIMUM", PolicyGateReason::ConfidenceBelowMinimum)
        .value("STATE_UNCERTAINTY_EXCEEDED", PolicyGateReason::StateUncertaintyExceeded)
        .value("ACTION_UNCERTAINTY_EXCEEDED", PolicyGateReason::ActionUncertaintyExceeded)
        .value("OBSERVATION_TOO_OLD", PolicyGateReason::ObservationTooOld)
        .value("INFERENCE_LATENCY_EXCEEDED", PolicyGateReason::InferenceLatencyExceeded)
        .value("SHIELD_REJECTED", PolicyGateReason::ShieldRejected);

    py::enum_<PolicyFeedbackLabel>(module, "PolicyFeedbackLabel")
        .value("SELECTED_ACCEPTED", PolicyFeedbackLabel::SelectedAccepted)
        .value("SELECTED_REPAIRED", PolicyFeedbackLabel::SelectedRepaired)
        .value("ELIGIBLE_NOT_SELECTED", PolicyFeedbackLabel::EligibleNotSelected)
        .value("POLICY_REJECTED", PolicyFeedbackLabel::PolicyRejected)
        .value("SHIELD_REJECTED", PolicyFeedbackLabel::ShieldRejected);

    py::class_<PolicyProposalMetadata>(module, "PolicyProposalMetadata")
        .def(py::init<>())
        .def_readwrite("policy_id", &PolicyProposalMetadata::policy_id)
        .def_readwrite("task_id", &PolicyProposalMetadata::task_id)
        .def_readwrite("episode_id", &PolicyProposalMetadata::episode_id)
        .def_readwrite("sequence", &PolicyProposalMetadata::sequence)
        .def_readwrite("confidence", &PolicyProposalMetadata::confidence)
        .def_readwrite("state_uncertainty", &PolicyProposalMetadata::state_uncertainty)
        .def_readwrite("action_uncertainty", &PolicyProposalMetadata::action_uncertainty)
        .def_readwrite("observation_age_seconds", &PolicyProposalMetadata::observation_age_seconds)
        .def_readwrite("inference_latency_seconds", &PolicyProposalMetadata::inference_latency_seconds);

    py::class_<PolicyProposal>(module, "PolicyProposal")
        .def(py::init<>())
        .def(py::init([](JointDeltaAction action, PolicyProposalMetadata metadata) {
                 return PolicyProposal{ShieldAction{std::move(action)}, std::move(metadata)};
             }),
             py::arg("action"), py::arg("metadata") = PolicyProposalMetadata{})
        .def(py::init([](EndEffectorAction action, PolicyProposalMetadata metadata) {
                 return PolicyProposal{ShieldAction{std::move(action)}, std::move(metadata)};
             }),
             py::arg("action"), py::arg("metadata") = PolicyProposalMetadata{})
        .def(py::init([](TrajectoryAction action, PolicyProposalMetadata metadata) {
                 return PolicyProposal{ShieldAction{std::move(action)}, std::move(metadata)};
             }),
             py::arg("action"), py::arg("metadata") = PolicyProposalMetadata{})
        .def_readwrite("action", &PolicyProposal::action)
        .def_readwrite("metadata", &PolicyProposal::metadata);

    py::class_<PolicyGateOptions>(module, "PolicyGateOptions")
        .def(py::init<>())
        .def_readwrite("minimum_confidence", &PolicyGateOptions::minimum_confidence)
        .def_readwrite("maximum_state_uncertainty", &PolicyGateOptions::maximum_state_uncertainty)
        .def_readwrite("maximum_action_uncertainty", &PolicyGateOptions::maximum_action_uncertainty)
        .def_readwrite("maximum_observation_age_seconds", &PolicyGateOptions::maximum_observation_age_seconds)
        .def_readwrite("maximum_inference_latency_seconds",
                       &PolicyGateOptions::maximum_inference_latency_seconds)
        .def_readwrite("maximum_proposals", &PolicyGateOptions::maximum_proposals)
        .def_readwrite("selection_mode", &PolicyGateOptions::selection_mode)
        .def_readwrite("shield", &PolicyGateOptions::shield);

    py::class_<PolicyGateDecision>(module, "PolicyGateDecision")
        .def_readonly("id", &PolicyGateDecision::id)
        .def_readonly("proposal_id", &PolicyGateDecision::proposal_id)
        .def_readonly("metadata", &PolicyGateDecision::metadata)
        .def_readonly("policy_eligible", &PolicyGateDecision::policy_eligible)
        .def_readonly("selected", &PolicyGateDecision::selected)
        .def_readonly("reason", &PolicyGateDecision::reason)
        .def_readonly("shield_decision", &PolicyGateDecision::shield_decision)
        .def_readonly("evidence", &PolicyGateDecision::evidence);

    py::class_<PolicyFeedbackRecord>(module, "PolicyFeedbackRecord")
        .def_readonly("id", &PolicyFeedbackRecord::id)
        .def_readonly("proposal_id", &PolicyFeedbackRecord::proposal_id)
        .def_readonly("policy_decision_id", &PolicyFeedbackRecord::policy_decision_id)
        .def_readonly("shield_decision_id", &PolicyFeedbackRecord::shield_decision_id)
        .def_readonly("robot_digest", &PolicyFeedbackRecord::robot_digest)
        .def_readonly("scene_digest", &PolicyFeedbackRecord::scene_digest)
        .def_readonly("metadata", &PolicyFeedbackRecord::metadata)
        .def_readonly("label", &PolicyFeedbackRecord::label)
        .def_readonly("reason", &PolicyFeedbackRecord::reason)
        .def_readonly("action_type", &PolicyFeedbackRecord::action_type)
        .def_readonly("requested_target", &PolicyFeedbackRecord::requested_target)
        .def_readonly("output_target", &PolicyFeedbackRecord::output_target)
        .def_readonly("repair_distance", &PolicyFeedbackRecord::repair_distance)
        .def_readonly("evidence", &PolicyFeedbackRecord::evidence);

    py::class_<PolicyBatchReport>(module, "PolicyBatchReport")
        .def_readonly("decisions", &PolicyBatchReport::decisions)
        .def_readonly("feedback", &PolicyBatchReport::feedback)
        .def_readonly("selected_index", &PolicyBatchReport::selected_index);

    py::class_<PolicyTelemetrySnapshot>(module, "PolicyTelemetrySnapshot")
        .def_readonly("batches", &PolicyTelemetrySnapshot::batches)
        .def_readonly("proposals", &PolicyTelemetrySnapshot::proposals)
        .def_readonly("policy_rejections", &PolicyTelemetrySnapshot::policy_rejections)
        .def_readonly("shield_checks", &PolicyTelemetrySnapshot::shield_checks)
        .def_readonly("shield_accepts", &PolicyTelemetrySnapshot::shield_accepts)
        .def_readonly("shield_repairs", &PolicyTelemetrySnapshot::shield_repairs)
        .def_readonly("shield_rejections", &PolicyTelemetrySnapshot::shield_rejections)
        .def_readonly("selected_accepts", &PolicyTelemetrySnapshot::selected_accepts)
        .def_readonly("selected_repairs", &PolicyTelemetrySnapshot::selected_repairs);

    py::class_<LearningPolicySafetyGate>(module, "LearningPolicySafetyGate")
        .def(py::init<>())
        .def(
            "check_proposals",
            [](LearningPolicySafetyGate& gate, const SerialRobotModel& robot, const SceneSnapshot& scene,
               const SafeAtlas& atlas, const Configuration& current,
               const std::vector<PolicyProposal>& proposals, const PolicyGateOptions& options) {
                auto result = [&]() {
                    py::gil_scoped_release release;
                    return gate.check_proposals(robot, scene, atlas, view(current), proposals, options);
                }();
                return unwrap(std::move(result));
            },
            py::arg("robot"), py::arg("scene"), py::arg("atlas"), py::arg("current"), py::arg("proposals"),
            py::arg("options") = PolicyGateOptions{})
        .def_property_readonly("telemetry", &LearningPolicySafetyGate::telemetry)
        .def("reset_telemetry", &LearningPolicySafetyGate::reset_telemetry);

    py::class_<PolicyFeedbackQuery>(module, "PolicyFeedbackQuery")
        .def(py::init<>())
        .def_readwrite("policy_id", &PolicyFeedbackQuery::policy_id)
        .def_readwrite("task_id", &PolicyFeedbackQuery::task_id)
        .def_readwrite("episode_id", &PolicyFeedbackQuery::episode_id)
        .def_readwrite("label", &PolicyFeedbackQuery::label)
        .def_readwrite("maximum_results", &PolicyFeedbackQuery::maximum_results);

    py::class_<PolicyFeedbackSummary>(module, "PolicyFeedbackSummary")
        .def_readonly("records", &PolicyFeedbackSummary::records)
        .def_readonly("selected_accepted", &PolicyFeedbackSummary::selected_accepted)
        .def_readonly("selected_repaired", &PolicyFeedbackSummary::selected_repaired)
        .def_readonly("eligible_not_selected", &PolicyFeedbackSummary::eligible_not_selected)
        .def_readonly("policy_rejected", &PolicyFeedbackSummary::policy_rejected)
        .def_readonly("shield_rejected", &PolicyFeedbackSummary::shield_rejected);

    py::class_<PolicyFeedbackLoadOptions>(module, "PolicyFeedbackLoadOptions")
        .def(py::init<>())
        .def_readwrite("maximum_records", &PolicyFeedbackLoadOptions::maximum_records)
        .def_readwrite("maximum_payload_bytes", &PolicyFeedbackLoadOptions::maximum_payload_bytes);

    py::class_<PolicyFeedbackDatabase>(module, "PolicyFeedbackDatabase")
        .def(py::init<>())
        .def_static("create",
                    [](std::vector<PolicyFeedbackRecord> records) {
                        return unwrap(PolicyFeedbackDatabase::create(std::move(records)));
                    })
        .def_property_readonly("records", &PolicyFeedbackDatabase::records)
        .def(
            "append",
            [](PolicyFeedbackDatabase& database, const std::vector<PolicyFeedbackRecord>& records,
               std::size_t maximum_records) { unwrap_void(database.append(records, maximum_records)); },
            py::arg("records"), py::arg("maximum_records") = 1'000'000)
        .def(
            "query",
            [](const PolicyFeedbackDatabase& database, const PolicyFeedbackQuery& query) {
                return unwrap(database.query(query));
            },
            py::arg("query") = PolicyFeedbackQuery{})
        .def_property_readonly("summary", &PolicyFeedbackDatabase::summary)
        .def("valid", &PolicyFeedbackDatabase::valid)
        .def(
            "save",
            [](const PolicyFeedbackDatabase& database, const std::filesystem::path& path,
               const SaveOptions& options) { unwrap_void(database.save(path, options)); },
            py::arg("path"), py::arg("options") = SaveOptions{})
        .def_static(
            "load",
            [](const std::filesystem::path& path, const PolicyFeedbackLoadOptions& options) {
                return unwrap(PolicyFeedbackDatabase::load(path, options));
            },
            py::arg("path"), py::arg("options") = PolicyFeedbackLoadOptions{});

    module.def("policy_selection_mode_name", &policy_selection_mode_name);
    module.def("policy_gate_reason_name", &policy_gate_reason_name);
    module.def("policy_feedback_label_name", &policy_feedback_label_name);
}

} // namespace rbfsafe::python_binding
