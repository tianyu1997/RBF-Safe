#include <rbfsafe/modules/applications.h>
#include <rbfsafe/modules/core.h>

#include "internal/json.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kSchema = 1;
constexpr std::size_t kMaximumStringBytes = 4'096;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json options_json(const PolicyCalibrationDriftOptions& options) {
    return internal::Json::Object{
        {"maximum_bin_success_rate_drop", options.maximum_bin_success_rate_drop},
        {"maximum_expected_calibration_error", options.maximum_expected_calibration_error},
        {"maximum_overall_success_rate_drop", options.maximum_overall_success_rate_drop},
        {"maximum_total_variation_distance", options.maximum_total_variation_distance},
        {"minimum_bin_samples", std::to_string(options.minimum_bin_samples)},
        {"minimum_total_samples", std::to_string(options.minimum_total_samples)},
    };
}

internal::Json report_json(const PolicyCalibrationDriftReport& report) {
    internal::Json::Array reasons;
    reasons.reserve(report.reasons.size());
    for (const auto reason : report.reasons)
        reasons.emplace_back(static_cast<int>(reason));
    internal::Json::Array bins;
    bins.reserve(report.bins.size());
    for (const auto& bin : report.bins) {
        bins.emplace_back(internal::Json::Object{
            {"absolute_calibration_error", bin.absolute_calibration_error},
            {"baseline_fraction", bin.baseline_fraction},
            {"baseline_success_rate", bin.baseline_success_rate},
            {"observed_fraction", bin.observed_fraction},
            {"observed_success_rate", bin.observed_success_rate},
            {"outcome_rate_available", bin.outcome_rate_available},
            {"samples", std::to_string(bin.samples)},
            {"success_rate_drop", bin.success_rate_drop},
            {"successes", std::to_string(bin.successes)},
        });
    }
    return internal::Json::Object{
        {"baseline_success_rate", report.baseline_success_rate},
        {"bins", std::move(bins)},
        {"expected_calibration_error", report.expected_calibration_error},
        {"id", report.id},
        {"maximum_bin_success_rate_drop", report.maximum_bin_success_rate_drop},
        {"maximum_calibration_error", report.maximum_calibration_error},
        {"observed_success_rate", report.observed_success_rate},
        {"options", options_json(report.options)},
        {"overall_success_rate_drop", report.overall_success_rate_drop},
        {"profile_id", report.profile_id},
        {"reasons", std::move(reasons)},
        {"sample_count", std::to_string(report.sample_count)},
        {"source_digest", report.source_digest},
        {"status", static_cast<int>(report.status)},
        {"total_variation_distance", report.total_variation_distance},
        {"window_id", report.window_id},
        {"window_sequence", std::to_string(report.window_sequence)},
    };
}

internal::Json event_json(const PolicyCalibrationLifecycleEvent& event) {
    return internal::Json::Object{
        {"current_state", static_cast<int>(event.current_state)},
        {"detail", event.detail},
        {"id", event.id},
        {"parent_id", event.parent_id},
        {"previous_state", static_cast<int>(event.previous_state)},
        {"report_id", event.report_id},
        {"sequence", std::to_string(event.sequence)},
        {"type", static_cast<int>(event.type)},
    };
}

internal::Json lifecycle_json(const PolicyCalibrationLifecycle& lifecycle) {
    internal::Json::Array reports;
    reports.reserve(lifecycle.reports().size());
    for (const auto& report : lifecycle.reports())
        reports.emplace_back(report_json(report));
    internal::Json::Array events;
    events.reserve(lifecycle.events().size());
    for (const auto& event : lifecycle.events())
        events.emplace_back(event_json(event));
    return internal::Json::Object{
        {"current_event_id", lifecycle.current_event_id()},
        {"events", std::move(events)},
        {"format", "rbfsafe-policy-calibration-lifecycle"},
        {"generation", std::to_string(lifecycle.generation())},
        {"latest_report_id", lifecycle.latest_report_id()},
        {"library_version", kVersion},
        {"profile_id", lifecycle.profile_id()},
        {"reports", std::move(reports)},
        {"schema", static_cast<double>(kSchema)},
        {"state", static_cast<int>(lifecycle.state())},
    };
}

Result<void> publish_file(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                          bool destination_exists) {
    std::error_code error;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(destination, ".backup-");
        std::filesystem::rename(destination, backup, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to stage existing calibration lifecycle");
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish calibration lifecycle");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
    }
    return Result<void>::success();
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 bool allow_empty = false) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "calibration lifecycle record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(
            StatusCode::CorruptData, "calibration lifecycle string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData,
                                              "calibration lifecycle count is invalid", std::string(key));
    }
    return result;
}

