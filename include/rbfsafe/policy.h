#pragma once

#include <rbfsafe/shield.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rbfsafe {

enum class PolicySelectionMode : std::uint8_t {
    InputOrder = 0,
    HighestConfidence = 1,
    LowestUncertainty = 2,
};

enum class PolicyGateReason : std::uint8_t {
    ShieldAccepted = 0,
    ShieldRepaired = 1,
    ConfidenceBelowMinimum = 2,
    StateUncertaintyExceeded = 3,
    ActionUncertaintyExceeded = 4,
    ObservationTooOld = 5,
    InferenceLatencyExceeded = 6,
    ShieldRejected = 7,
};

enum class PolicyFeedbackLabel : std::uint8_t {
    SelectedAccepted = 0,
    SelectedRepaired = 1,
    EligibleNotSelected = 2,
    PolicyRejected = 3,
    ShieldRejected = 4,
};

struct PolicyProposalMetadata {
    std::string policy_id;
    std::string task_id;
    std::string episode_id;
    std::uint64_t sequence = 0;
    double confidence = 1.0;
    double state_uncertainty = 0.0;
    double action_uncertainty = 0.0;
    double observation_age_seconds = 0.0;
    double inference_latency_seconds = 0.0;
};

struct PolicyProposal {
    ShieldAction action;
    PolicyProposalMetadata metadata;
};

struct PolicyGateOptions {
    double minimum_confidence = 0.0;
    double maximum_state_uncertainty = std::numeric_limits<double>::max();
    double maximum_action_uncertainty = std::numeric_limits<double>::max();
    double maximum_observation_age_seconds = std::numeric_limits<double>::max();
    double maximum_inference_latency_seconds = std::numeric_limits<double>::max();
    std::size_t maximum_proposals = 256;
    PolicySelectionMode selection_mode = PolicySelectionMode::InputOrder;
    ShieldOptions shield;
};

struct PolicyGateDecision {
    std::string id;
    std::string proposal_id;
    PolicyProposalMetadata metadata;
    bool policy_eligible = false;
    bool selected = false;
    PolicyGateReason reason = PolicyGateReason::ConfidenceBelowMinimum;
    std::optional<ShieldDecision> shield_decision;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
};

struct PolicyFeedbackRecord {
    std::string id;
    std::string proposal_id;
    std::string policy_decision_id;
    std::string shield_decision_id;
    std::string robot_digest;
    std::string scene_digest;
    PolicyProposalMetadata metadata;
    PolicyFeedbackLabel label = PolicyFeedbackLabel::PolicyRejected;
    PolicyGateReason reason = PolicyGateReason::ConfidenceBelowMinimum;
    ShieldActionType action_type = ShieldActionType::JointDelta;
    Configuration requested_target;
    Configuration output_target;
    double repair_distance = 0.0;
    EvidenceLevel evidence = EvidenceLevel::Unknown;
};

struct PolicyBatchReport {
    std::vector<PolicyGateDecision> decisions;
    std::vector<PolicyFeedbackRecord> feedback;
    std::optional<std::size_t> selected_index;
};

struct PolicyTelemetrySnapshot {
    std::uint64_t batches = 0;
    std::uint64_t proposals = 0;
    std::uint64_t policy_rejections = 0;
    std::uint64_t shield_checks = 0;
    std::uint64_t shield_accepts = 0;
    std::uint64_t shield_repairs = 0;
    std::uint64_t shield_rejections = 0;
    std::uint64_t selected_accepts = 0;
    std::uint64_t selected_repairs = 0;
};

class LearningPolicySafetyGate {
  public:
    Result<PolicyBatchReport> check_proposals(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                              const SafeAtlas& atlas, std::span<const double> current,
                                              std::span<const PolicyProposal> proposals,
                                              const PolicyGateOptions& options = {});

    PolicyTelemetrySnapshot telemetry() const;
    void reset_telemetry();

  private:
    RuntimeShield shield_;
    mutable std::mutex telemetry_mutex_;
    PolicyTelemetrySnapshot telemetry_;
};

struct PolicyFeedbackQuery {
    std::string policy_id;
    std::string task_id;
    std::string episode_id;
    std::optional<PolicyFeedbackLabel> label;
    std::size_t maximum_results = 100'000;
};

struct PolicyFeedbackSummary {
    std::uint64_t records = 0;
    std::uint64_t selected_accepted = 0;
    std::uint64_t selected_repaired = 0;
    std::uint64_t eligible_not_selected = 0;
    std::uint64_t policy_rejected = 0;
    std::uint64_t shield_rejected = 0;
};

struct PolicyFeedbackLoadOptions {
    std::size_t maximum_records = 1'000'000;
    std::uintmax_t maximum_payload_bytes = 536'870'912ULL;
};

class PolicyFeedbackDatabase {
  public:
    PolicyFeedbackDatabase() = default;

    static Result<PolicyFeedbackDatabase> create(std::vector<PolicyFeedbackRecord> records);

    const std::vector<PolicyFeedbackRecord>& records() const noexcept { return records_; }
    Result<void> append(std::span<const PolicyFeedbackRecord> records,
                        std::size_t maximum_records = 1'000'000);
    Result<std::vector<PolicyFeedbackRecord>> query(const PolicyFeedbackQuery& query = {}) const;
    PolicyFeedbackSummary summary() const noexcept;
    bool valid() const;

    Result<void> save(const std::filesystem::path& directory, const SaveOptions& options = {}) const;
    static Result<PolicyFeedbackDatabase> load(const std::filesystem::path& directory,
                                               const PolicyFeedbackLoadOptions& options = {});

  private:
    std::vector<PolicyFeedbackRecord> records_;
};

std::string policy_selection_mode_name(PolicySelectionMode mode);
std::string policy_gate_reason_name(PolicyGateReason reason);
std::string policy_feedback_label_name(PolicyFeedbackLabel label);

} // namespace rbfsafe
