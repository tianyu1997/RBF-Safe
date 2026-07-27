#include <rbfsafe/remote.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/remote.h"
#include "internal/sha256.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kSchema = 1;
constexpr std::size_t kMaximumStringBytes = 4096;
constexpr std::size_t kMaximumExactJsonInteger = sizeof(std::size_t) < sizeof(std::uint64_t)
                                                     ? std::numeric_limits<std::size_t>::max()
                                                     : static_cast<std::size_t>(9'007'199'254'740'991ULL);

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json transfer_json(const VerifiedArtifactTransfer& transfer) {
    return internal::Json::Object{
        {"artifact_content_digest", transfer.artifact_content_digest},
        {"artifact_generation", std::to_string(transfer.artifact_generation)},
        {"artifact_id", transfer.artifact_id},
        {"artifact_state", static_cast<int>(transfer.artifact_state)},
        {"attestation_id", transfer.attestation_id},
        {"authentication", static_cast<int>(transfer.authentication)},
        {"id", transfer.id},
        {"media_type", transfer.media_type},
        {"memory_id", transfer.memory_id},
        {"operation", static_cast<int>(transfer.operation)},
        {"payload_bytes", std::to_string(transfer.payload_bytes)},
        {"payload_digest", transfer.payload_digest},
        {"request_id", transfer.request_id},
        {"response_id", transfer.response_id},
        {"service_id", transfer.service_id},
        {"service_sequence", std::to_string(transfer.service_sequence)},
    };
}

internal::Json record_json(const ArtifactTransferRecord& record) {
    return internal::Json::Object{
        {"id", record.id},
        {"parent_id", record.parent_id},
        {"sequence", std::to_string(record.sequence)},
        {"transfer", transfer_json(record.transfer)},
    };
}

internal::Json records_json(const ArtifactTransferJournal& journal) {
    internal::Json::Array records;
    records.reserve(journal.records().size());
    for (const auto& record : journal.records())
        records.emplace_back(record_json(record));
    return internal::Json::Object{
        {"format", "rbfsafe-artifact-transfer-records"},
        {"records", std::move(records)},
        {"schema", static_cast<double>(kSchema)},
    };
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 bool allow_empty = false) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData, "expected JSON object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData, "missing or invalid string field",
                                            std::string(key));
    }
    return value->as_string();
}

Result<std::size_t> size_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object()) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "expected JSON object");
    }
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

Result<VerifiedArtifactTransfer> decode_transfer(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto operation =
        size_field(object, "operation", static_cast<std::size_t>(ArtifactTransferOperation::Publish));
    auto request_id = string_field(object, "request_id");
    auto response_id = string_field(object, "response_id");
    auto service_id = string_field(object, "service_id");
    auto memory_id = string_field(object, "memory_id");
    auto artifact_id = string_field(object, "artifact_id");
    auto artifact_generation = decimal_field(object, "artifact_generation");
    auto artifact_state =
        size_field(object, "artifact_state", static_cast<std::size_t>(MemoryArtifactState::Retired));
    auto content_digest = string_field(object, "artifact_content_digest");
    auto payload_digest = string_field(object, "payload_digest");
    auto payload_bytes = decimal_field(object, "payload_bytes");
    auto media_type = string_field(object, "media_type");
    auto service_sequence = decimal_field(object, "service_sequence");
    auto authentication = size_field(object, "authentication",
                                     static_cast<std::size_t>(ArtifactTransferAuthentication::HmacSha256));
    auto attestation_id = string_field(object, "attestation_id", true);
    if (!id || !operation || !request_id || !response_id || !service_id || !memory_id || !artifact_id ||
        !artifact_generation || !artifact_state || !content_digest || !payload_digest || !payload_bytes ||
        !media_type || !service_sequence || !authentication || !attestation_id) {
        return Result<VerifiedArtifactTransfer>::failure(StatusCode::CorruptData,
                                                         "artifact transfer record is incomplete");
    }
    VerifiedArtifactTransfer result;
    result.id = std::move(id).value();
    result.operation = static_cast<ArtifactTransferOperation>(operation.value());
    result.request_id = std::move(request_id).value();
    result.response_id = std::move(response_id).value();
    result.service_id = std::move(service_id).value();
    result.memory_id = std::move(memory_id).value();
    result.artifact_id = std::move(artifact_id).value();
    result.artifact_generation = artifact_generation.value();
    result.artifact_state = static_cast<MemoryArtifactState>(artifact_state.value());
    result.artifact_content_digest = std::move(content_digest).value();
    result.payload_digest = std::move(payload_digest).value();
    result.payload_bytes = payload_bytes.value();
    result.media_type = std::move(media_type).value();
    result.service_sequence = service_sequence.value();
    result.authentication = static_cast<ArtifactTransferAuthentication>(authentication.value());
    result.attestation_id = std::move(attestation_id).value();
    if (!valid_verified_artifact_transfer(result)) {
        return Result<VerifiedArtifactTransfer>::failure(StatusCode::CorruptData,
                                                         "artifact transfer identity is invalid", result.id);
    }
    return result;
}

