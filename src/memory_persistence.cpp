#include <rbfsafe/memory.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/memory.h"
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
constexpr std::size_t kMaximumTags = 64;
constexpr std::size_t kMaximumExactJsonInteger = sizeof(std::size_t) < sizeof(std::uint64_t)
                                                     ? std::numeric_limits<std::size_t>::max()
                                                     : static_cast<std::size_t>(9'007'199'254'740'991ULL);

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json strings_json(const std::vector<std::string>& values) {
    internal::Json::Array result;
    result.reserve(values.size());
    for (const auto& value : values)
        result.emplace_back(value);
    return result;
}

internal::Json artifact_json(const MemoryArtifact& artifact) {
    return internal::Json::Object{
        {"content_digest", artifact.content_digest},
        {"deployment_id", artifact.deployment_id},
        {"evidence", static_cast<int>(artifact.evidence)},
        {"generation", std::to_string(artifact.generation)},
        {"id", artifact.id},
        {"locator", artifact.locator},
        {"registered_sequence", std::to_string(artifact.registered_sequence)},
        {"robot_digest", artifact.robot_digest},
        {"scene_digest", artifact.scene_digest},
        {"state", static_cast<int>(artifact.state)},
        {"tags", strings_json(artifact.tags)},
        {"task_id", artifact.task_id},
        {"type", static_cast<int>(artifact.type)},
    };
}

internal::Json event_json(const MemoryEvent& event) {
    return internal::Json::Object{
        {"artifact_id", event.artifact_id},
        {"current_state", static_cast<int>(event.current_state)},
        {"detail", event.detail},
        {"id", event.id},
        {"previous_state", static_cast<int>(event.previous_state)},
        {"sequence", std::to_string(event.sequence)},
        {"task_id", event.task_id},
        {"type", static_cast<int>(event.type)},
    };
}

internal::Json payload_json(const SafetyMemory& memory) {
    internal::Json::Array artifacts;
    artifacts.reserve(memory.artifacts().size());
    for (const auto& artifact : memory.artifacts())
        artifacts.emplace_back(artifact_json(artifact));
    internal::Json::Array events;
    events.reserve(memory.events().size());
    for (const auto& event : memory.events())
        events.emplace_back(event_json(event));
    return internal::Json::Object{
        {"artifacts", std::move(artifacts)},
        {"events", std::move(events)},
        {"format", "rbfsafe-safety-memory-records"},
        {"next_sequence", std::to_string(memory.next_sequence())},
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

Result<std::size_t> size_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object())
        return Result<std::size_t>::failure(StatusCode::CorruptData, "expected JSON object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || std::floor(value->as_number()) != value->as_number() ||
        value->as_number() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "invalid numeric field",
                                            std::string(key));
    }
    return static_cast<std::size_t>(value->as_number());
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto value = string_field(object, key, true);
    if (!value)
        return value.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(value.value().data(), value.value().data() + value.value().size(), result);
    if (value.value().empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.value().data() + value.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData, "invalid decimal field",
                                              std::string(key));
    }
    return result;
}

Result<std::vector<std::string>> tags_field(const internal::Json& object) {
    const auto* value = object.find("tags");
    if (value == nullptr || !value->is_array() || value->as_array().size() > kMaximumTags) {
        return Result<std::vector<std::string>>::failure(StatusCode::CorruptData, "memory tags are invalid");
    }
    std::vector<std::string> result;
    result.reserve(value->as_array().size());
    for (const auto& tag : value->as_array()) {
        if (!tag.is_string() || tag.as_string().empty() || tag.as_string().size() > kMaximumStringBytes) {
            return Result<std::vector<std::string>>::failure(StatusCode::CorruptData,
                                                             "memory tag is invalid");
        }
        result.push_back(tag.as_string());
    }
    return result;
}