Result<double> double_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<double>::failure(StatusCode::CorruptData,
                                       "calibration lifecycle record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData, "calibration lifecycle scalar is invalid",
                                       std::string(key));
    }
    return value->as_number();
}

Result<std::size_t> enum_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    auto value = double_field(object, key);
    if (!value || value.value() < 0.0 || std::floor(value.value()) != value.value() ||
        value.value() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "calibration lifecycle enum is invalid",
                                            std::string(key));
    }
    return static_cast<std::size_t>(value.value());
}

Result<bool> bool_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<bool>::failure(StatusCode::CorruptData,
                                     "calibration lifecycle record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_bool()) {
        return Result<bool>::failure(StatusCode::CorruptData, "calibration lifecycle boolean is invalid",
                                     std::string(key));
    }
    return value->as_bool();
}

Result<PolicyCalibrationDriftOptions> decode_options(const internal::Json& object) {
    auto minimum_total = decimal_field(object, "minimum_total_samples");
    auto minimum_bin = decimal_field(object, "minimum_bin_samples");
    auto variation = double_field(object, "maximum_total_variation_distance");
    auto expected_error = double_field(object, "maximum_expected_calibration_error");
    auto overall_drop = double_field(object, "maximum_overall_success_rate_drop");
    auto bin_drop = double_field(object, "maximum_bin_success_rate_drop");
    if (!minimum_total || !minimum_bin || !variation || !expected_error || !overall_drop || !bin_drop) {
        return Result<PolicyCalibrationDriftOptions>::failure(
            StatusCode::CorruptData, "calibration lifecycle drift options are incomplete");
    }
    PolicyCalibrationDriftOptions result;
    result.minimum_total_samples = minimum_total.value();
    result.minimum_bin_samples = minimum_bin.value();
    result.maximum_total_variation_distance = variation.value();
    result.maximum_expected_calibration_error = expected_error.value();
    result.maximum_overall_success_rate_drop = overall_drop.value();
    result.maximum_bin_success_rate_drop = bin_drop.value();
    return result;
}

Result<PolicyCalibrationWindowBin> decode_bin(const internal::Json& object) {
    auto samples = decimal_field(object, "samples");
    auto successes = decimal_field(object, "successes");
    auto baseline_fraction = double_field(object, "baseline_fraction");
    auto observed_fraction = double_field(object, "observed_fraction");
    auto baseline_success = double_field(object, "baseline_success_rate");
    auto available = bool_field(object, "outcome_rate_available");
    auto observed_success = double_field(object, "observed_success_rate");
    auto success_drop = double_field(object, "success_rate_drop");
    auto calibration_error = double_field(object, "absolute_calibration_error");
    if (!samples || !successes || !baseline_fraction || !observed_fraction || !baseline_success ||
        !available || !observed_success || !success_drop || !calibration_error) {
        return Result<PolicyCalibrationWindowBin>::failure(StatusCode::CorruptData,
                                                           "calibration lifecycle drift bin is incomplete");
    }
    return PolicyCalibrationWindowBin{
        samples.value(),           successes.value(),        baseline_fraction.value(),
        observed_fraction.value(), baseline_success.value(), available.value(),
        observed_success.value(),  success_drop.value(),     calibration_error.value()};
}