Result<ArtifactTransferRecord> decode_record(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto sequence = decimal_field(object, "sequence");
    auto parent_id = string_field(object, "parent_id", true);
    const auto* transfer_json_value = object.is_object() ? object.find("transfer") : nullptr;
    if (!id || !sequence || !parent_id || transfer_json_value == nullptr) {
        return Result<ArtifactTransferRecord>::failure(StatusCode::CorruptData,
                                                       "artifact transfer journal record is incomplete");
    }
    auto transfer = decode_transfer(*transfer_json_value);
    if (!transfer)
        return transfer.error();
    ArtifactTransferRecord result;
    result.sequence = sequence.value();
    result.id = std::move(id).value();
    result.parent_id = std::move(parent_id).value();
    result.transfer = std::move(transfer).value();
    if (!internal::valid_sha256(result.id) ||
        internal::artifact_transfer_record_identity(result) != result.id) {
        return Result<ArtifactTransferRecord>::failure(
            StatusCode::CorruptData, "artifact transfer journal record identity is invalid", result.id);
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
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to stage existing artifact transfer journal");
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish artifact transfer journal");
    }
    if (destination_exists) {
        std::error_code ignored;
        std::filesystem::remove_all(backup, ignored);
    }
    return Result<void>::success();
}

} // namespace

