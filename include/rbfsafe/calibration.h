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

    PolicyTelemetrySnapshot telemetry() const { return gate_.telemetry(); }
    void reset_telemetry() { gate_.reset_telemetry(); }

  private:
    LearningPolicySafetyGate gate_;
};

} // namespace rbfsafe
