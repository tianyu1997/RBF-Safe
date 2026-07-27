#include <rbfsafe/calibration.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumDetailBytes = 1'024;
constexpr std::uint64_t kMaximumSamples = 1'000'000'000'000ULL;

bool valid_text(std::string_view value, std::size_t maximum_bytes) {
    return !value.empty() && value.size() <= maximum_bytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool finite_unit_interval(double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; }

bool valid_options(const PolicyCalibrationDriftOptions& options) {
    return options.minimum_total_samples > 0 && options.minimum_total_samples <= kMaximumSamples &&
           options.minimum_bin_samples > 0 && options.minimum_bin_samples <= kMaximumSamples &&
           finite_unit_interval(options.maximum_total_variation_distance) &&
           finite_unit_interval(options.maximum_expected_calibration_error) &&
           finite_unit_interval(options.maximum_overall_success_rate_drop) &&
           finite_unit_interval(options.maximum_bin_success_rate_drop);
}

internal::Json drift_options_json(const PolicyCalibrationDriftOptions& options) {
    return internal::Json::Object{
        {"maximum_bin_success_rate_drop", options.maximum_bin_success_rate_drop},
        {"maximum_expected_calibration_error", options.maximum_expected_calibration_error},
        {"maximum_overall_success_rate_drop", options.maximum_overall_success_rate_drop},
        {"maximum_total_variation_distance", options.maximum_total_variation_distance},
        {"minimum_bin_samples", std::to_string(options.minimum_bin_samples)},
        {"minimum_total_samples", std::to_string(options.minimum_total_samples)},
    };
}

std::string drift_report_identity(const PolicyCalibrationProfile& profile,
                                  const PolicyCalibrationWindowInput& window,
                                  const PolicyCalibrationDriftOptions& options) {
    internal::Json::Array bins;
    bins.reserve(window.bins.size());
    for (const auto& bin : window.bins) {
        bins.emplace_back(internal::Json::Object{{"samples", std::to_string(bin.samples)},
                                                 {"successes", std::to_string(bin.successes)}});
    }
    return internal::sha256(internal::Json(internal::Json::Object{
                                               {"bins", std::move(bins)},
                                               {"options", drift_options_json(options)},
                                               {"profile_id", profile.id()},
                                               {"source_digest", window.source_digest},
                                               {"window_id", window.window_id},
                                               {"window_sequence", std::to_string(window.sequence)},
                                           })
                                .dump(false));
}

std::string lifecycle_event_identity(std::string_view profile_id,
                                     const PolicyCalibrationLifecycleEvent& event) {
    return internal::sha256(internal::Json(internal::Json::Object{
                                               {"current_state", static_cast<int>(event.current_state)},
                                               {"detail", event.detail},
                                               {"parent_id", event.parent_id},
                                               {"previous_state", static_cast<int>(event.previous_state)},
                                               {"profile_id", std::string(profile_id)},
                                               {"report_id", event.report_id},
                                               {"sequence", std::to_string(event.sequence)},
                                               {"type", static_cast<int>(event.type)},
                                           })
                                .dump(false));
}

bool approximately_equal(double first, double second) {
    return std::abs(first - second) <= 1e-12 * std::max({1.0, std::abs(first), std::abs(second)});
}

bool equal_options(const PolicyCalibrationDriftOptions& first, const PolicyCalibrationDriftOptions& second) {
    return first.minimum_total_samples == second.minimum_total_samples &&
           first.minimum_bin_samples == second.minimum_bin_samples &&
           approximately_equal(first.maximum_total_variation_distance,
                               second.maximum_total_variation_distance) &&
           approximately_equal(first.maximum_expected_calibration_error,
                               second.maximum_expected_calibration_error) &&
           approximately_equal(first.maximum_overall_success_rate_drop,
                               second.maximum_overall_success_rate_drop) &&
           approximately_equal(first.maximum_bin_success_rate_drop, second.maximum_bin_success_rate_drop);
}

bool equal_report(const PolicyCalibrationDriftReport& first, const PolicyCalibrationDriftReport& second) {
    if (first.id != second.id || first.profile_id != second.profile_id ||
        first.window_id != second.window_id || first.window_sequence != second.window_sequence ||
        first.source_digest != second.source_digest || !equal_options(first.options, second.options) ||
        first.status != second.status || first.reasons != second.reasons ||
        first.sample_count != second.sample_count || first.bins.size() != second.bins.size() ||
        !approximately_equal(first.total_variation_distance, second.total_variation_distance) ||
        !approximately_equal(first.baseline_success_rate, second.baseline_success_rate) ||
        !approximately_equal(first.observed_success_rate, second.observed_success_rate) ||
        !approximately_equal(first.overall_success_rate_drop, second.overall_success_rate_drop) ||
        !approximately_equal(first.expected_calibration_error, second.expected_calibration_error) ||
        !approximately_equal(first.maximum_calibration_error, second.maximum_calibration_error) ||
        !approximately_equal(first.maximum_bin_success_rate_drop, second.maximum_bin_success_rate_drop)) {
        return false;
    }
    for (std::size_t index = 0; index < first.bins.size(); ++index) {
        const auto& left = first.bins[index];
        const auto& right = second.bins[index];
        if (left.samples != right.samples || left.successes != right.successes ||
            left.outcome_rate_available != right.outcome_rate_available ||
            !approximately_equal(left.baseline_fraction, right.baseline_fraction) ||
            !approximately_equal(left.observed_fraction, right.observed_fraction) ||
            !approximately_equal(left.baseline_success_rate, right.baseline_success_rate) ||
            !approximately_equal(left.observed_success_rate, right.observed_success_rate) ||
            !approximately_equal(left.success_rate_drop, right.success_rate_drop) ||
            !approximately_equal(left.absolute_calibration_error, right.absolute_calibration_error)) {
            return false;
        }
    }
    return true;
}

PolicyCalibrationLifecycleState state_after_assessment(PolicyCalibrationLifecycleState state,
                                                       PolicyCalibrationDriftStatus status) {
    if (status == PolicyCalibrationDriftStatus::DriftDetected)
        return PolicyCalibrationLifecycleState::Quarantined;
    if (status == PolicyCalibrationDriftStatus::InsufficientData &&
        state == PolicyCalibrationLifecycleState::Active) {
        return PolicyCalibrationLifecycleState::PendingReview;
    }
    return state;
}

bool transition_allowed(PolicyCalibrationLifecycleState source, PolicyCalibrationLifecycleState target,
                        const PolicyCalibrationDriftReport* latest_report) {
    if (source == target || source == PolicyCalibrationLifecycleState::Retired)
        return false;
    switch (source) {
    case PolicyCalibrationLifecycleState::PendingReview:
        if (target == PolicyCalibrationLifecycleState::Active) {
            return latest_report != nullptr && latest_report->status == PolicyCalibrationDriftStatus::Stable;
        }
        return target == PolicyCalibrationLifecycleState::Quarantined ||
               target == PolicyCalibrationLifecycleState::Retired;
    case PolicyCalibrationLifecycleState::Active:
        return target == PolicyCalibrationLifecycleState::PendingReview ||
               target == PolicyCalibrationLifecycleState::Quarantined ||
               target == PolicyCalibrationLifecycleState::Retired;
    case PolicyCalibrationLifecycleState::Quarantined:
        return target == PolicyCalibrationLifecycleState::PendingReview ||
               target == PolicyCalibrationLifecycleState::Retired;
    case PolicyCalibrationLifecycleState::Retired:
        return false;
    }
    return false;
}

PolicyCalibrationWindowInput window_input(const PolicyCalibrationDriftReport& report) {
    PolicyCalibrationWindowInput result;
    result.window_id = report.window_id;
    result.sequence = report.window_sequence;
    result.source_digest = report.source_digest;
    result.bins.reserve(report.bins.size());
    for (const auto& bin : report.bins)
        result.bins.push_back({bin.samples, bin.successes});
    return result;
}

bool equal_event(const PolicyCalibrationLifecycleEvent& first,
                 const PolicyCalibrationLifecycleEvent& second) {
    return first.id == second.id && first.parent_id == second.parent_id &&
           first.sequence == second.sequence && first.type == second.type &&
           first.previous_state == second.previous_state && first.current_state == second.current_state &&
           first.report_id == second.report_id && first.detail == second.detail;
}

} // namespace

