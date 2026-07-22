#include <rbfsafe/policy.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/policy.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <type_traits>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumMetadataStringBytes = 256;
constexpr std::size_t kMaximumConfigurationDimension = 128;

internal::Json configuration_json(std::span<const double> configuration) {
    internal::Json::Array values;
    values.reserve(configuration.size());
    for (const double value : configuration)
        values.emplace_back(value);
    return values;
}

internal::Json metadata_json(const PolicyProposalMetadata& metadata) {
    return internal::Json::Object{
        {"action_uncertainty", metadata.action_uncertainty},
        {"confidence", metadata.confidence},
        {"episode_id", metadata.episode_id},
        {"inference_latency_seconds", metadata.inference_latency_seconds},
        {"observation_age_seconds", metadata.observation_age_seconds},
        {"policy_id", metadata.policy_id},
        {"sequence", std::to_string(metadata.sequence)},
        {"state_uncertainty", metadata.state_uncertainty},
        {"task_id", metadata.task_id},
    };
}

bool valid_action_type(ShieldActionType type) {
    return type == ShieldActionType::JointDelta || type == ShieldActionType::EndEffectorPose ||
           type == ShieldActionType::Trajectory;
}

bool valid_gate_reason(PolicyGateReason reason) {
    return reason >= PolicyGateReason::ShieldAccepted && reason <= PolicyGateReason::ShieldRejected;
}

bool valid_feedback_label(PolicyFeedbackLabel label) {
    return label >= PolicyFeedbackLabel::SelectedAccepted && label <= PolicyFeedbackLabel::ShieldRejected;
}

Result<void> validate_metadata(const PolicyProposalMetadata& metadata, std::string context) {
    if (metadata.policy_id.empty() || metadata.task_id.empty() ||
        metadata.policy_id.size() > kMaximumMetadataStringBytes ||
        metadata.task_id.size() > kMaximumMetadataStringBytes ||
        metadata.episode_id.size() > kMaximumMetadataStringBytes) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "policy metadata identifiers are empty or too long", std::move(context));
    }
    if (!std::isfinite(metadata.confidence) || metadata.confidence < 0.0 || metadata.confidence > 1.0 ||
        !std::isfinite(metadata.state_uncertainty) || metadata.state_uncertainty < 0.0 ||
        !std::isfinite(metadata.action_uncertainty) || metadata.action_uncertainty < 0.0 ||
        !std::isfinite(metadata.observation_age_seconds) || metadata.observation_age_seconds < 0.0 ||
        !std::isfinite(metadata.inference_latency_seconds) || metadata.inference_latency_seconds < 0.0) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "policy confidence, uncertainty, age, or latency is invalid",
                                     std::move(context));
    }
    return Result<void>::success();
}

Result<void> validate_options(const PolicyGateOptions& options) {
    if (!std::isfinite(options.minimum_confidence) || options.minimum_confidence < 0.0 ||
        options.minimum_confidence > 1.0 || !std::isfinite(options.maximum_state_uncertainty) ||
        options.maximum_state_uncertainty < 0.0 || !std::isfinite(options.maximum_action_uncertainty) ||
        options.maximum_action_uncertainty < 0.0 || !std::isfinite(options.maximum_observation_age_seconds) ||
        options.maximum_observation_age_seconds < 0.0 ||
        !std::isfinite(options.maximum_inference_latency_seconds) ||
        options.maximum_inference_latency_seconds < 0.0 || options.maximum_proposals == 0 ||
        (options.selection_mode != PolicySelectionMode::InputOrder &&
         options.selection_mode != PolicySelectionMode::HighestConfidence &&
         options.selection_mode != PolicySelectionMode::LowestUncertainty)) {
        return Result<void>::failure(StatusCode::InvalidArgument, "policy gate options are invalid",
                                     "policy gate");
    }
    return Result<void>::success();
}

ShieldActionType action_type(const ShieldAction& action) {
    return std::visit(
        [](const auto& value) {
            using Action = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Action, JointDeltaAction>) {
                return ShieldActionType::JointDelta;
            } else if constexpr (std::is_same_v<Action, EndEffectorAction>) {
                return ShieldActionType::EndEffectorPose;
            } else {
                return ShieldActionType::Trajectory;
            }
        },
        action);
}