Result<void> save_artifact_transfer_journal(const ArtifactTransferJournal& journal,
                                            const std::filesystem::path& directory,
                                            const SaveOptions& options) {
    if (!journal.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "cannot save an invalid artifact transfer journal");
    }
    if (directory.empty() || directory == directory.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "artifact transfer journal destination must be a specific directory");
    }
    if (journal.records().size() > kMaximumExactJsonInteger) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "artifact transfer journal record count exceeds storage format");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(directory, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to inspect artifact transfer journal destination");
    }
    if (destination_exists && !options.overwrite) {
        return Result<void>::failure(StatusCode::IoError,
                                     "artifact transfer journal destination already exists");
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to create artifact transfer journal parent directory");
        }
    }
    const auto temporary = unique_sibling(directory, ".tmp-");
    std::filesystem::create_directories(temporary, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to create artifact transfer journal temporary directory");
    }
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    const std::string payload = records_json(journal).dump(true) + "\n";
    auto written = internal::write_text_file(temporary / "records.json", payload);
    if (!written) {
        cleanup();
        return written;
    }
    internal::Json manifest(internal::Json::Object{
        {"current_record_id", journal.current_record_id()},
        {"format", "rbfsafe-artifact-transfer-journal"},
        {"journal_id", journal.identity()},
        {"library_version", kVersion},
        {"payload_sha256", internal::sha256(payload)},
        {"records", static_cast<double>(journal.records().size())},
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

Result<ArtifactTransferJournal>
load_artifact_transfer_journal(const std::filesystem::path& directory,
                               const ArtifactTransferJournalLoadOptions& options) {
    if (directory.empty() || options.maximum_records == 0 || options.maximum_payload_bytes == 0) {
        return Result<ArtifactTransferJournal>::failure(StatusCode::InvalidArgument,
                                                        "artifact transfer journal load options are invalid");
    }
    auto manifest = internal::read_json_file(directory / "manifest.json");
    if (!manifest)
        return manifest.error();
    auto format = string_field(manifest.value(), "format");
    auto schema = size_field(manifest.value(), "schema", 1000);
    auto library_version = string_field(manifest.value(), "library_version");
    auto journal_id = string_field(manifest.value(), "journal_id");
    auto current_record_id = string_field(manifest.value(), "current_record_id", true);
    auto records_count = size_field(manifest.value(), "records", kMaximumExactJsonInteger);
    auto checksum = string_field(manifest.value(), "payload_sha256");
    if (!format || !schema || !library_version || !journal_id || !current_record_id || !records_count ||
        !checksum) {
        return Result<ArtifactTransferJournal>::failure(StatusCode::CorruptData,
                                                        "artifact transfer journal manifest is incomplete");
    }
    if (format.value() != "rbfsafe-artifact-transfer-journal" || schema.value() != kSchema) {
        return Result<ArtifactTransferJournal>::failure(StatusCode::IncompatibleFormat,
                                                        "unsupported artifact transfer journal schema");
    }
    if (!internal::valid_sha256(journal_id.value()) || !internal::valid_sha256(checksum.value()) ||
        (!current_record_id.value().empty() && !internal::valid_sha256(current_record_id.value()))) {
        return Result<ArtifactTransferJournal>::failure(
            StatusCode::CorruptData, "artifact transfer journal manifest identity is invalid");
    }
    if (records_count.value() > options.maximum_records) {
        return Result<ArtifactTransferJournal>::failure(
            StatusCode::ResourceLimit, "artifact transfer journal record count exceeds limit");
    }
    std::error_code error;
    const auto payload_size = std::filesystem::file_size(directory / "records.json", error);
    if (error) {
        return Result<ArtifactTransferJournal>::failure(
            StatusCode::IoError, "failed to inspect artifact transfer journal payload");
    }
    if (payload_size > options.maximum_payload_bytes) {
        return Result<ArtifactTransferJournal>::failure(
            StatusCode::ResourceLimit, "artifact transfer journal payload exceeds size limit");
    }
    auto actual_checksum = internal::sha256_file(directory / "records.json");
    if (!actual_checksum)
        return actual_checksum.error();
    if (actual_checksum.value() != checksum.value()) {
        return Result<ArtifactTransferJournal>::failure(
            StatusCode::CorruptData, "artifact transfer journal payload checksum mismatch");
    }
    auto payload = internal::read_json_file(directory / "records.json");
    if (!payload)
        return payload.error();
    auto payload_format = string_field(payload.value(), "format");
    auto payload_schema = size_field(payload.value(), "schema", 1000);
    const auto* records_json_value = payload.value().is_object() ? payload.value().find("records") : nullptr;
    if (!payload_format || !payload_schema || payload_format.value() != "rbfsafe-artifact-transfer-records" ||
        payload_schema.value() != kSchema || records_json_value == nullptr ||
        !records_json_value->is_array() || records_json_value->as_array().size() != records_count.value()) {
        return Result<ArtifactTransferJournal>::failure(
            StatusCode::CorruptData, "artifact transfer journal payload metadata is inconsistent");
    }
    ArtifactTransferJournal result;
    result.records_.reserve(records_count.value());
    for (const auto& item : records_json_value->as_array()) {
        auto record = decode_record(item);
        if (!record)
            return record.error();
        result.records_.push_back(std::move(record).value());
    }
    result.current_record_id_ = std::move(current_record_id).value();
    if (!result.valid() || result.identity() != journal_id.value()) {
        return Result<ArtifactTransferJournal>::failure(StatusCode::CorruptData,
                                                        "artifact transfer journal chain is inconsistent");
    }
    return result;
}

} // namespace rbfsafe
