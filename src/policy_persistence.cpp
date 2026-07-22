#include <rbfsafe/policy.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/policy.h"
#include "internal/sha256.h"

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
constexpr std::size_t kMaximumStringBytes = 4096;
constexpr std::size_t kMaximumDimension = 128;
constexpr std::size_t kMaximumExactJsonInteger = sizeof(std::size_t) < sizeof(std::uint64_t)
                                                     ? std::numeric_limits<std::size_t>::max()
                                                     : static_cast<std::size_t>(9'007'199'254'740'991ULL);

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json configuration_json(std::span<const double> configuration) {
    internal::Json::Array result;
    result.reserve(configuration.size());
    for (const double value : configuration)
        result.emplace_back(value);
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

internal::Json record_json(const PolicyFeedbackRecord& record) {
    return internal::Json::Object{
        {"action_type", static_cast<int>(record.action_type)},
        {"evidence", static_cast<int>(record.evidence)},
        {"id", record.id},
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
    };
}

internal::Json payload_json(const PolicyFeedbackDatabase& database) {
    internal::Json::Array records;
    records.reserve(database.records().size());
    for (const auto& record : database.records())
        records.emplace_back(record_json(record));
    return internal::Json::Object{
        {"format", "rbfsafe-policy-feedback-records"},
        {"records", std::move(records)},
        {"schema", static_cast<double>(kSchema)},
    };
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 bool allow_empty = false) {
    if (!object.is_object())
        return Result<std::string>::failure(StatusCode::CorruptData, "expected JSON object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData, "missing or invalid string field",
                                            std::string(key));
    }
    return value->as_string();
}

Result<double> number_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object())
        return Result<double>::failure(StatusCode::CorruptData, "expected JSON object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData, "missing or invalid numeric field",
                                       std::string(key));
    }
    return value->as_number();
}

Result<std::size_t> size_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    auto value = number_field(object, key);
    if (!value)
        return value.error();
    if (value.value() < 0.0 || std::floor(value.value()) != value.value() ||
        value.value() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "numeric field exceeds limit",
                                            std::string(key));
    }
    return static_cast<std::size_t>(value.value());
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key, true);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), result);
    if (text.value().empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "invalid decimal field",
                                              std::string(key));
    }
    return result;
}

Result<Configuration> configuration_field(const internal::Json& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_array() || value->as_array().size() > kMaximumDimension) {
        return Result<Configuration>::failure(StatusCode::CorruptData,
                                              "configuration field has invalid length", std::string(key));
    }
    Configuration result;
    result.reserve(value->as_array().size());
    for (const auto& coordinate : value->as_array()) {
        if (!coordinate.is_number() || !std::isfinite(coordinate.as_number())) {
            return Result<Configuration>::failure(
                StatusCode::CorruptData, "configuration field contains invalid value", std::string(key));
        }
        result.push_back(coordinate.as_number());
    }
    return result;
}

Result<PolicyProposalMetadata> metadata_field(const internal::Json& object) {
    if (!object.is_object())
        return Result<PolicyProposalMetadata>::failure(StatusCode::CorruptData,
                                                       "policy metadata is not an object");
    auto policy_id = string_field(object, "policy_id");
    auto task_id = string_field(object, "task_id");
    auto episode_id = string_field(object, "episode_id", true);
    auto sequence = decimal_field(object, "sequence");
    auto confidence = number_field(object, "confidence");
    auto state_uncertainty = number_field(object, "state_uncertainty");
    auto action_uncertainty = number_field(object, "action_uncertainty");
    auto observation_age = number_field(object, "observation_age_seconds");
    auto inference_latency = number_field(object, "inference_latency_seconds");
    if (!policy_id || !task_id || !episode_id || !sequence || !confidence || !state_uncertainty ||
        !action_uncertainty || !observation_age || !inference_latency) {
        return Result<PolicyProposalMetadata>::failure(StatusCode::CorruptData,
                                                       "policy metadata is incomplete");
    }
    PolicyProposalMetadata result;
    result.policy_id = std::move(policy_id).value();
    result.task_id = std::move(task_id).value();
    result.episode_id = std::move(episode_id).value();
    result.sequence = sequence.value();
    result.confidence = confidence.value();
    result.state_uncertainty = state_uncertainty.value();
    result.action_uncertainty = action_uncertainty.value();
    result.observation_age_seconds = observation_age.value();
    result.inference_latency_seconds = inference_latency.value();
    return result;
}