Result<Configuration> validate_action_and_target(const ShieldAction& action, std::span<const double> current,
                                                 const PolicyGateOptions& options) {
    return std::visit(
        [&](const auto& value) -> Result<Configuration> {
            using Action = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Action, JointDeltaAction>) {
                auto status = validate_configuration(value.delta, current.size(), "policy joint delta");
                if (!status)
                    return status.error();
                Configuration target(current.begin(), current.end());
                for (std::size_t axis = 0; axis < target.size(); ++axis) {
                    target[axis] += value.delta[axis];
                    if (!std::isfinite(target[axis])) {
                        return Result<Configuration>::failure(StatusCode::InvalidArgument,
                                                              "policy joint target is not finite");
                    }
                }
                return target;
            } else if constexpr (std::is_same_v<Action, EndEffectorAction>) {
                if (!value.target.valid()) {
                    return Result<Configuration>::failure(StatusCode::InvalidArgument,
                                                          "policy end-effector target is invalid");
                }
                return Configuration{};
            } else {
                if (value.waypoints.empty()) {
                    return Result<Configuration>::failure(StatusCode::InvalidArgument,
                                                          "policy trajectory is empty");
                }
                if (value.waypoints.size() > options.shield.maximum_input_waypoints) {
                    return Result<Configuration>::failure(StatusCode::ResourceLimit,
                                                          "policy trajectory exceeds waypoint budget");
                }
                for (const auto& waypoint : value.waypoints) {
                    auto status = validate_configuration(waypoint, current.size(), "policy trajectory");
                    if (!status)
                        return status.error();
                }
                return value.waypoints.back();
            }
        },
        action);
}

std::optional<PolicyGateReason> rejected_by_policy(const PolicyProposalMetadata& metadata,
                                                   const PolicyGateOptions& options) {
    if (metadata.confidence < options.minimum_confidence)
        return PolicyGateReason::ConfidenceBelowMinimum;
    if (metadata.state_uncertainty > options.maximum_state_uncertainty)
        return PolicyGateReason::StateUncertaintyExceeded;
    if (metadata.action_uncertainty > options.maximum_action_uncertainty)
        return PolicyGateReason::ActionUncertaintyExceeded;
    if (metadata.observation_age_seconds > options.maximum_observation_age_seconds)
        return PolicyGateReason::ObservationTooOld;
    if (metadata.inference_latency_seconds > options.maximum_inference_latency_seconds)
        return PolicyGateReason::InferenceLatencyExceeded;
    return std::nullopt;
}

std::string proposal_identity(const PolicyProposal& proposal) {
    internal::Json document(internal::Json::Object{
        {"action_digest", shield_action_digest(proposal.action)},
        {"metadata", metadata_json(proposal.metadata)},
    });
    return internal::sha256(document.dump(false));
}

std::string policy_decision_identity(const PolicyGateDecision& decision) {
    internal::Json document(internal::Json::Object{
        {"evidence", static_cast<int>(decision.evidence)},
        {"metadata", metadata_json(decision.metadata)},
        {"policy_eligible", decision.policy_eligible},
        {"proposal_id", decision.proposal_id},
        {"reason", static_cast<int>(decision.reason)},
        {"selected", decision.selected},
        {"shield_decision_id", decision.shield_decision ? decision.shield_decision->id : ""},
    });
    return internal::sha256(document.dump(false));
}

double combined_uncertainty(const PolicyProposalMetadata& metadata) {
    return metadata.state_uncertainty + metadata.action_uncertainty;
}

bool preferred_candidate(const PolicyGateDecision& candidate, std::size_t candidate_index,
                         const PolicyGateDecision& incumbent, std::size_t incumbent_index,
                         PolicySelectionMode mode) {
    const auto candidate_outcome = candidate.shield_decision->outcome;
    const auto incumbent_outcome = incumbent.shield_decision->outcome;
    if (candidate_outcome != incumbent_outcome)
        return candidate_outcome == ShieldOutcome::Accept;
    if (mode == PolicySelectionMode::InputOrder)
        return false;
    if (mode == PolicySelectionMode::HighestConfidence) {
        if (candidate.metadata.confidence != incumbent.metadata.confidence)
            return candidate.metadata.confidence > incumbent.metadata.confidence;
        const double candidate_uncertainty = combined_uncertainty(candidate.metadata);
        const double incumbent_uncertainty = combined_uncertainty(incumbent.metadata);
        if (candidate_uncertainty != incumbent_uncertainty)
            return candidate_uncertainty < incumbent_uncertainty;
    } else {
        const double candidate_uncertainty = combined_uncertainty(candidate.metadata);
        const double incumbent_uncertainty = combined_uncertainty(incumbent.metadata);
        if (candidate_uncertainty != incumbent_uncertainty)
            return candidate_uncertainty < incumbent_uncertainty;
        if (candidate.metadata.confidence != incumbent.metadata.confidence)
            return candidate.metadata.confidence > incumbent.metadata.confidence;
    }
    return candidate_index < incumbent_index;
}

