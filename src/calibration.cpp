#include <rbfsafe/calibration.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kSchema = 1;
constexpr std::size_t kMaximumTextBytes = 256;
constexpr std::size_t kMaximumOutcomeBytes = 1'024;
constexpr std::size_t kMaximumBins = 4'096;
constexpr std::uint64_t kMaximumSamples = 1'000'000'000'000ULL;
constexpr double kWilsonZ95 = 1.959963984540054;

bool valid_text(std::string_view value, std::size_t maximum_bytes) {
    return !value.empty() && value.size() <= maximum_bytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

double wilson_lower_bound(std::uint64_t successes, std::uint64_t samples) {
    const double count = static_cast<double>(samples);
    const double probability = static_cast<double>(successes) / count;
    const double z_squared = kWilsonZ95 * kWilsonZ95;
    const double denominator = 1.0 + z_squared / count;
    const double center = probability + z_squared / (2.0 * count);
    const double margin =
        kWilsonZ95 * std::sqrt((probability * (1.0 - probability) + z_squared / (4.0 * count)) / count);
    return std::max(0.0, (center - margin) / denominator);
}

internal::Json bin_input_json(const PolicyCalibrationBinInput& bin) {
    return internal::Json::Object{
        {"lower_confidence", bin.lower_confidence}, {"mean_confidence", bin.mean_confidence},
        {"samples", std::to_string(bin.samples)},   {"successes", std::to_string(bin.successes)},
        {"upper_confidence", bin.upper_confidence},
    };
}

internal::Json profile_identity_json(const PolicyCalibrationProfileInput& input) {
    internal::Json::Array bins;
    bins.reserve(input.bins.size());
    for (const auto& bin : input.bins)
        bins.push_back(bin_input_json(bin));
    return internal::Json::Object{
        {"action_uncertainty_unit", input.action_uncertainty_unit},
        {"bins", std::move(bins)},
        {"dataset_digest", input.dataset_digest},
        {"method", input.method},
        {"method_version", input.method_version},
        {"outcome_definition", input.outcome_definition},
        {"policy_id", input.policy_id},
        {"policy_model_digest", input.policy_model_digest},
        {"scope_id", input.scope_id},
        {"state_uncertainty_unit", input.state_uncertainty_unit},
        {"task_id", input.task_id},
    };
}

PolicyCalibrationProfileInput profile_input(const PolicyCalibrationProfile& profile) {
    PolicyCalibrationProfileInput input;
    input.policy_id = profile.policy_id();
    input.policy_model_digest = profile.policy_model_digest();
    input.scope_id = profile.scope_id();
    input.task_id = profile.task_id();
    input.dataset_digest = profile.dataset_digest();
    input.method = profile.method();
    input.method_version = profile.method_version();
    input.outcome_definition = profile.outcome_definition();
    input.state_uncertainty_unit = profile.state_uncertainty_unit();
    input.action_uncertainty_unit = profile.action_uncertainty_unit();
    input.bins.reserve(profile.bins().size());
    for (const auto& bin : profile.bins()) {
        input.bins.push_back(
            {bin.lower_confidence, bin.upper_confidence, bin.mean_confidence, bin.samples, bin.successes});
    }
    return input;
}

internal::Json storage_json(const PolicyCalibrationProfile& profile) {
    auto input = profile_input(profile);
    internal::Json::Array bins;
    bins.reserve(profile.bins().size());
    for (const auto& bin : profile.bins()) {
        bins.emplace_back(internal::Json::Object{
            {"absolute_calibration_error", bin.absolute_calibration_error},
            {"lower_confidence", bin.lower_confidence},
            {"lower_confidence_bound_95", bin.lower_confidence_bound_95},
            {"mean_confidence", bin.mean_confidence},
            {"observed_success_rate", bin.observed_success_rate},
            {"samples", std::to_string(bin.samples)},
            {"successes", std::to_string(bin.successes)},
            {"upper_confidence", bin.upper_confidence},
        });
    }
    return internal::Json::Object{
        {"action_uncertainty_unit", input.action_uncertainty_unit},
        {"bins", std::move(bins)},
        {"dataset_digest", input.dataset_digest},
        {"expected_calibration_error", profile.expected_calibration_error()},
        {"format", "rbfsafe-policy-calibration-profile"},
        {"id", profile.id()},
        {"library_version", kVersion},
        {"maximum_calibration_error", profile.maximum_calibration_error()},
        {"method", input.method},
        {"method_version", input.method_version},
        {"outcome_definition", input.outcome_definition},
        {"policy_id", input.policy_id},
        {"policy_model_digest", input.policy_model_digest},
        {"sample_count", std::to_string(profile.sample_count())},
        {"schema", static_cast<double>(kSchema)},
        {"scope_id", input.scope_id},
        {"state_uncertainty_unit", input.state_uncertainty_unit},
        {"task_id", input.task_id},
    };
}

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

Result<void> publish_file(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                          bool destination_exists) {
    std::error_code error;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(destination, ".backup-");
        std::filesystem::rename(destination, backup, error);
        if (error)
            return Result<void>::failure(StatusCode::IoError, "failed to stage existing calibration profile");
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish calibration profile");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
    }
    return Result<void>::success();
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 std::size_t maximum_bytes) {
    if (!object.is_object())
        return Result<std::string>::failure(StatusCode::CorruptData, "calibration profile is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || !valid_text(value->as_string(), maximum_bytes)) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "calibration profile string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<double> number_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object())
        return Result<double>::failure(StatusCode::CorruptData, "calibration profile is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData, "calibration profile number field is invalid",
                                       std::string(key));
    }
    return value->as_number();
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key, 32);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData,
                                              "calibration profile count field is invalid", std::string(key));
    }
    return result;
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

