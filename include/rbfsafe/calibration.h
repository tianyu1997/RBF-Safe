#pragma once

#include <rbfsafe/policy.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rbfsafe {

struct PolicyCalibrationBinInput {
    double lower_confidence = 0.0;
    double upper_confidence = 1.0;
    double mean_confidence = 0.5;
    std::uint64_t samples = 0;
    std::uint64_t successes = 0;
};

struct PolicyCalibrationProfileInput {
    std::string policy_id;
    std::string policy_model_digest;
    std::string scope_id;
    std::string task_id;
    std::string dataset_digest;
    std::string method;
    std::string method_version;
    std::string outcome_definition;
    std::string state_uncertainty_unit;
    std::string action_uncertainty_unit;
    std::vector<PolicyCalibrationBinInput> bins;
};

struct PolicyCalibrationBin {
    double lower_confidence = 0.0;
    double upper_confidence = 1.0;
    double mean_confidence = 0.5;
    std::uint64_t samples = 0;
    std::uint64_t successes = 0;
    double observed_success_rate = 0.0;
    double lower_confidence_bound_95 = 0.0;
    double absolute_calibration_error = 0.0;
};

struct PolicyCalibrationLookup {
    std::string profile_id;
    std::size_t bin_index = 0;
    double raw_confidence = 0.0;
    double calibrated_confidence = 0.0;
    double conservative_confidence = 0.0;
    std::uint64_t samples = 0;
};

struct PolicyCalibrationLoadOptions {
    std::size_t maximum_bins = 4'096;
    std::uintmax_t maximum_payload_bytes = 1'048'576ULL;
};

class PolicyCalibrationProfile {
  public:
    PolicyCalibrationProfile() = default;

    static Result<PolicyCalibrationProfile> create(PolicyCalibrationProfileInput input);

    const std::string& id() const noexcept { return id_; }
    const std::string& policy_id() const noexcept { return policy_id_; }
    const std::string& policy_model_digest() const noexcept { return policy_model_digest_; }
    const std::string& scope_id() const noexcept { return scope_id_; }
    const std::string& task_id() const noexcept { return task_id_; }
    const std::string& dataset_digest() const noexcept { return dataset_digest_; }
    const std::string& method() const noexcept { return method_; }
    const std::string& method_version() const noexcept { return method_version_; }
    const std::string& outcome_definition() const noexcept { return outcome_definition_; }
    const std::string& state_uncertainty_unit() const noexcept { return state_uncertainty_unit_; }
    const std::string& action_uncertainty_unit() const noexcept { return action_uncertainty_unit_; }
    const std::vector<PolicyCalibrationBin>& bins() const noexcept { return bins_; }
    std::uint64_t sample_count() const noexcept { return sample_count_; }
    double expected_calibration_error() const noexcept { return expected_calibration_error_; }
    double maximum_calibration_error() const noexcept { return maximum_calibration_error_; }

    bool valid() const;
    Result<PolicyCalibrationLookup> lookup(double raw_confidence) const;

    Result<void> save(const std::filesystem::path& path, const SaveOptions& options = {}) const;
    static Result<PolicyCalibrationProfile> load(const std::filesystem::path& path,
                                                 const PolicyCalibrationLoadOptions& options = {});

  private:
    std::string id_;
    std::string policy_id_;
    std::string policy_model_digest_;
    std::string scope_id_;
    std::string task_id_;
    std::string dataset_digest_;
    std::string method_;
    std::string method_version_;
    std::string outcome_definition_;
    std::string state_uncertainty_unit_;
    std::string action_uncertainty_unit_;
    std::vector<PolicyCalibrationBin> bins_;
    std::uint64_t sample_count_ = 0;
    double expected_calibration_error_ = 0.0;
    double maximum_calibration_error_ = 0.0;
};

enum class PolicyCalibrationDriftStatus : std::uint8_t {
    InsufficientData = 0,
    Stable = 1,
    DriftDetected = 2,
};

enum class PolicyCalibrationDriftReason : std::uint8_t {
    InsufficientTotalSamples = 0,
    InsufficientBinSamples = 1,
    ConfidenceDistributionShift = 2,
    ExpectedCalibrationErrorExceeded = 3,
    OverallSuccessRateDropExceeded = 4,
    BinSuccessRateDropExceeded = 5,
};

struct PolicyCalibrationWindowBinInput {
    std::uint64_t samples = 0;
    std::uint64_t successes = 0;
};

struct PolicyCalibrationWindowInput {
    std::string window_id;
    std::uint64_t sequence = 0;
    std::string source_digest;
    std::vector<PolicyCalibrationWindowBinInput> bins;
};