Result<PolicyFeedbackRecord> decode_record(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto proposal_id = string_field(object, "proposal_id");
    auto policy_decision_id = string_field(object, "policy_decision_id");
    auto shield_decision_id = string_field(object, "shield_decision_id", true);
    auto robot_digest = string_field(object, "robot_digest");
    auto scene_digest = string_field(object, "scene_digest");
    auto action_type =
        size_field(object, "action_type", static_cast<std::size_t>(ShieldActionType::Trajectory));
    auto label = size_field(object, "label", static_cast<std::size_t>(PolicyFeedbackLabel::ShieldRejected));
    auto reason = size_field(object, "reason", static_cast<std::size_t>(PolicyGateReason::ShieldRejected));
    auto evidence =
        size_field(object, "evidence", static_cast<std::size_t>(EvidenceLevel::RuntimeExecutable));
    auto requested_target = configuration_field(object, "requested_target");
    auto output_target = configuration_field(object, "output_target");
    auto repair_distance = number_field(object, "repair_distance");
    const auto* metadata_json_value = object.find("metadata");
    if (!id || !proposal_id || !policy_decision_id || !shield_decision_id || !robot_digest || !scene_digest ||
        !action_type || !label || !reason || !evidence || !requested_target || !output_target ||
        !repair_distance || metadata_json_value == nullptr) {
        return Result<PolicyFeedbackRecord>::failure(StatusCode::CorruptData,
                                                     "policy feedback record is incomplete");
    }
    auto metadata = metadata_field(*metadata_json_value);
    if (!metadata)
        return metadata.error();
    PolicyFeedbackRecord result;
    result.id = std::move(id).value();
    result.proposal_id = std::move(proposal_id).value();
    result.policy_decision_id = std::move(policy_decision_id).value();
    result.shield_decision_id = std::move(shield_decision_id).value();
    result.robot_digest = std::move(robot_digest).value();
    result.scene_digest = std::move(scene_digest).value();
    result.metadata = std::move(metadata).value();
    result.label = static_cast<PolicyFeedbackLabel>(label.value());
    result.reason = static_cast<PolicyGateReason>(reason.value());
    result.action_type = static_cast<ShieldActionType>(action_type.value());
    result.requested_target = std::move(requested_target).value();
    result.output_target = std::move(output_target).value();
    result.repair_distance = repair_distance.value();
    result.evidence = static_cast<EvidenceLevel>(evidence.value());
    return result;
}

Result<void> publish_directory(const std::filesystem::path& temporary,
                               const std::filesystem::path& destination, bool destination_exists) {
    std::error_code error;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = unique_sibling(destination, ".backup-");
        std::filesystem::rename(destination, backup, error);
        if (error)
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to stage existing policy feedback database");
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish policy feedback database");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove_all(backup, ignored);
    }
    return Result<void>::success();
}

} // namespace