bool approximately_equal(double first, double second) {
    return std::abs(first - second) <= 1e-12 * std::max({1.0, std::abs(first), std::abs(second)});
}

} // namespace

Result<PolicyCalibrationProfile> PolicyCalibrationProfile::create(PolicyCalibrationProfileInput input) {
    if (!valid_text(input.policy_id, kMaximumTextBytes) ||
        !internal::valid_sha256(input.policy_model_digest) ||
        !valid_text(input.scope_id, kMaximumTextBytes) || !valid_text(input.task_id, kMaximumTextBytes) ||
        !internal::valid_sha256(input.dataset_digest) || !valid_text(input.method, kMaximumTextBytes) ||
        !valid_text(input.method_version, kMaximumTextBytes) ||
        !valid_text(input.outcome_definition, kMaximumOutcomeBytes) ||
        !valid_text(input.state_uncertainty_unit, kMaximumTextBytes) ||
        !valid_text(input.action_uncertainty_unit, kMaximumTextBytes) || input.bins.empty()) {
        return Result<PolicyCalibrationProfile>::failure(StatusCode::InvalidArgument,
                                                         "policy calibration profile input is invalid");
    }
    if (input.bins.size() > kMaximumBins) {
        return Result<PolicyCalibrationProfile>::failure(StatusCode::ResourceLimit,
                                                         "policy calibration profile exceeds bin limit");
    }
    std::uint64_t total_samples = 0;
    long double weighted_error = 0.0L;
    double maximum_error = 0.0;
    std::vector<PolicyCalibrationBin> bins;
    bins.reserve(input.bins.size());
    for (std::size_t index = 0; index < input.bins.size(); ++index) {
        const auto& source = input.bins[index];
        const bool correct_lower = index == 0
                                       ? source.lower_confidence == 0.0
                                       : source.lower_confidence == input.bins[index - 1].upper_confidence;
        const bool correct_upper =
            index + 1 == input.bins.size() ? source.upper_confidence == 1.0 : source.upper_confidence < 1.0;
        if (source.samples > kMaximumSamples || total_samples > kMaximumSamples - source.samples) {
            return Result<PolicyCalibrationProfile>::failure(StatusCode::ResourceLimit,
                                                             "policy calibration sample count exceeds limit");
        }
        if (!std::isfinite(source.lower_confidence) || !std::isfinite(source.upper_confidence) ||
            !std::isfinite(source.mean_confidence) || source.lower_confidence < 0.0 || !correct_lower ||
            !correct_upper || source.lower_confidence >= source.upper_confidence ||
            source.mean_confidence < source.lower_confidence ||
            (index + 1 == input.bins.size() ? source.mean_confidence > source.upper_confidence
                                            : source.mean_confidence >= source.upper_confidence) ||
            source.samples == 0 || source.successes > source.samples) {
            return Result<PolicyCalibrationProfile>::failure(
                StatusCode::InvalidArgument, "policy calibration bin is invalid", std::to_string(index));
        }
        const double observed = static_cast<double>(source.successes) / static_cast<double>(source.samples);
        const double absolute_error = std::abs(source.mean_confidence - observed);
        bins.push_back({source.lower_confidence, source.upper_confidence, source.mean_confidence,
                        source.samples, source.successes, observed,
                        wilson_lower_bound(source.successes, source.samples), absolute_error});
        total_samples += source.samples;
        weighted_error += static_cast<long double>(source.samples) * absolute_error;
        maximum_error = std::max(maximum_error, absolute_error);
    }

    PolicyCalibrationProfile result;
    result.id_ = internal::sha256(profile_identity_json(input).dump(false));
    result.policy_id_ = std::move(input.policy_id);
    result.policy_model_digest_ = std::move(input.policy_model_digest);
    result.scope_id_ = std::move(input.scope_id);
    result.task_id_ = std::move(input.task_id);
    result.dataset_digest_ = std::move(input.dataset_digest);
    result.method_ = std::move(input.method);
    result.method_version_ = std::move(input.method_version);
    result.outcome_definition_ = std::move(input.outcome_definition);
    result.state_uncertainty_unit_ = std::move(input.state_uncertainty_unit);
    result.action_uncertainty_unit_ = std::move(input.action_uncertainty_unit);
    result.bins_ = std::move(bins);
    result.sample_count_ = total_samples;
    result.expected_calibration_error_ = static_cast<double>(weighted_error / total_samples);
    result.maximum_calibration_error_ = maximum_error;
    return result;
}