Result<PolicyCalibrationDriftReport>
assess_policy_calibration_drift(const PolicyCalibrationProfile& profile, PolicyCalibrationWindowInput window,
                                const PolicyCalibrationDriftOptions& options) {
    if (!profile.valid() || !valid_text(window.window_id, kMaximumIdentifierBytes) ||
        !internal::valid_sha256(window.source_digest) || window.bins.size() != profile.bins().size() ||
        !valid_options(options)) {
        return Result<PolicyCalibrationDriftReport>::failure(StatusCode::InvalidArgument,
                                                             "policy calibration drift input is invalid");
    }

    std::uint64_t total_samples = 0;
    std::uint64_t total_successes = 0;
    bool insufficient_bin_samples = false;
    for (const auto& bin : window.bins) {
        if (bin.samples > kMaximumSamples || total_samples > kMaximumSamples - bin.samples) {
            return Result<PolicyCalibrationDriftReport>::failure(
                StatusCode::ResourceLimit, "policy calibration window exceeds sample limit");
        }
        if (bin.successes > bin.samples) {
            return Result<PolicyCalibrationDriftReport>::failure(StatusCode::InvalidArgument,
                                                                 "policy calibration window bin is invalid");
        }
        total_samples += bin.samples;
        total_successes += bin.successes;
        insufficient_bin_samples =
            insufficient_bin_samples || (bin.samples > 0 && bin.samples < options.minimum_bin_samples);
    }

    PolicyCalibrationDriftReport report;
    report.id = drift_report_identity(profile, window, options);
    report.profile_id = profile.id();
    report.window_id = std::move(window.window_id);
    report.window_sequence = window.sequence;
    report.source_digest = std::move(window.source_digest);
    report.options = options;
    report.sample_count = total_samples;
    report.baseline_success_rate = 0.0;
    for (const auto& bin : profile.bins()) {
        report.baseline_success_rate +=
            static_cast<double>(bin.successes) / static_cast<double>(profile.sample_count());
    }
    report.observed_success_rate =
        total_samples == 0 ? 0.0 : static_cast<double>(total_successes) / static_cast<double>(total_samples);
    report.overall_success_rate_drop =
        std::max(0.0, report.baseline_success_rate - report.observed_success_rate);

    long double weighted_error = 0.0L;
    double variation_sum = 0.0;
    report.bins.reserve(window.bins.size());
    for (std::size_t index = 0; index < window.bins.size(); ++index) {
        const auto& baseline = profile.bins()[index];
        const auto& observed = window.bins[index];
        PolicyCalibrationWindowBin result;
        result.samples = observed.samples;
        result.successes = observed.successes;
        result.baseline_fraction =
            static_cast<double>(baseline.samples) / static_cast<double>(profile.sample_count());
        result.observed_fraction =
            total_samples == 0 ? 0.0
                               : static_cast<double>(observed.samples) / static_cast<double>(total_samples);
        result.baseline_success_rate = baseline.observed_success_rate;
        result.outcome_rate_available = observed.samples != 0;
        if (result.outcome_rate_available) {
            result.observed_success_rate =
                static_cast<double>(observed.successes) / static_cast<double>(observed.samples);
            result.success_rate_drop =
                std::max(0.0, result.baseline_success_rate - result.observed_success_rate);
            result.absolute_calibration_error =
                std::abs(baseline.mean_confidence - result.observed_success_rate);
            weighted_error += static_cast<long double>(observed.samples) * result.absolute_calibration_error;
            report.maximum_calibration_error =
                std::max(report.maximum_calibration_error, result.absolute_calibration_error);
            report.maximum_bin_success_rate_drop =
                std::max(report.maximum_bin_success_rate_drop, result.success_rate_drop);
        }
        variation_sum += std::abs(result.baseline_fraction - result.observed_fraction);
        report.bins.push_back(result);
    }
    report.total_variation_distance = total_samples == 0 ? 1.0 : 0.5 * variation_sum;
    report.expected_calibration_error =
        total_samples == 0 ? 0.0 : static_cast<double>(weighted_error / total_samples);

    if (total_samples < options.minimum_total_samples) {
        report.reasons.push_back(PolicyCalibrationDriftReason::InsufficientTotalSamples);
    }
    if (insufficient_bin_samples)
        report.reasons.push_back(PolicyCalibrationDriftReason::InsufficientBinSamples);
    if (!report.reasons.empty()) {
        report.status = PolicyCalibrationDriftStatus::InsufficientData;
        return report;
    }
    if (report.total_variation_distance > options.maximum_total_variation_distance)
        report.reasons.push_back(PolicyCalibrationDriftReason::ConfidenceDistributionShift);
    if (report.expected_calibration_error > options.maximum_expected_calibration_error)
        report.reasons.push_back(PolicyCalibrationDriftReason::ExpectedCalibrationErrorExceeded);
    if (report.overall_success_rate_drop > options.maximum_overall_success_rate_drop)
        report.reasons.push_back(PolicyCalibrationDriftReason::OverallSuccessRateDropExceeded);
    if (report.maximum_bin_success_rate_drop > options.maximum_bin_success_rate_drop)
        report.reasons.push_back(PolicyCalibrationDriftReason::BinSuccessRateDropExceeded);
    report.status = report.reasons.empty() ? PolicyCalibrationDriftStatus::Stable
                                           : PolicyCalibrationDriftStatus::DriftDetected;
    return report;
}