PolicyFeedbackLabel feedback_label(const PolicyGateDecision& decision) {
    if (!decision.policy_eligible)
        return PolicyFeedbackLabel::PolicyRejected;
    if (!decision.shield_decision || decision.shield_decision->outcome == ShieldOutcome::Reject)
        return PolicyFeedbackLabel::ShieldRejected;
    if (!decision.selected)
        return PolicyFeedbackLabel::EligibleNotSelected;
    return decision.shield_decision->outcome == ShieldOutcome::Accept ? PolicyFeedbackLabel::SelectedAccepted
                                                                      : PolicyFeedbackLabel::SelectedRepaired;
}

PolicyFeedbackRecord make_feedback(const PolicyGateDecision& decision, ShieldActionType type,
                                   const Configuration& requested_target, const SafeAtlas& atlas) {
    PolicyFeedbackRecord record;
    record.proposal_id = decision.proposal_id;
    record.policy_decision_id = decision.id;
    record.robot_digest = atlas.robot_digest();
    record.scene_digest = atlas.scene_digest();
    record.metadata = decision.metadata;
    record.label = feedback_label(decision);
    record.reason = decision.reason;
    record.action_type = type;
    record.requested_target = requested_target;
    record.evidence = decision.evidence;
    if (decision.shield_decision) {
        record.shield_decision_id = decision.shield_decision->id;
        record.requested_target = decision.shield_decision->requested_target;
        record.repair_distance = decision.shield_decision->repair_distance;
        if (!decision.shield_decision->output_trajectory.empty())
            record.output_target = decision.shield_decision->output_trajectory.back();
    }
    record.id = internal::policy_feedback_record_identity(record);
    return record;
}

void accumulate_telemetry(PolicyTelemetrySnapshot& telemetry, const PolicyBatchReport& report) {
    ++telemetry.batches;
    telemetry.proposals += report.decisions.size();
    for (const auto& decision : report.decisions) {
        if (!decision.policy_eligible) {
            ++telemetry.policy_rejections;
            continue;
        }
        ++telemetry.shield_checks;
        switch (decision.shield_decision->outcome) {
        case ShieldOutcome::Accept:
            ++telemetry.shield_accepts;
            break;
        case ShieldOutcome::Repair:
            ++telemetry.shield_repairs;
            break;
        case ShieldOutcome::Reject:
            ++telemetry.shield_rejections;
            break;
        }
        if (decision.selected && decision.shield_decision->outcome == ShieldOutcome::Accept)
            ++telemetry.selected_accepts;
        if (decision.selected && decision.shield_decision->outcome == ShieldOutcome::Repair)
            ++telemetry.selected_repairs;
    }
}

} // namespace