bool PolicyCalibrationProfile::valid() const {
    auto rebuilt = create(profile_input(*this));
    if (!rebuilt || rebuilt.value().id() != id_ || rebuilt.value().sample_count() != sample_count_ ||
        !approximately_equal(rebuilt.value().expected_calibration_error(), expected_calibration_error_) ||
        !approximately_equal(rebuilt.value().maximum_calibration_error(), maximum_calibration_error_) ||
        rebuilt.value().bins().size() != bins_.size()) {
        return false;
    }
    for (std::size_t index = 0; index < bins_.size(); ++index) {
        const auto& expected = rebuilt.value().bins()[index];
        const auto& actual = bins_[index];
        if (!approximately_equal(expected.observed_success_rate, actual.observed_success_rate) ||
            !approximately_equal(expected.lower_confidence_bound_95, actual.lower_confidence_bound_95) ||
            !approximately_equal(expected.absolute_calibration_error, actual.absolute_calibration_error)) {
            return false;
        }
    }
    return true;
}

Result<PolicyCalibrationLookup> PolicyCalibrationProfile::lookup(double raw_confidence) const {
    if (id_.empty() || bins_.empty() || !std::isfinite(raw_confidence) || raw_confidence < 0.0 ||
        raw_confidence > 1.0) {
        return Result<PolicyCalibrationLookup>::failure(StatusCode::InvalidArgument,
                                                        "policy calibration lookup is invalid", id_);
    }
    for (std::size_t index = 0; index < bins_.size(); ++index) {
        const auto& bin = bins_[index];
        if (raw_confidence >= bin.lower_confidence &&
            (raw_confidence < bin.upper_confidence ||
             (index + 1 == bins_.size() && raw_confidence == bin.upper_confidence))) {
            return PolicyCalibrationLookup{id_,
                                           index,
                                           raw_confidence,
                                           bin.observed_success_rate,
                                           std::min(raw_confidence, bin.lower_confidence_bound_95),
                                           bin.samples};
        }
    }
    return Result<PolicyCalibrationLookup>::failure(StatusCode::InternalError,
                                                    "calibration profile does not cover confidence", id_);
}