Result<PolicyCalibrationLifecycle>
PolicyCalibrationLifecycle::create(const PolicyCalibrationProfile& profile) {
    if (!profile.valid()) {
        return Result<PolicyCalibrationLifecycle>::failure(StatusCode::InvalidArgument,
                                                           "policy calibration lifecycle profile is invalid");
    }
    PolicyCalibrationLifecycle result;
    result.profile_id_ = profile.id();
    PolicyCalibrationLifecycleEvent event;
    event.detail = "calibration profile registered";
    event.id = lifecycle_event_identity(result.profile_id_, event);
    result.events_.push_back(event);
    result.current_event_id_ = event.id;
    return result;
}

Result<PolicyCalibrationDriftReport> PolicyCalibrationLifecycle::assess(
    const PolicyCalibrationProfile& profile, PolicyCalibrationWindowInput window,
    std::string_view expected_current_event_id, const PolicyCalibrationDriftOptions& options,
    std::size_t maximum_reports, std::size_t maximum_events) {
    if (!valid(profile) || maximum_reports == 0 || maximum_events == 0) {
        return Result<PolicyCalibrationDriftReport>::failure(
            StatusCode::InvalidArgument, "policy calibration lifecycle assessment input is invalid");
    }
    if (expected_current_event_id != current_event_id_) {
        return Result<PolicyCalibrationDriftReport>::failure(
            StatusCode::IdentityMismatch, "policy calibration lifecycle head changed", current_event_id_);
    }
    if (state_ == PolicyCalibrationLifecycleState::Retired) {
        return Result<PolicyCalibrationDriftReport>::failure(
            StatusCode::InvalidArgument, "retired calibration lifecycle cannot be assessed");
    }
    if (reports_.size() >= maximum_reports || events_.size() >= maximum_events ||
        generation_ == std::numeric_limits<std::uint64_t>::max()) {
        return Result<PolicyCalibrationDriftReport>::failure(
            StatusCode::ResourceLimit, "policy calibration lifecycle resource limit reached");
    }
    if (!reports_.empty() && window.sequence <= reports_.back().window_sequence) {
        return Result<PolicyCalibrationDriftReport>::failure(
            StatusCode::InvalidArgument, "policy calibration window sequence must increase");
    }
    auto report = assess_policy_calibration_drift(profile, std::move(window), options);
    if (!report)
        return report.error();

    PolicyCalibrationLifecycleEvent event;
    event.parent_id = current_event_id_;
    event.sequence = generation_ + 1;
    event.type = PolicyCalibrationLifecycleEventType::DriftAssessed;
    event.previous_state = state_;
    event.current_state = state_after_assessment(state_, report.value().status);
    event.report_id = report.value().id;
    event.detail = policy_calibration_drift_status_name(report.value().status);
    event.id = lifecycle_event_identity(profile_id_, event);
    reports_.push_back(report.value());
    events_.push_back(event);
    state_ = event.current_state;
    generation_ = event.sequence;
    current_event_id_ = event.id;
    latest_report_id_ = report.value().id;
    return report;
}