Result<PolicyCalibrationDriftReport> decode_report(const internal::Json& object, std::size_t maximum_bins) {
    auto id = string_field(object, "id");
    auto profile_id = string_field(object, "profile_id");
    auto window_id = string_field(object, "window_id");
    auto window_sequence = decimal_field(object, "window_sequence");
    auto source_digest = string_field(object, "source_digest");
    auto status =
        enum_field(object, "status", static_cast<std::size_t>(PolicyCalibrationDriftStatus::DriftDetected));
    auto sample_count = decimal_field(object, "sample_count");
    auto variation = double_field(object, "total_variation_distance");
    auto baseline_success = double_field(object, "baseline_success_rate");
    auto observed_success = double_field(object, "observed_success_rate");
    auto overall_drop = double_field(object, "overall_success_rate_drop");
    auto expected_error = double_field(object, "expected_calibration_error");
    auto maximum_error = double_field(object, "maximum_calibration_error");
    auto maximum_bin_drop = double_field(object, "maximum_bin_success_rate_drop");
    const auto* stored_options = object.is_object() ? object.find("options") : nullptr;
    const auto* reasons = object.is_object() ? object.find("reasons") : nullptr;
    const auto* bins = object.is_object() ? object.find("bins") : nullptr;
    if (!id || !profile_id || !window_id || !window_sequence || !source_digest || !status || !sample_count ||
        !variation || !baseline_success || !observed_success || !overall_drop || !expected_error ||
        !maximum_error || !maximum_bin_drop || stored_options == nullptr || reasons == nullptr ||
        bins == nullptr || !reasons->is_array() || !bins->is_array() ||
        bins->as_array().size() > maximum_bins) {
        return Result<PolicyCalibrationDriftReport>::failure(
            StatusCode::CorruptData, "calibration lifecycle drift report is incomplete");
    }
    auto options = decode_options(*stored_options);
    if (!options)
        return options.error();
    PolicyCalibrationDriftReport result;
    result.id = std::move(id).value();
    result.profile_id = std::move(profile_id).value();
    result.window_id = std::move(window_id).value();
    result.window_sequence = window_sequence.value();
    result.source_digest = std::move(source_digest).value();
    result.options = options.value();
    result.status = static_cast<PolicyCalibrationDriftStatus>(status.value());
    result.sample_count = sample_count.value();
    result.total_variation_distance = variation.value();
    result.baseline_success_rate = baseline_success.value();
    result.observed_success_rate = observed_success.value();
    result.overall_success_rate_drop = overall_drop.value();
    result.expected_calibration_error = expected_error.value();
    result.maximum_calibration_error = maximum_error.value();
    result.maximum_bin_success_rate_drop = maximum_bin_drop.value();
    result.reasons.reserve(reasons->as_array().size());
    for (const auto& item : reasons->as_array()) {
        if (!item.is_number() || !std::isfinite(item.as_number()) || item.as_number() < 0.0 ||
            std::floor(item.as_number()) != item.as_number() ||
            item.as_number() >
                static_cast<double>(PolicyCalibrationDriftReason::BinSuccessRateDropExceeded)) {
            return Result<PolicyCalibrationDriftReport>::failure(
                StatusCode::CorruptData, "calibration lifecycle drift reason is invalid");
        }
        result.reasons.push_back(
            static_cast<PolicyCalibrationDriftReason>(static_cast<std::size_t>(item.as_number())));
    }
    result.bins.reserve(bins->as_array().size());
    for (const auto& item : bins->as_array()) {
        auto bin = decode_bin(item);
        if (!bin)
            return bin.error();
        result.bins.push_back(bin.value());
    }
    return result;
}

Result<PolicyCalibrationLifecycleEvent> decode_event(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto parent_id = string_field(object, "parent_id", true);
    auto sequence = decimal_field(object, "sequence");
    auto type = enum_field(object, "type",
                           static_cast<std::size_t>(PolicyCalibrationLifecycleEventType::StateTransition));
    auto previous = enum_field(object, "previous_state",
                               static_cast<std::size_t>(PolicyCalibrationLifecycleState::Retired));
    auto current = enum_field(object, "current_state",
                              static_cast<std::size_t>(PolicyCalibrationLifecycleState::Retired));
    auto report_id = string_field(object, "report_id", true);
    auto detail = string_field(object, "detail");
    if (!id || !parent_id || !sequence || !type || !previous || !current || !report_id || !detail) {
        return Result<PolicyCalibrationLifecycleEvent>::failure(StatusCode::CorruptData,
                                                                "calibration lifecycle event is incomplete");
    }
    PolicyCalibrationLifecycleEvent result;
    result.id = std::move(id).value();
    result.parent_id = std::move(parent_id).value();
    result.sequence = sequence.value();
    result.type = static_cast<PolicyCalibrationLifecycleEventType>(type.value());
    result.previous_state = static_cast<PolicyCalibrationLifecycleState>(previous.value());
    result.current_state = static_cast<PolicyCalibrationLifecycleState>(current.value());
    result.report_id = std::move(report_id).value();
    result.detail = std::move(detail).value();
    return result;
}

} // namespace

Result<void> save_policy_calibration_lifecycle(const PolicyCalibrationLifecycle& lifecycle,
                                               const PolicyCalibrationProfile& profile,
                                               const std::filesystem::path& path,
                                               const SaveOptions& options) {
    if (!lifecycle.valid(profile) || path.empty() || path == path.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "calibration lifecycle or destination is invalid");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to inspect calibration lifecycle destination");
    }
    if (destination_exists && !options.overwrite) {
        return Result<void>::failure(StatusCode::IoError, "calibration lifecycle destination already exists");
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to create calibration lifecycle parent");
        }
    }
    const auto temporary = unique_sibling(path, ".tmp-");
    auto written = internal::write_text_file(temporary, lifecycle_json(lifecycle).dump(true) + "\n");
    if (!written)
        return written;
    auto published = publish_file(temporary, path, destination_exists);
    if (!published) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return published;
}