Result<void> PolicyCalibrationProfile::save(const std::filesystem::path& path,
                                            const SaveOptions& options) const {
    if (!valid() || path.empty() || path == path.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "calibration profile or destination is invalid");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to inspect calibration destination");
    if (destination_exists && !options.overwrite)
        return Result<void>::failure(StatusCode::IoError, "calibration destination already exists");
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return Result<void>::failure(StatusCode::IoError, "failed to create calibration parent");
    }
    const auto temporary = unique_sibling(path, ".tmp-");
    auto written = internal::write_text_file(temporary, storage_json(*this).dump(true) + "\n");
    if (!written)
        return written;
    auto published = publish_file(temporary, path, destination_exists);
    if (!published) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return published;
}

Result<PolicyCalibrationProfile> PolicyCalibrationProfile::load(const std::filesystem::path& path,
                                                                const PolicyCalibrationLoadOptions& options) {
    if (path.empty() || options.maximum_bins == 0 || options.maximum_bins > kMaximumBins ||
        options.maximum_payload_bytes == 0) {
        return Result<PolicyCalibrationProfile>::failure(StatusCode::InvalidArgument,
                                                         "calibration load options are invalid");
    }
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error)
        return Result<PolicyCalibrationProfile>::failure(StatusCode::IoError,
                                                         "failed to inspect calibration profile");
    if (bytes > options.maximum_payload_bytes) {
        return Result<PolicyCalibrationProfile>::failure(StatusCode::ResourceLimit,
                                                         "calibration profile exceeds byte limit");
    }
    auto document = internal::read_json_file(path);
    if (!document)
        return document.error();
    auto format = string_field(document.value(), "format", 128);
    auto schema = number_field(document.value(), "schema");
    auto library_version = string_field(document.value(), "library_version", 128);
    auto stored_id = string_field(document.value(), "id", 64);
    auto stored_samples = decimal_field(document.value(), "sample_count");
    auto stored_expected_error = number_field(document.value(), "expected_calibration_error");
    auto stored_maximum_error = number_field(document.value(), "maximum_calibration_error");
    if (!format || !schema || !library_version || !stored_id || !stored_samples || !stored_expected_error ||
        !stored_maximum_error) {
        return Result<PolicyCalibrationProfile>::failure(StatusCode::CorruptData,
                                                         "calibration profile metadata is incomplete");
    }
    if (format.value() != "rbfsafe-policy-calibration-profile" || schema.value() != 1.0) {
        return Result<PolicyCalibrationProfile>::failure(StatusCode::IncompatibleFormat,
                                                         "unsupported calibration profile schema");
    }

    PolicyCalibrationProfileInput input;
    auto policy_id = string_field(document.value(), "policy_id", kMaximumTextBytes);
    auto policy_model_digest = string_field(document.value(), "policy_model_digest", 64);
    auto scope_id = string_field(document.value(), "scope_id", kMaximumTextBytes);
    auto task_id = string_field(document.value(), "task_id", kMaximumTextBytes);
    auto dataset_digest = string_field(document.value(), "dataset_digest", 64);
    auto method = string_field(document.value(), "method", kMaximumTextBytes);
    auto method_version = string_field(document.value(), "method_version", kMaximumTextBytes);
    auto outcome = string_field(document.value(), "outcome_definition", kMaximumOutcomeBytes);
    auto state_unit = string_field(document.value(), "state_uncertainty_unit", kMaximumTextBytes);
    auto action_unit = string_field(document.value(), "action_uncertainty_unit", kMaximumTextBytes);
    const auto* stored_bins = document.value().find("bins");
    if (!policy_id || !policy_model_digest || !scope_id || !task_id || !dataset_digest || !method ||
        !method_version || !outcome || !state_unit || !action_unit || stored_bins == nullptr ||
        !stored_bins->is_array()) {
        return Result<PolicyCalibrationProfile>::failure(StatusCode::CorruptData,
                                                         "calibration profile fields are incomplete");
    }
    if (stored_bins->as_array().empty() || stored_bins->as_array().size() > options.maximum_bins) {
        return Result<PolicyCalibrationProfile>::failure(StatusCode::ResourceLimit,
                                                         "calibration profile exceeds bin limit");
    }
    input.policy_id = std::move(policy_id).value();
    input.policy_model_digest = std::move(policy_model_digest).value();
    input.scope_id = std::move(scope_id).value();
    input.task_id = std::move(task_id).value();
    input.dataset_digest = std::move(dataset_digest).value();
    input.method = std::move(method).value();
    input.method_version = std::move(method_version).value();
    input.outcome_definition = std::move(outcome).value();
    input.state_uncertainty_unit = std::move(state_unit).value();
    input.action_uncertainty_unit = std::move(action_unit).value();

    std::vector<std::array<double, 3>> stored_derived;
    stored_derived.reserve(stored_bins->as_array().size());
    for (const auto& item : stored_bins->as_array()) {
        auto lower = number_field(item, "lower_confidence");
        auto upper = number_field(item, "upper_confidence");
        auto mean = number_field(item, "mean_confidence");
        auto samples = decimal_field(item, "samples");
        auto successes = decimal_field(item, "successes");
        auto observed = number_field(item, "observed_success_rate");
        auto bound = number_field(item, "lower_confidence_bound_95");
        auto calibration_error = number_field(item, "absolute_calibration_error");
        if (!lower || !upper || !mean || !samples || !successes || !observed || !bound ||
            !calibration_error) {
            return Result<PolicyCalibrationProfile>::failure(StatusCode::CorruptData,
                                                             "calibration bin is incomplete");
        }
        input.bins.push_back(
            {lower.value(), upper.value(), mean.value(), samples.value(), successes.value()});
        stored_derived.push_back({observed.value(), bound.value(), calibration_error.value()});
    }
    auto result = create(std::move(input));
    if (!result)
        return Result<PolicyCalibrationProfile>::failure(StatusCode::CorruptData,
                                                         "calibration profile semantic validation failed");
    if (result.value().id() != stored_id.value() || result.value().sample_count() != stored_samples.value() ||
        !approximately_equal(result.value().expected_calibration_error(), stored_expected_error.value()) ||
        !approximately_equal(result.value().maximum_calibration_error(), stored_maximum_error.value())) {
        return Result<PolicyCalibrationProfile>::failure(StatusCode::CorruptData,
                                                         "calibration profile identity is inconsistent");
    }
    for (std::size_t index = 0; index < stored_derived.size(); ++index) {
        const auto& bin = result.value().bins()[index];
        if (!approximately_equal(bin.observed_success_rate, stored_derived[index][0]) ||
            !approximately_equal(bin.lower_confidence_bound_95, stored_derived[index][1]) ||
            !approximately_equal(bin.absolute_calibration_error, stored_derived[index][2])) {
            return Result<PolicyCalibrationProfile>::failure(
                StatusCode::CorruptData, "calibration profile statistics are inconsistent");
        }
    }
    return result;
}