Result<PolicyCalibrationLifecycleEvent> PolicyCalibrationLifecycle::transition(
    const PolicyCalibrationProfile& profile, std::string_view expected_current_event_id,
    PolicyCalibrationLifecycleState target_state, std::string detail, std::size_t maximum_events) {
    if (!valid(profile) || maximum_events == 0 || !valid_text(detail, kMaximumDetailBytes)) {
        return Result<PolicyCalibrationLifecycleEvent>::failure(
            StatusCode::InvalidArgument, "policy calibration lifecycle transition input is invalid");
    }
    if (expected_current_event_id != current_event_id_) {
        return Result<PolicyCalibrationLifecycleEvent>::failure(
            StatusCode::IdentityMismatch, "policy calibration lifecycle head changed", current_event_id_);
    }
    if (events_.size() >= maximum_events || generation_ == std::numeric_limits<std::uint64_t>::max()) {
        return Result<PolicyCalibrationLifecycleEvent>::failure(
            StatusCode::ResourceLimit, "policy calibration lifecycle event limit reached");
    }
    const PolicyCalibrationDriftReport* latest = reports_.empty() ? nullptr : &reports_.back();
    if (!transition_allowed(state_, target_state, latest)) {
        return Result<PolicyCalibrationLifecycleEvent>::failure(
            StatusCode::InvalidArgument, "policy calibration lifecycle transition is not allowed");
    }
    PolicyCalibrationLifecycleEvent event;
    event.parent_id = current_event_id_;
    event.sequence = generation_ + 1;
    event.type = PolicyCalibrationLifecycleEventType::StateTransition;
    event.previous_state = state_;
    event.current_state = target_state;
    event.detail = std::move(detail);
    event.id = lifecycle_event_identity(profile_id_, event);
    events_.push_back(event);
    state_ = target_state;
    generation_ = event.sequence;
    current_event_id_ = event.id;
    return event;
}