struct PolicyCalibrationDriftOptions {
    std::uint64_t minimum_total_samples = 1'000;
    std::uint64_t minimum_bin_samples = 30;
    double maximum_total_variation_distance = 0.1;
    double maximum_expected_calibration_error = 0.1;
    double maximum_overall_success_rate_drop = 0.1;
    double maximum_bin_success_rate_drop = 0.2;
};

struct PolicyCalibrationWindowBin {
    std::uint64_t samples = 0;
    std::uint64_t successes = 0;
    double baseline_fraction = 0.0;
    double observed_fraction = 0.0;
    double baseline_success_rate = 0.0;
    bool outcome_rate_available = false;
    double observed_success_rate = 0.0;
    double success_rate_drop = 0.0;
    double absolute_calibration_error = 0.0;
};

struct PolicyCalibrationDriftReport {
    std::string id;
    std::string profile_id;
    std::string window_id;
    std::uint64_t window_sequence = 0;
    std::string source_digest;
    PolicyCalibrationDriftOptions options;
    PolicyCalibrationDriftStatus status = PolicyCalibrationDriftStatus::InsufficientData;
    std::vector<PolicyCalibrationDriftReason> reasons;
    std::uint64_t sample_count = 0;
    double total_variation_distance = 0.0;
    double baseline_success_rate = 0.0;
    double observed_success_rate = 0.0;
    double overall_success_rate_drop = 0.0;
    double expected_calibration_error = 0.0;
    double maximum_calibration_error = 0.0;
    double maximum_bin_success_rate_drop = 0.0;
    std::vector<PolicyCalibrationWindowBin> bins;
};

Result<PolicyCalibrationDriftReport>
assess_policy_calibration_drift(const PolicyCalibrationProfile& profile, PolicyCalibrationWindowInput window,
                                const PolicyCalibrationDriftOptions& options = {});

enum class PolicyCalibrationLifecycleState : std::uint8_t {
    PendingReview = 0,
    Active = 1,
    Quarantined = 2,
    Retired = 3,
};

enum class PolicyCalibrationLifecycleEventType : std::uint8_t {
    Registered = 0,
    DriftAssessed = 1,
    StateTransition = 2,
};

struct PolicyCalibrationLifecycleEvent {
    std::string id;
    std::string parent_id;
    std::uint64_t sequence = 0;
    PolicyCalibrationLifecycleEventType type = PolicyCalibrationLifecycleEventType::Registered;
    PolicyCalibrationLifecycleState previous_state = PolicyCalibrationLifecycleState::PendingReview;
    PolicyCalibrationLifecycleState current_state = PolicyCalibrationLifecycleState::PendingReview;
    std::string report_id;
    std::string detail;
};

struct PolicyCalibrationLifecycleSummary {
    std::uint64_t assessments = 0;
    std::uint64_t stable = 0;
    std::uint64_t insufficient_data = 0;
    std::uint64_t drift_detected = 0;
    std::uint64_t transitions = 0;
};

struct PolicyCalibrationLifecycleLoadOptions {
    std::size_t maximum_reports = 100'000;
    std::size_t maximum_events = 1'000'000;
    std::size_t maximum_total_bins = 1'000'000;
    std::uintmax_t maximum_payload_bytes = 268'435'456ULL;
};

class PolicyCalibrationLifecycle;
Result<void> save_policy_calibration_lifecycle(const PolicyCalibrationLifecycle& lifecycle,
                                               const PolicyCalibrationProfile& profile,
                                               const std::filesystem::path& path, const SaveOptions& options);
Result<PolicyCalibrationLifecycle>
load_policy_calibration_lifecycle(const std::filesystem::path& path, const PolicyCalibrationProfile& profile,
                                  const PolicyCalibrationLifecycleLoadOptions& options);

class PolicyCalibrationLifecycle {
  public:
    PolicyCalibrationLifecycle() = default;

    static Result<PolicyCalibrationLifecycle> create(const PolicyCalibrationProfile& profile);

    const std::string& profile_id() const noexcept { return profile_id_; }
    PolicyCalibrationLifecycleState state() const noexcept { return state_; }
    std::uint64_t generation() const noexcept { return generation_; }
    const std::string& current_event_id() const noexcept { return current_event_id_; }
    const std::string& latest_report_id() const noexcept { return latest_report_id_; }
    const std::vector<PolicyCalibrationDriftReport>& reports() const noexcept { return reports_; }
    const std::vector<PolicyCalibrationLifecycleEvent>& events() const noexcept { return events_; }