Result<void> PolicyFeedbackDatabase::save(const std::filesystem::path& directory,
                                          const SaveOptions& options) const {
    if (!valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "cannot save invalid policy feedback database");
    }
    if (directory.empty() || directory == directory.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "policy feedback destination must be a specific directory");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(directory, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to inspect policy feedback destination");
    if (destination_exists && !options.overwrite) {
        return Result<void>::failure(StatusCode::IoError, "policy feedback destination already exists");
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error)
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to create policy feedback parent directory");
    }
    const auto temporary = unique_sibling(directory, ".tmp-");
    std::filesystem::create_directories(temporary, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to create policy feedback temporary directory");
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    const std::string payload = payload_json(*this).dump(true) + "\n";
    auto written = internal::write_text_file(temporary / "records.json", payload);
    if (!written) {
        cleanup();
        return written;
    }
    internal::Json manifest(internal::Json::Object{
        {"format", "rbfsafe-policy-feedback"},
        {"library_version", kVersion},
        {"payload_sha256", internal::sha256(payload)},
        {"records", static_cast<double>(records_.size())},
        {"schema", static_cast<double>(kSchema)},
    });
    written = internal::write_text_file(temporary / "manifest.json", manifest.dump(true) + "\n");
    if (!written) {
        cleanup();
        return written;
    }
    auto published = publish_directory(temporary, directory, destination_exists);
    if (!published)
        cleanup();
    return published;
}

Result<PolicyFeedbackDatabase> PolicyFeedbackDatabase::load(const std::filesystem::path& directory,
                                                            const PolicyFeedbackLoadOptions& options) {
    if (options.maximum_records == 0 || options.maximum_payload_bytes == 0) {
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::InvalidArgument,
                                                       "policy feedback load limits must be positive");
    }
    auto manifest = internal::read_json_file(directory / "manifest.json");
    if (!manifest)
        return manifest.error();
    auto format = string_field(manifest.value(), "format");
    auto schema = size_field(manifest.value(), "schema", 1000);
    auto records_count = size_field(manifest.value(), "records", kMaximumExactJsonInteger);
    auto checksum = string_field(manifest.value(), "payload_sha256");
    if (!format || !schema || !records_count || !checksum) {
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::CorruptData,
                                                       "policy feedback manifest is incomplete");
    }
    if (format.value() != "rbfsafe-policy-feedback" || schema.value() != kSchema) {
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::IncompatibleFormat,
                                                       "unsupported policy feedback schema");
    }
    if (!internal::valid_sha256(checksum.value())) {
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::CorruptData,
                                                       "policy feedback checksum is invalid");
    }
    if (records_count.value() > options.maximum_records) {
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::ResourceLimit,
                                                       "policy feedback record count exceeds limit");
    }
    std::error_code error;
    const auto payload_size = std::filesystem::file_size(directory / "records.json", error);
    if (error)
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::IoError,
                                                       "failed to inspect policy feedback payload");
    if (payload_size > options.maximum_payload_bytes) {
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::ResourceLimit,
                                                       "policy feedback payload exceeds size limit");
    }
    auto actual_checksum = internal::sha256_file(directory / "records.json");
    if (!actual_checksum)
        return actual_checksum.error();
    if (actual_checksum.value() != checksum.value()) {
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::CorruptData,
                                                       "policy feedback payload checksum mismatch");
    }
    auto payload = internal::read_json_file(directory / "records.json");
    if (!payload)
        return payload.error();
    auto payload_format = string_field(payload.value(), "format");
    auto payload_schema = size_field(payload.value(), "schema", 1000);
    const auto* records_json = payload.value().find("records");
    if (!payload_format || !payload_schema || payload_format.value() != "rbfsafe-policy-feedback-records" ||
        payload_schema.value() != kSchema || records_json == nullptr || !records_json->is_array() ||
        records_json->as_array().size() != records_count.value()) {
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::CorruptData,
                                                       "policy feedback payload metadata is inconsistent");
    }
    std::vector<PolicyFeedbackRecord> records;
    records.reserve(records_count.value());
    for (const auto& item : records_json->as_array()) {
        auto record = decode_record(item);
        if (!record)
            return record.error();
        records.push_back(std::move(record).value());
    }
    auto database = PolicyFeedbackDatabase::create(std::move(records));
    if (!database) {
        return Result<PolicyFeedbackDatabase>::failure(StatusCode::CorruptData, database.error().message,
                                                       database.error().context);
    }
    return database;
}

} // namespace rbfsafe