Result<PolicyCalibrationDriftReport> PolicyCalibrationLifecycle::latest_report() const {
    if (reports_.empty()) {
        return Result<PolicyCalibrationDriftReport>::failure(
            StatusCode::InvalidArgument, "policy calibration lifecycle has no drift report");
    }
    return reports_.back();
}

Result<PolicyCalibrationDriftReport> PolicyCalibrationLifecycle::report(std::string_view report_id) const {
    if (!internal::valid_sha256(std::string(report_id))) {
        return Result<PolicyCalibrationDriftReport>::failure(StatusCode::InvalidArgument,
                                                             "policy calibration drift report ID is invalid");
    }
    const auto found = std::find_if(reports_.begin(), reports_.end(),
                                    [&](const auto& candidate) { return candidate.id == report_id; });
    if (found == reports_.end()) {
        return Result<PolicyCalibrationDriftReport>::failure(
            StatusCode::InvalidArgument, "policy calibration drift report is not registered",
            std::string(report_id));
    }
    return *found;
}

PolicyCalibrationLifecycleSummary PolicyCalibrationLifecycle::summary() const noexcept {
    PolicyCalibrationLifecycleSummary result;
    result.assessments = static_cast<std::uint64_t>(reports_.size());
    for (const auto& report : reports_) {
        switch (report.status) {
        case PolicyCalibrationDriftStatus::InsufficientData:
            ++result.insufficient_data;
            break;
        case PolicyCalibrationDriftStatus::Stable:
            ++result.stable;
            break;
        case PolicyCalibrationDriftStatus::DriftDetected:
            ++result.drift_detected;
            break;
        }
    }
    result.transitions =
        static_cast<std::uint64_t>(std::count_if(events_.begin(), events_.end(), [](const auto& event) {
            return event.type == PolicyCalibrationLifecycleEventType::StateTransition;
        }));
    return result;
}