Result<CalibratedPolicyBatchReport> CalibratedPolicySafetyGate::check_proposals(
    const PolicyCalibrationProfile& profile, std::string_view expected_scope_id,
    std::string_view expected_policy_model_digest, const SerialRobotModel& robot, const SceneSnapshot& scene,
    const SafeAtlas& atlas, std::span<const double> current, std::span<const PolicyProposal> proposals,
    const CalibratedPolicyGateOptions& options) {
    if (!profile.valid() || !valid_text(expected_scope_id, kMaximumTextBytes) ||
        !internal::valid_sha256(std::string(expected_policy_model_digest)) ||
        options.minimum_total_samples == 0 || options.minimum_bin_samples == 0 ||
        !std::isfinite(options.maximum_expected_calibration_error) ||
        options.maximum_expected_calibration_error < 0.0 ||
        options.maximum_expected_calibration_error > 1.0 ||
        !std::isfinite(options.maximum_bin_calibration_error) ||
        options.maximum_bin_calibration_error < 0.0 || options.maximum_bin_calibration_error > 1.0 ||
        options.policy.maximum_proposals == 0) {
        return Result<CalibratedPolicyBatchReport>::failure(StatusCode::InvalidArgument,
                                                            "calibrated policy gate input is invalid");
    }
    if (profile.scope_id() != expected_scope_id ||
        profile.policy_model_digest() != expected_policy_model_digest) {
        return Result<CalibratedPolicyBatchReport>::failure(
            StatusCode::IdentityMismatch, "calibration profile scope or model identity does not match",
            profile.id());
    }
    if (profile.sample_count() < options.minimum_total_samples ||
        profile.expected_calibration_error() > options.maximum_expected_calibration_error ||
        profile.maximum_calibration_error() > options.maximum_bin_calibration_error) {
        return Result<CalibratedPolicyBatchReport>::failure(
            StatusCode::InvalidArgument, "calibration profile does not satisfy deployment quality gates",
            profile.id());
    }
    if (proposals.empty()) {
        return Result<CalibratedPolicyBatchReport>::failure(StatusCode::InvalidArgument,
                                                            "calibrated policy proposal batch is empty");
    }
    if (proposals.size() > options.policy.maximum_proposals) {
        return Result<CalibratedPolicyBatchReport>::failure(
            StatusCode::ResourceLimit, "calibrated policy proposal batch exceeds resource limit");
    }

    std::vector<PolicyProposal> effective;
    effective.reserve(proposals.size());
    CalibratedPolicyBatchReport report;
    report.profile_id = profile.id();
    report.applications.reserve(proposals.size());
    std::set<std::string> application_ids;
    for (std::size_t index = 0; index < proposals.size(); ++index) {
        if ((index & 63U) == 0U && options.policy.shield.cancellation.cancelled()) {
            return Result<CalibratedPolicyBatchReport>::failure(StatusCode::Cancelled,
                                                                "calibrated policy gate was cancelled");
        }
        const auto& proposal = proposals[index];
        if (proposal.metadata.policy_id != profile.policy_id() ||
            proposal.metadata.task_id != profile.task_id()) {
            return Result<CalibratedPolicyBatchReport>::failure(
                StatusCode::IdentityMismatch, "proposal is outside calibration profile policy or task scope",
                profile.id());
        }
        auto lookup = profile.lookup(proposal.metadata.confidence);
        if (!lookup)
            return lookup.error();
        if (lookup.value().samples < options.minimum_bin_samples) {
            return Result<CalibratedPolicyBatchReport>::failure(
                StatusCode::InvalidArgument, "proposal calibration bin has insufficient samples",
                profile.id());
        }
        PolicyProposal adjusted = proposal;
        adjusted.metadata.confidence = lookup.value().conservative_confidence;
        CalibratedPolicyApplication application;
        application.profile_id = profile.id();
        application.raw_metadata = proposal.metadata;
        application.effective_metadata = adjusted.metadata;
        application.bin_index = lookup.value().bin_index;
        application.bin_samples = lookup.value().samples;
        application.calibrated_confidence = lookup.value().calibrated_confidence;
        application.conservative_confidence = lookup.value().conservative_confidence;
        application.id = internal::sha256(
            internal::Json(internal::Json::Object{
                               {"action_digest", shield_action_digest(proposal.action)},
                               {"bin_index", std::to_string(application.bin_index)},
                               {"bin_samples", std::to_string(application.bin_samples)},
                               {"calibrated_confidence", application.calibrated_confidence},
                               {"conservative_confidence", application.conservative_confidence},
                               {"effective_metadata", metadata_json(application.effective_metadata)},
                               {"profile_id", application.profile_id},
                               {"raw_metadata", metadata_json(application.raw_metadata)},
                           })
                .dump(false));
        if (!application_ids.insert(application.id).second) {
            return Result<CalibratedPolicyBatchReport>::failure(
                StatusCode::InvalidArgument, "duplicate calibrated policy application", application.id);
        }
        effective.push_back(std::move(adjusted));
        report.applications.push_back(std::move(application));
    }
    auto policy_report = gate_.check_proposals(robot, scene, atlas, current, effective, options.policy);
    if (!policy_report)
        return policy_report.error();
    report.policy_report = std::move(policy_report).value();
    return report;
}

} // namespace rbfsafe