Result<PolicyCalibrationLifecycle>
load_policy_calibration_lifecycle(const std::filesystem::path& path, const PolicyCalibrationProfile& profile,
                                  const PolicyCalibrationLifecycleLoadOptions& options) {
    if (path.empty() || !profile.valid() || options.maximum_reports == 0 || options.maximum_events == 0 ||
        options.maximum_total_bins == 0 || options.maximum_payload_bytes == 0) {
        return Result<PolicyCalibrationLifecycle>::failure(StatusCode::InvalidArgument,
                                                           "calibration lifecycle load input is invalid");
    }
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<PolicyCalibrationLifecycle>::failure(StatusCode::IoError,
                                                           "failed to inspect calibration lifecycle file");
    }
    if (bytes > options.maximum_payload_bytes) {
        return Result<PolicyCalibrationLifecycle>::failure(StatusCode::ResourceLimit,
                                                           "calibration lifecycle exceeds byte limit");
    }
    auto document = internal::read_json_file(path);
    if (!document)
        return document.error();
    auto format = string_field(document.value(), "format");
    auto schema = double_field(document.value(), "schema");
    auto library_version = string_field(document.value(), "library_version");
    auto profile_id = string_field(document.value(), "profile_id");
    auto state = enum_field(document.value(), "state",
                            static_cast<std::size_t>(PolicyCalibrationLifecycleState::Retired));
    auto generation = decimal_field(document.value(), "generation");
    auto current_event_id = string_field(document.value(), "current_event_id");
    auto latest_report_id = string_field(document.value(), "latest_report_id", true);
    const auto* reports = document.value().is_object() ? document.value().find("reports") : nullptr;
    const auto* events = document.value().is_object() ? document.value().find("events") : nullptr;
    if (!format || !schema || !library_version || !profile_id || !state || !generation || !current_event_id ||
        !latest_report_id || reports == nullptr || events == nullptr || !reports->is_array() ||
        !events->is_array()) {
        return Result<PolicyCalibrationLifecycle>::failure(StatusCode::CorruptData,
                                                           "calibration lifecycle metadata is incomplete");
    }
    if (format.value() != "rbfsafe-policy-calibration-lifecycle" || schema.value() != 1.0) {
        return Result<PolicyCalibrationLifecycle>::failure(StatusCode::IncompatibleFormat,
                                                           "unsupported calibration lifecycle schema");
    }
    if (reports->as_array().size() > options.maximum_reports || events->as_array().empty() ||
        events->as_array().size() > options.maximum_events) {
        return Result<PolicyCalibrationLifecycle>::failure(
            StatusCode::ResourceLimit, "calibration lifecycle record count exceeds limit");
    }

    PolicyCalibrationLifecycle result;
    result.profile_id_ = std::move(profile_id).value();
    result.state_ = static_cast<PolicyCalibrationLifecycleState>(state.value());
    result.generation_ = generation.value();
    result.current_event_id_ = std::move(current_event_id).value();
    result.latest_report_id_ = std::move(latest_report_id).value();
    result.reports_.reserve(reports->as_array().size());
    std::size_t total_bins = 0;
    for (const auto& item : reports->as_array()) {
        const auto* bins = item.is_object() ? item.find("bins") : nullptr;
        if (bins == nullptr || !bins->is_array() ||
            bins->as_array().size() > options.maximum_total_bins - total_bins) {
            return Result<PolicyCalibrationLifecycle>::failure(
                StatusCode::ResourceLimit, "calibration lifecycle bin count exceeds limit");
        }
        total_bins += bins->as_array().size();
        auto report = decode_report(item, profile.bins().size());
        if (!report)
            return report.error();
        result.reports_.push_back(std::move(report).value());
    }
    result.events_.reserve(events->as_array().size());
    for (const auto& item : events->as_array()) {
        auto event = decode_event(item);
        if (!event)
            return event.error();
        result.events_.push_back(std::move(event).value());
    }
    if (!result.valid(profile)) {
        return Result<PolicyCalibrationLifecycle>::failure(
            StatusCode::CorruptData, "calibration lifecycle semantic validation failed");
    }
    return result;
}

} // namespace rbfsafe