namespace internal {

std::string policy_feedback_record_identity(const PolicyFeedbackRecord& record) {
    Json document(Json::Object{
        {"action_type", static_cast<int>(record.action_type)},
        {"evidence", static_cast<int>(record.evidence)},
        {"label", static_cast<int>(record.label)},
        {"metadata", metadata_json(record.metadata)},
        {"output_target", configuration_json(record.output_target)},
        {"policy_decision_id", record.policy_decision_id},
        {"proposal_id", record.proposal_id},
        {"reason", static_cast<int>(record.reason)},
        {"repair_distance", record.repair_distance},
        {"requested_target", configuration_json(record.requested_target)},
        {"robot_digest", record.robot_digest},
        {"scene_digest", record.scene_digest},
        {"shield_decision_id", record.shield_decision_id},
    });
    return sha256(document.dump(false));
}

Result<void> validate_policy_feedback_record(const PolicyFeedbackRecord& record) {
    auto metadata_status = validate_metadata(record.metadata, "policy feedback");
    if (!metadata_status)
        return metadata_status;
    if (!valid_sha256(record.id) || !valid_sha256(record.proposal_id) ||
        !valid_sha256(record.policy_decision_id) || !valid_sha256(record.robot_digest) ||
        !valid_sha256(record.scene_digest) ||
        (!record.shield_decision_id.empty() && !valid_sha256(record.shield_decision_id)) ||
        !valid_action_type(record.action_type) || !valid_gate_reason(record.reason) ||
        !valid_feedback_label(record.label) || !std::isfinite(record.repair_distance) ||
        record.repair_distance < 0.0 || record.evidence == EvidenceLevel::RuntimeExecutable ||
        record.requested_target.size() > kMaximumConfigurationDimension ||
        record.output_target.size() > kMaximumConfigurationDimension ||
        (!record.requested_target.empty() && !record.output_target.empty() &&
         record.requested_target.size() != record.output_target.size()) ||
        !std::all_of(record.requested_target.begin(), record.requested_target.end(),
                     [](double value) { return std::isfinite(value); }) ||
        !std::all_of(record.output_target.begin(), record.output_target.end(),
                     [](double value) { return std::isfinite(value); })) {
        return Result<void>::failure(StatusCode::InvalidArgument, "policy feedback record is invalid",
                                     record.id);
    }
    if ((record.label == PolicyFeedbackLabel::PolicyRejected &&
         (!record.shield_decision_id.empty() || record.evidence != EvidenceLevel::Unknown)) ||
        (record.label == PolicyFeedbackLabel::ShieldRejected && record.shield_decision_id.empty()) ||
        ((record.label == PolicyFeedbackLabel::SelectedAccepted ||
          record.label == PolicyFeedbackLabel::SelectedRepaired ||
          record.label == PolicyFeedbackLabel::EligibleNotSelected) &&
         (record.shield_decision_id.empty() || record.evidence != EvidenceLevel::CertifiedConnectivity))) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "policy feedback label contradicts its shield evidence", record.id);
    }
    if (record.id != policy_feedback_record_identity(record)) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "policy feedback record identity is inconsistent", record.id);
    }
    return Result<void>::success();
}

} // namespace internal