Result<MemoryArtifact> decode_artifact(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto type = size_field(object, "type", static_cast<std::size_t>(MemoryArtifactType::FleetSchedule));
    auto state = size_field(object, "state", static_cast<std::size_t>(MemoryArtifactState::Retired));
    auto deployment_id = string_field(object, "deployment_id");
    auto robot_digest = string_field(object, "robot_digest");
    auto scene_digest = string_field(object, "scene_digest");
    auto task_id = string_field(object, "task_id");
    auto content_digest = string_field(object, "content_digest");
    auto locator = string_field(object, "locator");
    auto evidence =
        size_field(object, "evidence", static_cast<std::size_t>(EvidenceLevel::RuntimeExecutable));
    auto tags = tags_field(object);
    auto generation = decimal_field(object, "generation");
    auto registered_sequence = decimal_field(object, "registered_sequence");
    if (!id || !type || !state || !deployment_id || !robot_digest || !scene_digest || !task_id ||
        !content_digest || !locator || !evidence || !tags || !generation || !registered_sequence) {
        return Result<MemoryArtifact>::failure(StatusCode::CorruptData,
                                               "memory artifact record is incomplete");
    }
    MemoryArtifact result;
    result.id = std::move(id).value();
    result.type = static_cast<MemoryArtifactType>(type.value());
    result.state = static_cast<MemoryArtifactState>(state.value());
    result.deployment_id = std::move(deployment_id).value();
    result.robot_digest = std::move(robot_digest).value();
    result.scene_digest = std::move(scene_digest).value();
    result.task_id = std::move(task_id).value();
    result.content_digest = std::move(content_digest).value();
    result.locator = std::move(locator).value();
    result.evidence = static_cast<EvidenceLevel>(evidence.value());
    result.tags = std::move(tags).value();
    result.generation = generation.value();
    result.registered_sequence = registered_sequence.value();
    auto status = internal::validate_memory_artifact(result);
    if (!status) {
        return Result<MemoryArtifact>::failure(StatusCode::CorruptData, status.error().message,
                                               status.error().context);
    }
    return result;
}

Result<MemoryEvent> decode_event(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto sequence = decimal_field(object, "sequence");
    auto type = size_field(object, "type", static_cast<std::size_t>(MemoryEventType::SceneInvalidated));
    auto artifact_id = string_field(object, "artifact_id");
    auto previous_state =
        size_field(object, "previous_state", static_cast<std::size_t>(MemoryArtifactState::Retired));
    auto current_state =
        size_field(object, "current_state", static_cast<std::size_t>(MemoryArtifactState::Retired));
    auto task_id = string_field(object, "task_id");
    auto detail = string_field(object, "detail");
    if (!id || !sequence || !type || !artifact_id || !previous_state || !current_state || !task_id ||
        !detail) {
        return Result<MemoryEvent>::failure(StatusCode::CorruptData, "memory event record is incomplete");
    }
    MemoryEvent result;
    result.id = std::move(id).value();
    result.sequence = sequence.value();
    result.type = static_cast<MemoryEventType>(type.value());
    result.artifact_id = std::move(artifact_id).value();
    result.previous_state = static_cast<MemoryArtifactState>(previous_state.value());
    result.current_state = static_cast<MemoryArtifactState>(current_state.value());
    result.task_id = std::move(task_id).value();
    result.detail = std::move(detail).value();
    auto status = internal::validate_memory_event(result);
    if (!status) {
        return Result<MemoryEvent>::failure(StatusCode::CorruptData, status.error().message,
                                            status.error().context);
    }
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
            return Result<void>::failure(StatusCode::IoError, "failed to stage existing safety memory");
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish safety memory");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove_all(backup, ignored);
    }
    return Result<void>::success();
}

} // namespace