bool PolicyCalibrationLifecycle::deployment_ready() const noexcept {
    return state_ == PolicyCalibrationLifecycleState::Active && !reports_.empty() &&
           reports_.back().status == PolicyCalibrationDriftStatus::Stable &&
           latest_report_id_ == reports_.back().id;
}

bool PolicyCalibrationLifecycle::valid(const PolicyCalibrationProfile& profile) const {
    if (!profile.valid() || profile_id_ != profile.id() || events_.empty() ||
        generation_ != static_cast<std::uint64_t>(events_.size() - 1) ||
        current_event_id_ != events_.back().id ||
        (reports_.empty() ? !latest_report_id_.empty() : latest_report_id_ != reports_.back().id)) {
        return false;
    }
    auto initial = create(profile);
    if (!initial || !equal_event(events_.front(), initial.value().events_.front()))
        return false;

    PolicyCalibrationLifecycleState replay_state = PolicyCalibrationLifecycleState::PendingReview;
    std::size_t report_index = 0;
    std::uint64_t previous_window_sequence = 0;
    bool have_window = false;
    for (std::size_t index = 1; index < events_.size(); ++index) {
        const auto& event = events_[index];
        if (event.sequence != static_cast<std::uint64_t>(index) || event.parent_id != events_[index - 1].id ||
            event.previous_state != replay_state ||
            event.id != lifecycle_event_identity(profile_id_, event)) {
            return false;
        }
        if (event.type == PolicyCalibrationLifecycleEventType::DriftAssessed) {
            if (report_index >= reports_.size())
                return false;
            const auto& stored = reports_[report_index];
            if ((have_window && stored.window_sequence <= previous_window_sequence) ||
                event.report_id != stored.id ||
                event.detail != policy_calibration_drift_status_name(stored.status)) {
                return false;
            }
            auto rebuilt = assess_policy_calibration_drift(profile, window_input(stored), stored.options);
            if (!rebuilt || !equal_report(stored, rebuilt.value()))
                return false;
            replay_state = state_after_assessment(replay_state, stored.status);
            if (event.current_state != replay_state)
                return false;
            previous_window_sequence = stored.window_sequence;
            have_window = true;
            ++report_index;
        } else if (event.type == PolicyCalibrationLifecycleEventType::StateTransition) {
            const auto* latest = report_index == 0 ? nullptr : &reports_[report_index - 1];
            if (!event.report_id.empty() || !valid_text(event.detail, kMaximumDetailBytes) ||
                !transition_allowed(replay_state, event.current_state, latest)) {
                return false;
            }
            replay_state = event.current_state;
        } else {
            return false;
        }
    }
    return report_index == reports_.size() && replay_state == state_;
}

Result<void> PolicyCalibrationLifecycle::save(const std::filesystem::path& path,
                                              const PolicyCalibrationProfile& profile,
                                              const SaveOptions& options) const {
    return save_policy_calibration_lifecycle(*this, profile, path, options);
}