    Result<PolicyCalibrationDriftReport>
    assess(const PolicyCalibrationProfile& profile, PolicyCalibrationWindowInput window,
           std::string_view expected_current_event_id, const PolicyCalibrationDriftOptions& options = {},
           std::size_t maximum_reports = 100'000, std::size_t maximum_events = 1'000'000);
    Result<PolicyCalibrationLifecycleEvent> transition(const PolicyCalibrationProfile& profile,
                                                       std::string_view expected_current_event_id,
                                                       PolicyCalibrationLifecycleState target_state,
                                                       std::string detail,
                                                       std::size_t maximum_events = 1'000'000);
    Result<PolicyCalibrationDriftReport> latest_report() const;
    Result<PolicyCalibrationDriftReport> report(std::string_view report_id) const;
    PolicyCalibrationLifecycleSummary summary() const noexcept;
    bool deployment_ready() const noexcept;
    bool valid(const PolicyCalibrationProfile& profile) const;

    Result<void> save(const std::filesystem::path& path, const PolicyCalibrationProfile& profile,
                      const SaveOptions& options = {}) const;
    static Result<PolicyCalibrationLifecycle> load(const std::filesystem::path& path,
                                                   const PolicyCalibrationProfile& profile,
                                                   const PolicyCalibrationLifecycleLoadOptions& options = {});

  private:
    friend Result<void> save_policy_calibration_lifecycle(const PolicyCalibrationLifecycle&,
                                                          const PolicyCalibrationProfile&,
                                                          const std::filesystem::path&, const SaveOptions&);
    friend Result<PolicyCalibrationLifecycle>
    load_policy_calibration_lifecycle(const std::filesystem::path&, const PolicyCalibrationProfile&,
                                      const PolicyCalibrationLifecycleLoadOptions&);

    std::string profile_id_;
    PolicyCalibrationLifecycleState state_ = PolicyCalibrationLifecycleState::PendingReview;
    std::uint64_t generation_ = 0;
    std::string current_event_id_;
    std::string latest_report_id_;
    std::vector<PolicyCalibrationDriftReport> reports_;
    std::vector<PolicyCalibrationLifecycleEvent> events_;
};

struct CalibratedPolicyGateOptions {
    std::uint64_t minimum_total_samples = 1'000;
    std::uint64_t minimum_bin_samples = 30;
    double maximum_expected_calibration_error = 0.1;
    double maximum_bin_calibration_error = 0.2;
    PolicyGateOptions policy;
};

struct CalibratedPolicyApplication {
    std::string id;
    std::string profile_id;
    PolicyProposalMetadata raw_metadata;
    PolicyProposalMetadata effective_metadata;
    std::size_t bin_index = 0;
    std::uint64_t bin_samples = 0;
    double calibrated_confidence = 0.0;
    double conservative_confidence = 0.0;
};

struct CalibratedPolicyBatchReport {
    std::string profile_id;
    std::string lifecycle_event_id;
    std::vector<CalibratedPolicyApplication> applications;
    PolicyBatchReport policy_report;
};

class CalibratedPolicySafetyGate {
  public:
    Result<CalibratedPolicyBatchReport>
    check_proposals(const PolicyCalibrationProfile& profile, std::string_view expected_scope_id,
                    std::string_view expected_policy_model_digest, const SerialRobotModel& robot,
                    const SceneSnapshot& scene, const SafeAtlas& atlas, std::span<const double> current,
                    std::span<const PolicyProposal> proposals,
                    const CalibratedPolicyGateOptions& options = {});

    Result<CalibratedPolicyBatchReport> check_proposals_guarded(
        const PolicyCalibrationProfile& profile, const PolicyCalibrationLifecycle& lifecycle,
        std::string_view expected_lifecycle_event_id, std::string_view expected_scope_id,
        std::string_view expected_policy_model_digest, const SerialRobotModel& robot,
        const SceneSnapshot& scene, const SafeAtlas& atlas, std::span<const double> current,
        std::span<const PolicyProposal> proposals, const CalibratedPolicyGateOptions& options = {});

    PolicyTelemetrySnapshot telemetry() const { return gate_.telemetry(); }
    void reset_telemetry() { gate_.reset_telemetry(); }

  private:
    LearningPolicySafetyGate gate_;
};

std::string policy_calibration_drift_status_name(PolicyCalibrationDriftStatus status);
std::string policy_calibration_drift_reason_name(PolicyCalibrationDriftReason reason);
std::string policy_calibration_lifecycle_state_name(PolicyCalibrationLifecycleState state);
std::string policy_calibration_lifecycle_event_type_name(PolicyCalibrationLifecycleEventType type);

} // namespace rbfsafe