Result<void> save_safety_memory_directory(const SafetyMemory& memory, const std::filesystem::path& directory,
                                          const SaveOptions& options) {
    if (!memory.valid())
        return Result<void>::failure(StatusCode::InvalidArgument, "cannot save invalid safety memory");
    if (directory.empty() || directory == directory.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "safety memory destination must be a specific directory");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(directory, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to inspect safety memory destination");
    if (destination_exists && !options.overwrite)
        return Result<void>::failure(StatusCode::IoError, "safety memory destination already exists");
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error)
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to create safety memory parent directory");
    }
    const auto temporary = unique_sibling(directory, ".tmp-");
    std::filesystem::create_directories(temporary, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to create safety memory temporary directory");
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    const std::string payload = payload_json(memory).dump(true) + "\n";
    auto written = internal::write_text_file(temporary / "memory.json", payload);
    if (!written) {
        cleanup();
        return written;
    }
    internal::Json manifest(internal::Json::Object{
        {"artifacts", static_cast<double>(memory.artifacts().size())},
        {"events", static_cast<double>(memory.events().size())},
        {"format", "rbfsafe-safety-memory"},
        {"library_version", kVersion},
        {"payload_sha256", internal::sha256(payload)},
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

Result<SafetyMemory> load_safety_memory_directory(const std::filesystem::path& directory,
                                                  const SafetyMemoryLoadOptions& options) {
    if (options.maximum_artifacts == 0 || options.maximum_events == 0 || options.maximum_payload_bytes == 0) {
        return Result<SafetyMemory>::failure(StatusCode::InvalidArgument,
                                             "safety memory load limits must be positive");
    }
    auto manifest = internal::read_json_file(directory / "manifest.json");
    if (!manifest)
        return manifest.error();
    auto format = string_field(manifest.value(), "format");
    auto schema = size_field(manifest.value(), "schema", 1000);
    auto artifacts_count = size_field(manifest.value(), "artifacts", kMaximumExactJsonInteger);
    auto events_count = size_field(manifest.value(), "events", kMaximumExactJsonInteger);
    auto checksum = string_field(manifest.value(), "payload_sha256");
    if (!format || !schema || !artifacts_count || !events_count || !checksum) {
        return Result<SafetyMemory>::failure(StatusCode::CorruptData, "safety memory manifest is incomplete");
    }
    if (format.value() != "rbfsafe-safety-memory" || schema.value() != kSchema) {
        return Result<SafetyMemory>::failure(StatusCode::IncompatibleFormat,
                                             "unsupported safety memory schema");
    }
    if (!internal::valid_sha256(checksum.value())) {
        return Result<SafetyMemory>::failure(StatusCode::CorruptData, "safety memory checksum is invalid");
    }
    if (artifacts_count.value() > options.maximum_artifacts ||
        events_count.value() > options.maximum_events) {
        return Result<SafetyMemory>::failure(StatusCode::ResourceLimit,
                                             "safety memory record count exceeds limit");
    }
    std::error_code error;
    const auto payload_size = std::filesystem::file_size(directory / "memory.json", error);
    if (error)
        return Result<SafetyMemory>::failure(StatusCode::IoError, "failed to inspect safety memory payload");
    if (payload_size > options.maximum_payload_bytes) {
        return Result<SafetyMemory>::failure(StatusCode::ResourceLimit,
                                             "safety memory payload exceeds size limit");
    }
    auto actual_checksum = internal::sha256_file(directory / "memory.json");
    if (!actual_checksum)
        return actual_checksum.error();
    if (actual_checksum.value() != checksum.value()) {
        return Result<SafetyMemory>::failure(StatusCode::CorruptData,
                                             "safety memory payload checksum mismatch");
    }
    auto payload = internal::read_json_file(directory / "memory.json");
    if (!payload)
        return payload.error();
    auto payload_format = string_field(payload.value(), "format");
    auto payload_schema = size_field(payload.value(), "schema", 1000);
    auto next_sequence = decimal_field(payload.value(), "next_sequence");
    const auto* artifacts_json = payload.value().find("artifacts");
    const auto* events_json = payload.value().find("events");
    if (!payload_format || !payload_schema || !next_sequence ||
        payload_format.value() != "rbfsafe-safety-memory-records" || payload_schema.value() != kSchema ||
        artifacts_json == nullptr || !artifacts_json->is_array() || events_json == nullptr ||
        !events_json->is_array() || artifacts_json->as_array().size() != artifacts_count.value() ||
        events_json->as_array().size() != events_count.value()) {
        return Result<SafetyMemory>::failure(StatusCode::CorruptData,
                                             "safety memory payload metadata is inconsistent");
    }
    SafetyMemory result;
    result.artifacts_.reserve(artifacts_count.value());
    for (const auto& item : artifacts_json->as_array()) {
        auto artifact = decode_artifact(item);
        if (!artifact)
            return artifact.error();
        result.artifacts_.push_back(std::move(artifact).value());
    }
    result.events_.reserve(events_count.value());
    for (const auto& item : events_json->as_array()) {
        auto event = decode_event(item);
        if (!event)
            return event.error();
        result.events_.push_back(std::move(event).value());
    }
    result.next_sequence_ = next_sequence.value();
    if (!result.valid()) {
        return Result<SafetyMemory>::failure(StatusCode::CorruptData,
                                             "safety memory history is inconsistent");
    }
    return result;
}

} // namespace rbfsafe