Result<PolicyBatchReport> LearningPolicySafetyGate::check_proposals(const SerialRobotModel& robot,
                                                                    const SceneSnapshot& scene,
                                                                    const SafeAtlas& atlas,
                                                                    std::span<const double> current,
                                                                    std::span<const PolicyProposal> proposals,
                                                                    const PolicyGateOptions& options) {
    auto options_status = validate_options(options);
    if (!options_status)
        return options_status.error();
    if (proposals.empty()) {
        return Result<PolicyBatchReport>::failure(StatusCode::InvalidArgument,
                                                  "policy proposal batch is empty", "policy gate");
    }
    if (proposals.size() > options.maximum_proposals) {
        return Result<PolicyBatchReport>::failure(
            StatusCode::ResourceLimit, "policy proposal batch exceeds resource limit", "policy gate");
    }
    if (options.shield.cancellation.cancelled()) {
        return Result<PolicyBatchReport>::failure(StatusCode::Cancelled, "policy gate was cancelled",
                                                  "policy gate");
    }
    auto compatibility = atlas.verify_compatible(robot, scene);
    if (!compatibility)
        return compatibility.error();
    auto current_status = validate_configuration(current, atlas.dimension(), "policy current state");
    if (!current_status)
        return current_status.error();

    PolicyBatchReport report;
    report.decisions.reserve(proposals.size());
    std::vector<Configuration> requested_targets;
    requested_targets.reserve(proposals.size());
    std::vector<ShieldActionType> action_types;
    action_types.reserve(proposals.size());
    std::set<std::string> proposal_ids;
    for (std::size_t index = 0; index < proposals.size(); ++index) {
        if ((index & 63U) == 0U && options.shield.cancellation.cancelled()) {
            return Result<PolicyBatchReport>::failure(StatusCode::Cancelled, "policy gate was cancelled",
                                                      "policy gate");
        }
        const auto& proposal = proposals[index];
        auto metadata_status = validate_metadata(proposal.metadata, "policy proposal");
        if (!metadata_status)
            return metadata_status.error();
        auto target = validate_action_and_target(proposal.action, current, options);
        if (!target)
            return target.error();
        requested_targets.push_back(std::move(target).value());
        action_types.push_back(action_type(proposal.action));

        PolicyGateDecision decision;
        decision.metadata = proposal.metadata;
        decision.proposal_id = proposal_identity(proposal);
        if (!proposal_ids.insert(decision.proposal_id).second) {
            return Result<PolicyBatchReport>::failure(StatusCode::InvalidArgument,
                                                      "policy proposal batch contains duplicates",
                                                      decision.proposal_id);
        }
        const auto policy_rejection = rejected_by_policy(proposal.metadata, options);
        if (policy_rejection) {
            decision.reason = *policy_rejection;
            report.decisions.push_back(std::move(decision));
            continue;
        }
        decision.policy_eligible = true;
        auto shield_decision =
            shield_.check_action(robot, scene, atlas, current, proposal.action, options.shield);
        if (!shield_decision)
            return shield_decision.error();
        decision.evidence = shield_decision.value().evidence;
        switch (shield_decision.value().outcome) {
        case ShieldOutcome::Accept:
            decision.reason = PolicyGateReason::ShieldAccepted;
            break;
        case ShieldOutcome::Repair:
            decision.reason = PolicyGateReason::ShieldRepaired;
            break;
        case ShieldOutcome::Reject:
            decision.reason = PolicyGateReason::ShieldRejected;
            break;
        }
        decision.shield_decision = std::move(shield_decision).value();
        report.decisions.push_back(std::move(decision));
    }

    for (std::size_t index = 0; index < report.decisions.size(); ++index) {
        const auto& decision = report.decisions[index];
        if (!decision.shield_decision || decision.shield_decision->outcome == ShieldOutcome::Reject)
            continue;
        if (!report.selected_index ||
            preferred_candidate(decision, index, report.decisions[*report.selected_index],
                                *report.selected_index, options.selection_mode)) {
            report.selected_index = index;
        }
    }
    if (report.selected_index)
        report.decisions[*report.selected_index].selected = true;
    report.feedback.reserve(report.decisions.size());
    for (std::size_t index = 0; index < report.decisions.size(); ++index) {
        auto& decision = report.decisions[index];
        decision.id = policy_decision_identity(decision);
        report.feedback.push_back(
            make_feedback(decision, action_types[index], requested_targets[index], atlas));
    }
    {
        std::lock_guard lock(telemetry_mutex_);
        accumulate_telemetry(telemetry_, report);
    }
    return report;
}

PolicyTelemetrySnapshot LearningPolicySafetyGate::telemetry() const {
    std::lock_guard lock(telemetry_mutex_);
    return telemetry_;
}

void LearningPolicySafetyGate::reset_telemetry() {
    std::lock_guard lock(telemetry_mutex_);
    telemetry_ = {};
}

Result<PolicyFeedbackDatabase> PolicyFeedbackDatabase::create(std::vector<PolicyFeedbackRecord> records) {
    PolicyFeedbackDatabase database;
    std::set<std::string> identities;
    for (const auto& record : records) {
        auto status = internal::validate_policy_feedback_record(record);
        if (!status)
            return status.error();
        if (!identities.insert(record.id).second) {
            return Result<PolicyFeedbackDatabase>::failure(StatusCode::InvalidArgument,
                                                           "duplicate policy feedback record", record.id);
        }
    }
    database.records_ = std::move(records);
    return database;
}

Result<void> PolicyFeedbackDatabase::append(std::span<const PolicyFeedbackRecord> records,
                                            std::size_t maximum_records) {
    if (maximum_records == 0 || records.size() > maximum_records ||
        records_.size() > maximum_records - records.size()) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "policy feedback database exceeds record budget");
    }
    std::set<std::string> identities;
    for (const auto& record : records_)
        identities.insert(record.id);
    for (const auto& record : records) {
        auto status = internal::validate_policy_feedback_record(record);
        if (!status)
            return status;
        if (!identities.insert(record.id).second) {
            return Result<void>::failure(StatusCode::InvalidArgument, "duplicate policy feedback record",
                                         record.id);
        }
    }
    records_.insert(records_.end(), records.begin(), records.end());
    return Result<void>::success();
}