Result<PolicyCalibrationLifecycle>
PolicyCalibrationLifecycle::load(const std::filesystem::path& path, const PolicyCalibrationProfile& profile,
                                 const PolicyCalibrationLifecycleLoadOptions& options) {
    return load_policy_calibration_lifecycle(path, profile, options);
}

Result<CalibratedPolicyBatchReport> CalibratedPolicySafetyGate::check_proposals_guarded(
    const PolicyCalibrationProfile& profile, const PolicyCalibrationLifecycle& lifecycle,
    std::string_view expected_lifecycle_event_id, std::string_view expected_scope_id,
    std::string_view expected_policy_model_digest, const SerialRobotModel& robot, const SceneSnapshot& scene,
    const SafeAtlas& atlas, std::span<const double> current, std::span<const PolicyProposal> proposals,
    const CalibratedPolicyGateOptions& options) {
    if (!lifecycle.valid(profile) || !lifecycle.deployment_ready()) {
        return Result<CalibratedPolicyBatchReport>::failure(
            StatusCode::InvalidArgument, "policy calibration lifecycle is not deployment-ready",
            lifecycle.current_event_id());
    }
    if (!internal::valid_sha256(std::string(expected_lifecycle_event_id)) ||
        expected_lifecycle_event_id != lifecycle.current_event_id()) {
        return Result<CalibratedPolicyBatchReport>::failure(
            StatusCode::IdentityMismatch, "policy calibration lifecycle head does not match",
            lifecycle.current_event_id());
    }
    auto result = check_proposals(profile, expected_scope_id, expected_policy_model_digest, robot, scene,
                                  atlas, current, proposals, options);
    if (!result)
        return result.error();
    result.value().lifecycle_event_id = lifecycle.current_event_id();
    return result;
}

std::string policy_calibration_drift_status_name(PolicyCalibrationDriftStatus status) {
    switch (status) {
    case PolicyCalibrationDriftStatus::InsufficientData:
        return "insufficient-data";
    case PolicyCalibrationDriftStatus::Stable:
        return "stable";
    case PolicyCalibrationDriftStatus::DriftDetected:
        return "drift-detected";
    }
    return "unknown";
}

std::string policy_calibration_drift_reason_name(PolicyCalibrationDriftReason reason) {
    switch (reason) {
    case PolicyCalibrationDriftReason::InsufficientTotalSamples:
        return "insufficient-total-samples";
    case PolicyCalibrationDriftReason::InsufficientBinSamples:
        return "insufficient-bin-samples";
    case PolicyCalibrationDriftReason::ConfidenceDistributionShift:
        return "confidence-distribution-shift";
    case PolicyCalibrationDriftReason::ExpectedCalibrationErrorExceeded:
        return "expected-calibration-error-exceeded";
    case PolicyCalibrationDriftReason::OverallSuccessRateDropExceeded:
        return "overall-success-rate-drop-exceeded";
    case PolicyCalibrationDriftReason::BinSuccessRateDropExceeded:
        return "bin-success-rate-drop-exceeded";
    }
    return "unknown";
}

std::string policy_calibration_lifecycle_state_name(PolicyCalibrationLifecycleState state) {
    switch (state) {
    case PolicyCalibrationLifecycleState::PendingReview:
        return "pending-review";
    case PolicyCalibrationLifecycleState::Active:
        return "active";
    case PolicyCalibrationLifecycleState::Quarantined:
        return "quarantined";
    case PolicyCalibrationLifecycleState::Retired:
        return "retired";
    }
    return "unknown";
}

std::string policy_calibration_lifecycle_event_type_name(PolicyCalibrationLifecycleEventType type) {
    switch (type) {
    case PolicyCalibrationLifecycleEventType::Registered:
        return "registered";
    case PolicyCalibrationLifecycleEventType::DriftAssessed:
        return "drift-assessed";
    case PolicyCalibrationLifecycleEventType::StateTransition:
        return "state-transition";
    }
    return "unknown";
}

} // namespace rbfsafe