Result<std::vector<PolicyFeedbackRecord>>
PolicyFeedbackDatabase::query(const PolicyFeedbackQuery& query_options) const {
    if (query_options.maximum_results == 0 || query_options.policy_id.size() > kMaximumMetadataStringBytes ||
        query_options.task_id.size() > kMaximumMetadataStringBytes ||
        query_options.episode_id.size() > kMaximumMetadataStringBytes ||
        (query_options.label && !valid_feedback_label(*query_options.label))) {
        return Result<std::vector<PolicyFeedbackRecord>>::failure(StatusCode::InvalidArgument,
                                                                  "policy feedback query is invalid");
    }
    std::vector<PolicyFeedbackRecord> result;
    result.reserve(std::min(query_options.maximum_results, records_.size()));
    for (const auto& record : records_) {
        if (!query_options.policy_id.empty() && record.metadata.policy_id != query_options.policy_id)
            continue;
        if (!query_options.task_id.empty() && record.metadata.task_id != query_options.task_id)
            continue;
        if (!query_options.episode_id.empty() && record.metadata.episode_id != query_options.episode_id)
            continue;
        if (query_options.label && record.label != *query_options.label)
            continue;
        result.push_back(record);
        if (result.size() == query_options.maximum_results)
            break;
    }
    return result;
}

PolicyFeedbackSummary PolicyFeedbackDatabase::summary() const noexcept {
    PolicyFeedbackSummary result;
    result.records = records_.size();
    for (const auto& record : records_) {
        switch (record.label) {
        case PolicyFeedbackLabel::SelectedAccepted:
            ++result.selected_accepted;
            break;
        case PolicyFeedbackLabel::SelectedRepaired:
            ++result.selected_repaired;
            break;
        case PolicyFeedbackLabel::EligibleNotSelected:
            ++result.eligible_not_selected;
            break;
        case PolicyFeedbackLabel::PolicyRejected:
            ++result.policy_rejected;
            break;
        case PolicyFeedbackLabel::ShieldRejected:
            ++result.shield_rejected;
            break;
        }
    }
    return result;
}

bool PolicyFeedbackDatabase::valid() const {
    std::set<std::string> identities;
    for (const auto& record : records_) {
        if (!internal::validate_policy_feedback_record(record) || !identities.insert(record.id).second)
            return false;
    }
    return true;
}

std::string policy_selection_mode_name(PolicySelectionMode mode) {
    switch (mode) {
    case PolicySelectionMode::InputOrder:
        return "input_order";
    case PolicySelectionMode::HighestConfidence:
        return "highest_confidence";
    case PolicySelectionMode::LowestUncertainty:
        return "lowest_uncertainty";
    }
    return "unknown";
}

std::string policy_gate_reason_name(PolicyGateReason reason) {
    switch (reason) {
    case PolicyGateReason::ShieldAccepted:
        return "shield_accepted";
    case PolicyGateReason::ShieldRepaired:
        return "shield_repaired";
    case PolicyGateReason::ConfidenceBelowMinimum:
        return "confidence_below_minimum";
    case PolicyGateReason::StateUncertaintyExceeded:
        return "state_uncertainty_exceeded";
    case PolicyGateReason::ActionUncertaintyExceeded:
        return "action_uncertainty_exceeded";
    case PolicyGateReason::ObservationTooOld:
        return "observation_too_old";
    case PolicyGateReason::InferenceLatencyExceeded:
        return "inference_latency_exceeded";
    case PolicyGateReason::ShieldRejected:
        return "shield_rejected";
    }
    return "unknown";
}

std::string policy_feedback_label_name(PolicyFeedbackLabel label) {
    switch (label) {
    case PolicyFeedbackLabel::SelectedAccepted:
        return "selected_accepted";
    case PolicyFeedbackLabel::SelectedRepaired:
        return "selected_repaired";
    case PolicyFeedbackLabel::EligibleNotSelected:
        return "eligible_not_selected";
    case PolicyFeedbackLabel::PolicyRejected:
        return "policy_rejected";
    case PolicyFeedbackLabel::ShieldRejected:
        return "shield_rejected";
    }
    return "unknown";
}

} // namespace rbfsafe
