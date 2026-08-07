#include <rbfsafe/modules/assurance.h>
#include <rbfsafe/modules/core.h>

#include "internal/binary.h"
#include "internal/certificate_utils.h"
#include "internal/coordination.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace rbfsafe {
namespace {

using internal::Json;

constexpr std::uint32_t kStorageSchema = 1;
constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kRecordFields = 8;
constexpr std::size_t kManifestFields = 7;
constexpr const char* kHistoryFormat = "rbfsafe-rotating-occupancy-publication-history";
constexpr const char* kRecordFormat = "rbfsafe-occupancy-publication-history-record";

bool valid_text(std::string_view value) {
    return !value.empty() && value.size() <= kMaximumIdentifierBytes &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char character) { return character < 0x20U || character == 0x7fU; });
}

bool exact_object(const Json& value, std::size_t fields) {
    return value.is_object() && value.as_object().size() == fields;
}

bool valid_load_options(const RotatingOccupancyPublicationHistoryLoadOptions& options) {
    const auto& trust = options.trust;
    const auto& occupancy = options.occupancy;
    return options.maximum_publications > 0 && options.maximum_manifest_bytes > 0 &&
           options.maximum_record_bytes > 0 && options.maximum_publication_bytes > 0 &&
           options.maximum_total_payload_bytes > 0 && trust.maximum_bundles > 0 &&
           trust.maximum_keys_per_bundle > 0 && trust.maximum_total_keys > 0 &&
           trust.maximum_signatures_per_rotation > 0 && trust.maximum_metadata_bytes > 0 &&
           trust.maximum_bundle_bytes > 0 && occupancy.maximum_occupancies > 0 &&
           occupancy.maximum_input_waypoints > 0 && occupancy.maximum_dimension > 0 &&
           occupancy.maximum_slices > 0 && occupancy.maximum_link_envelopes > 0 &&
           occupancy.maximum_conflicts > 0 && occupancy.maximum_slice_pair_evaluations > 0 &&
           occupancy.maximum_link_pair_evaluations > 0 && occupancy.maximum_payload_bytes > 0;
}

bool cancelled(const RotatingOccupancyPublicationHistoryLoadOptions& options) {
    return options.trust.cancellation.cancelled() || options.occupancy.cancellation.cancelled();
}

bool real_directory(const std::filesystem::path& path, std::error_code& error) {
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
}

bool real_regular_file(const std::filesystem::path& path, std::error_code& error) {
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status);
}

std::filesystem::path short_history_temporary(const std::filesystem::path& destination,
                                              std::string_view purpose, std::string_view extension = {}) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto history_root = destination.parent_path().parent_path();
    return history_root / ("." + std::string(purpose) + '-' + std::to_string(nonce) + std::string(extension));
}

std::string sequence_prefix(std::uint64_t sequence) {
    std::ostringstream output;
    output << std::setfill('0') << std::setw(20) << sequence;
    return output.str();
}

std::string record_filename(const OccupancyPublicationHistoryRecord& record) {
    return sequence_prefix(record.sequence) + '-' + record.id + ".json";
}

std::string publication_filename(const OccupancyPublication& publication) {
    return sequence_prefix(publication.publisher_sequence) + '-' + publication.id + ".json";
}

std::string payload_filename(const OccupancyPublication& publication) {
    return sequence_prefix(publication.publisher_sequence) + '-' + publication.payload_digest + ".bin";
}

Result<std::string> string_field(const Json& object, std::string_view key, std::size_t maximum_bytes,
                                 bool allow_empty = false) {
    const auto* value = object.is_object() ? object.find(key) : nullptr;
    if (value == nullptr || !value->is_string() || value->as_string().size() > maximum_bytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "rotating occupancy publication-history string field is invalid",
                                            std::string(key));
    }
    return value->as_string();
}

Result<std::uint64_t> decimal_field(const Json& object, std::string_view key) {
    auto text = string_field(object, key, 32);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto* begin = text.value().data();
    const auto* end = begin + text.value().size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return Result<std::uint64_t>::failure(
            StatusCode::CorruptData, "rotating occupancy publication-history decimal field is invalid",
            std::string(key));
    }
    return result;
}

Result<std::uint32_t> integer_field(const Json& object, std::string_view key, std::uint32_t maximum) {
    const auto* value = object.is_object() ? object.find(key) : nullptr;
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || value->as_number() > static_cast<double>(maximum) ||
        std::floor(value->as_number()) != value->as_number()) {
        return Result<std::uint32_t>::failure(
            StatusCode::CorruptData, "rotating occupancy publication-history integer field is invalid",
            std::string(key));
    }
    return static_cast<std::uint32_t>(value->as_number());
}

Result<Json> read_bounded_json(const std::filesystem::path& path, std::uintmax_t maximum_bytes,
                               std::string_view description) {
    std::error_code error;
    if (!real_regular_file(path, error)) {
        return Result<Json>::failure(error ? StatusCode::IoError : StatusCode::CorruptData,
                                     "rotating occupancy publication-history " + std::string(description) +
                                         " is missing, indirect, or not a regular file",
                                     path.string());
    }
    auto bytes = internal::read_binary_file(path, maximum_bytes);
    if (!bytes)
        return bytes.error();
    const std::string_view text(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
    return Json::parse(text);
}

Result<std::vector<std::byte>>
read_payload_file(const std::filesystem::path& path,
                  const RotatingOccupancyPublicationHistoryLoadOptions& options) {
    if (cancelled(options)) {
        return Result<std::vector<std::byte>>::failure(
            StatusCode::Cancelled, "rotating occupancy publication-history operation was cancelled");
    }
    std::error_code error;
    if (!real_regular_file(path, error)) {
        return Result<std::vector<std::byte>>::failure(
            error ? StatusCode::IoError : StatusCode::CorruptData,
            "rotating occupancy publication-history payload is missing, indirect, or not a regular file",
            path.string());
    }
    return internal::read_binary_file(path, options.occupancy.maximum_payload_bytes);
}

Json record_payload(const OccupancyPublicationHistoryRecord& record) {
    return Json::Object{
        {"authentication_tag", record.authentication_tag},
        {"id", record.id},
        {"parent_record_id", record.parent_record_id},
        {"payload_bytes", std::to_string(record.payload_bytes)},
        {"payload_digest", record.payload_digest},
        {"publication_id", record.publication_id},
        {"sequence", std::to_string(record.sequence)},
        {"storage_schema", static_cast<int>(record.storage_schema)},
    };
}

Json record_document(const OccupancyPublicationHistoryRecord& record) {
    const auto payload = record_payload(record);
    return Json::Object{
        {"checksum", internal::sha256(payload.dump(false))},
        {"format", kRecordFormat},
        {"library_version", kVersion},
        {"payload", payload},
        {"schema", static_cast<int>(kStorageSchema)},
    };
}

Result<OccupancyPublicationHistoryRecord> decode_record(const Json& document) {
    if (!exact_object(document, 5)) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::CorruptData,
            "rotating occupancy publication-history record document fields are invalid");
    }
    auto format = string_field(document, "format", 128);
    auto schema = integer_field(document, "schema", std::numeric_limits<std::uint32_t>::max());
    auto version = string_field(document, "library_version", 128);
    auto checksum = string_field(document, "checksum", 64);
    const auto* payload = document.find("payload");
    if (!format || !schema || !version || !checksum || payload == nullptr ||
        format.value() != kRecordFormat || schema.value() != kStorageSchema ||
        !exact_object(*payload, kRecordFields) || !internal::valid_sha256(checksum.value()) ||
        checksum.value() != internal::sha256(payload->dump(false))) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::CorruptData, "rotating occupancy publication-history record envelope is invalid");
    }
    auto storage = integer_field(*payload, "storage_schema", std::numeric_limits<std::uint32_t>::max());
    auto sequence = decimal_field(*payload, "sequence");
    auto id = string_field(*payload, "id", 64);
    auto parent = string_field(*payload, "parent_record_id", 64, true);
    auto publication = string_field(*payload, "publication_id", 64);
    auto authentication = string_field(*payload, "authentication_tag", kEd25519SignatureBytes * 2);
    auto digest = string_field(*payload, "payload_digest", 64);
    auto bytes = decimal_field(*payload, "payload_bytes");
    if (!storage || !sequence || !id || !parent || !publication || !authentication || !digest || !bytes ||
        storage.value() != kStorageSchema) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::CorruptData, "rotating occupancy publication-history record payload is incomplete");
    }
    OccupancyPublicationHistoryRecord result;
    result.storage_schema = storage.value();
    result.sequence = sequence.value();
    result.id = std::move(id).value();
    result.parent_record_id = std::move(parent).value();
    result.publication_id = std::move(publication).value();
    result.authentication_tag = std::move(authentication).value();
    result.payload_digest = std::move(digest).value();
    result.payload_bytes = bytes.value();
    if (!result.valid()) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::IdentityMismatch, "rotating occupancy publication-history record identity is invalid",
            result.id);
    }
    return result;
}

Json manifest_payload(std::string_view stream_id, std::string_view publisher_service_id,
                      std::string_view trust_root_bundle_id, std::string_view root_publication_id,
                      std::string_view timeline_id, std::string_view workspace_frame_id) {
    return Json::Object{
        {"publisher_service_id", std::string(publisher_service_id)},
        {"root_publication_id", std::string(root_publication_id)},
        {"storage_schema", static_cast<int>(kStorageSchema)},
        {"stream_id", std::string(stream_id)},
        {"timeline_id", std::string(timeline_id)},
        {"trust_root_bundle_id", std::string(trust_root_bundle_id)},
        {"workspace_frame_id", std::string(workspace_frame_id)},
    };
}

Json manifest_document(std::string_view stream_id, std::string_view publisher_service_id,
                       std::string_view trust_root_bundle_id, std::string_view root_publication_id,
                       std::string_view timeline_id, std::string_view workspace_frame_id) {
    const auto payload = manifest_payload(stream_id, publisher_service_id, trust_root_bundle_id,
                                          root_publication_id, timeline_id, workspace_frame_id);
    return Json::Object{
        {"format", kHistoryFormat},
        {"identity", internal::sha256(payload.dump(false))},
        {"library_version", kVersion},
        {"payload", payload},
        {"schema", static_cast<int>(kStorageSchema)},
    };
}

struct DecodedManifest {
    std::string stream_id;
    std::string publisher_service_id;
    std::string trust_root_bundle_id;
    std::string root_publication_id;
    std::string timeline_id;
    std::string workspace_frame_id;
};

Result<DecodedManifest> decode_manifest(const Json& document) {
    if (!exact_object(document, 5)) {
        return Result<DecodedManifest>::failure(
            StatusCode::CorruptData, "rotating occupancy publication-history manifest fields are invalid");
    }
    auto format = string_field(document, "format", 128);
    auto schema = integer_field(document, "schema", std::numeric_limits<std::uint32_t>::max());
    auto version = string_field(document, "library_version", 128);
    auto identity = string_field(document, "identity", 64);
    const auto* payload = document.find("payload");
    if (!format || !schema || !version || !identity || payload == nullptr ||
        format.value() != kHistoryFormat || schema.value() != kStorageSchema ||
        !exact_object(*payload, kManifestFields) || !internal::valid_sha256(identity.value()) ||
        identity.value() != internal::sha256(payload->dump(false))) {
        return Result<DecodedManifest>::failure(
            StatusCode::CorruptData, "rotating occupancy publication-history manifest envelope is invalid");
    }
    auto storage = integer_field(*payload, "storage_schema", std::numeric_limits<std::uint32_t>::max());
    auto stream = string_field(*payload, "stream_id", kMaximumIdentifierBytes);
    auto publisher = string_field(*payload, "publisher_service_id", kMaximumIdentifierBytes);
    auto trust_root = string_field(*payload, "trust_root_bundle_id", 64);
    auto root_publication = string_field(*payload, "root_publication_id", 64);
    auto timeline = string_field(*payload, "timeline_id", kMaximumIdentifierBytes);
    auto frame = string_field(*payload, "workspace_frame_id", kMaximumIdentifierBytes);
    if (!storage || !stream || !publisher || !trust_root || !root_publication || !timeline || !frame ||
        storage.value() != kStorageSchema || !internal::valid_sha256(trust_root.value()) ||
        !internal::valid_sha256(root_publication.value())) {
        return Result<DecodedManifest>::failure(
            StatusCode::CorruptData, "rotating occupancy publication-history manifest payload is incomplete");
    }
    DecodedManifest result;
    result.stream_id = std::move(stream).value();
    result.publisher_service_id = std::move(publisher).value();
    result.trust_root_bundle_id = std::move(trust_root).value();
    result.root_publication_id = std::move(root_publication).value();
    result.timeline_id = std::move(timeline).value();
    result.workspace_frame_id = std::move(frame).value();
    return result;
}

OccupancyPublicationHistoryRecord make_record(const OccupancyPublication& publication,
                                              std::string parent_record_id) {
    OccupancyPublicationHistoryRecord result;
    result.storage_schema = kStorageSchema;
    result.sequence = publication.publisher_sequence;
    result.parent_record_id = std::move(parent_record_id);
    result.publication_id = publication.id;
    result.authentication_tag = publication.authentication_tag;
    result.payload_digest = publication.payload_digest;
    result.payload_bytes = publication.payload_bytes;
    result.id = internal::occupancy_publication_history_record_identity(result);
    return result;
}

Result<void> verify_publication_entry(const std::filesystem::path& payload_path,
                                      const OccupancyPublication& publication,
                                      const ServiceTrustBundle& trust_bundle, std::string_view stream_id,
                                      std::string_view publisher_service_id,
                                      std::string_view parent_publication_id,
                                      const RotatingOccupancyPublicationHistoryLoadOptions& options) {
    auto verified = verify_continuous_fleet_occupancy_publication(
        payload_path, publication, trust_bundle, stream_id, publisher_service_id, trust_bundle.id(),
        parent_publication_id, publication.valid_from_tick, options.occupancy);
    if (!verified)
        return verified.error();
    return Result<void>::success();
}

Result<void> write_immutable_text(const std::filesystem::path& destination, const std::string& content) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to inspect immutable rotating occupancy history record",
                                         destination.string());
        }
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "immutable rotating occupancy history record already exists",
                                     destination.string());
    }
    const auto temporary = short_history_temporary(destination, "record", ".json");
    auto written = internal::write_text_file(temporary, content);
    if (!written) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return written;
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to publish immutable rotating occupancy history record",
                                     destination.string());
    }
    return Result<void>::success();
}

Result<void> publish_payload_bytes(const std::filesystem::path& destination, std::span<const std::byte> bytes,
                                   std::uintmax_t maximum_bytes) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to inspect immutable rotating occupancy history payload",
                                         destination.string());
        }
        if (!real_regular_file(destination, error)) {
            return Result<void>::failure(
                error ? StatusCode::IoError : StatusCode::CorruptData,
                "orphan rotating occupancy history payload is indirect or not a regular file",
                destination.string());
        }
        auto existing = internal::read_binary_file(destination, maximum_bytes);
        if (!existing)
            return existing.error();
        if (!std::equal(existing.value().begin(), existing.value().end(), bytes.begin(), bytes.end())) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "orphan rotating occupancy history payload bytes are inconsistent",
                                         destination.string());
        }
        return Result<void>::success();
    }
    const auto temporary = short_history_temporary(destination, "payload", ".bin");
    internal::BinaryWriter writer;
    writer.bytes(bytes);
    auto written = writer.save(temporary);
    if (!written) {
        std::filesystem::remove(temporary, error);
        return written;
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to publish immutable rotating occupancy history payload",
                                     destination.string());
    }
    return Result<void>::success();
}

Result<void> publish_publication_file(const std::filesystem::path& destination,
                                      const OccupancyPublication& publication, std::uintmax_t maximum_bytes) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to inspect immutable rotating occupancy history publication",
                                         destination.string());
        }
        auto existing = OccupancyPublication::load(destination, maximum_bytes);
        if (!existing)
            return existing.error();
        if (existing.value().id != publication.id ||
            existing.value().authentication_tag != publication.authentication_tag) {
            return Result<void>::failure(StatusCode::IdentityMismatch,
                                         "orphan rotating occupancy history publication is inconsistent",
                                         destination.string());
        }
        return Result<void>::success();
    }
    const auto temporary = short_history_temporary(destination, "publication", ".json");
    auto saved = publication.save(temporary);
    if (!saved) {
        std::filesystem::remove(temporary, error);
        return saved;
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to publish immutable rotating occupancy history publication",
                                     destination.string());
    }
    return Result<void>::success();
}

class DirectoryLock {
  public:
    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;
    DirectoryLock(DirectoryLock&& other) noexcept : path_(std::move(other.path_)), held_(other.held_) {
        other.held_ = false;
    }
    DirectoryLock& operator=(DirectoryLock&&) = delete;

    ~DirectoryLock() {
        if (!held_)
            return;
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    static Result<DirectoryLock> acquire(const std::filesystem::path& history) {
        const auto path = history / ".writer-lock";
        std::error_code error;
        const bool created = std::filesystem::create_directory(path, error);
        if (error) {
            return Result<DirectoryLock>::failure(StatusCode::IoError,
                                                  "failed to acquire rotating occupancy history writer lock",
                                                  path.string());
        }
        if (!created) {
            return Result<DirectoryLock>::failure(StatusCode::ResourceLimit,
                                                  "rotating occupancy history writer lock is already held",
                                                  path.string());
        }
        return DirectoryLock(path);
    }

  private:
    explicit DirectoryLock(std::filesystem::path path) : path_(std::move(path)) {}

    std::filesystem::path path_;
    bool held_ = true;
};

Result<void> clone_trust_history(const ServiceTrustHistory& source, const std::filesystem::path& destination,
                                 std::string_view expected_root_bundle_id,
                                 std::string_view expected_head_bundle_id,
                                 const ServiceTrustHistoryLoadOptions& options) {
    if (!source.valid() || source.root_bundle_id() != expected_root_bundle_id ||
        source.current_bundle_id() != expected_head_bundle_id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "source trust history does not match rotating occupancy caller pins");
    }
    auto root = source.bundle(std::string(expected_root_bundle_id));
    if (!root)
        return root.error();
    auto target =
        ServiceTrustHistory::create(destination, root.value(), std::string(expected_root_bundle_id));
    if (!target)
        return target.error();
    const auto& records = source.records();
    for (std::size_t index = 1; index < records.size(); ++index) {
        if (options.cancellation.cancelled()) {
            return Result<void>::failure(StatusCode::Cancelled,
                                         "rotating occupancy trust-history clone was cancelled");
        }
        const auto& record = records[index];
        auto successor = source.bundle(record.bundle_id);
        if (!successor)
            return successor.error();
        Result<ServiceTrustRotationRecord> appended =
            record.authorization_set
                ? target.value().publish(successor.value(), *record.authorization_set,
                                         target.value().current_bundle_id(), options.maximum_bundles)
            : record.authorization
                ? target.value().publish(successor.value(), *record.authorization,
                                         target.value().current_bundle_id(), options.maximum_bundles)
                : Result<ServiceTrustRotationRecord>::failure(
                      StatusCode::CorruptData, "source trust history successor has no authorization");
        if (!appended)
            return appended.error();
    }
    if (target.value().current_bundle_id() != expected_head_bundle_id) {
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "cloned trust history does not reproduce the expected head");
    }
    return Result<void>::success();
}

std::size_t trust_bundle_position(const ServiceTrustHistory& history, std::string_view bundle_id) {
    const auto& records = history.records();
    const auto found = std::find_if(records.begin(), records.end(), [bundle_id](const auto& record) {
        return record.bundle_id == bundle_id;
    });
    return found == records.end() ? std::numeric_limits<std::size_t>::max()
                                  : static_cast<std::size_t>(std::distance(records.begin(), found));
}

OccupancyPublicationHistoryRelation relation_for(std::size_t first_count, std::size_t second_count,
                                                 std::size_t common_count) {
    if (common_count == first_count && common_count == second_count)
        return OccupancyPublicationHistoryRelation::Identical;
    if (common_count == second_count)
        return OccupancyPublicationHistoryRelation::FirstExtendsSecond;
    if (common_count == first_count)
        return OccupancyPublicationHistoryRelation::SecondExtendsFirst;
    return OccupancyPublicationHistoryRelation::Forked;
}

bool valid_relation(OccupancyPublicationHistoryRelation relation, std::uint64_t first_count,
                    std::uint64_t second_count, std::uint64_t common_count, std::string_view first_head,
                    std::string_view second_head) {
    if (first_count == 0 || second_count == 0 || common_count == 0 ||
        common_count > std::min(first_count, second_count))
        return false;
    switch (relation) {
    case OccupancyPublicationHistoryRelation::Identical:
        return first_count == second_count && common_count == first_count && first_head == second_head;
    case OccupancyPublicationHistoryRelation::FirstExtendsSecond:
        return first_count > second_count && common_count == second_count && first_head != second_head;
    case OccupancyPublicationHistoryRelation::SecondExtendsFirst:
        return second_count > first_count && common_count == first_count && first_head != second_head;
    case OccupancyPublicationHistoryRelation::Forked:
        return common_count < std::min(first_count, second_count) && first_head != second_head;
    }
    return false;
}

} // namespace

namespace internal {

std::string
rotating_occupancy_publication_history_audit_identity(const RotatingOccupancyPublicationHistoryAudit& audit) {
    return sha256(
        Json(Json::Object{
                 {"common_publication_id", audit.common_publication_id},
                 {"common_publication_prefix_count", std::to_string(audit.common_publication_prefix_count)},
                 {"common_trust_bundle_id", audit.common_trust_bundle_id},
                 {"common_trust_prefix_count", std::to_string(audit.common_trust_prefix_count)},
                 {"first_head_publication_id", audit.first_head_publication_id},
                 {"first_publication_count", std::to_string(audit.first_publication_count)},
                 {"first_trust_bundle_count", std::to_string(audit.first_trust_bundle_count)},
                 {"first_trust_head_bundle_id", audit.first_trust_head_bundle_id},
                 {"publication_relation", static_cast<int>(audit.publication_relation)},
                 {"publisher_service_id", audit.publisher_service_id},
                 {"root_publication_id", audit.root_publication_id},
                 {"second_head_publication_id", audit.second_head_publication_id},
                 {"second_publication_count", std::to_string(audit.second_publication_count)},
                 {"second_trust_bundle_count", std::to_string(audit.second_trust_bundle_count)},
                 {"second_trust_head_bundle_id", audit.second_trust_head_bundle_id},
                 {"storage_schema", static_cast<int>(audit.storage_schema)},
                 {"stream_id", audit.stream_id},
                 {"trust_relation", static_cast<int>(audit.trust_relation)},
                 {"trust_root_bundle_id", audit.trust_root_bundle_id},
             })
            .dump(false));
}

} // namespace internal

bool RotatingOccupancyPublicationHistoryAudit::valid() const {
    const auto publication_relation_value = static_cast<std::uint8_t>(publication_relation);
    const auto trust_relation_value = static_cast<std::uint8_t>(trust_relation);
    return storage_schema == kStorageSchema && internal::valid_sha256(id) &&
           id == internal::rotating_occupancy_publication_history_audit_identity(*this) &&
           publication_relation_value <=
               static_cast<std::uint8_t>(OccupancyPublicationHistoryRelation::Forked) &&
           trust_relation_value <= static_cast<std::uint8_t>(OccupancyPublicationHistoryRelation::Forked) &&
           valid_text(stream_id) && valid_text(publisher_service_id) &&
           internal::valid_sha256(trust_root_bundle_id) && internal::valid_sha256(root_publication_id) &&
           internal::valid_sha256(first_trust_head_bundle_id) &&
           internal::valid_sha256(second_trust_head_bundle_id) &&
           internal::valid_sha256(first_head_publication_id) &&
           internal::valid_sha256(second_head_publication_id) &&
           internal::valid_sha256(common_trust_bundle_id) && internal::valid_sha256(common_publication_id) &&
           valid_relation(trust_relation, first_trust_bundle_count, second_trust_bundle_count,
                          common_trust_prefix_count, first_trust_head_bundle_id,
                          second_trust_head_bundle_id) &&
           valid_relation(publication_relation, first_publication_count, second_publication_count,
                          common_publication_prefix_count, first_head_publication_id,
                          second_head_publication_id);
}

bool RotatingOccupancyPublicationHistory::valid() const {
    if (storage_schema_ != kStorageSchema || directory_.empty() || !valid_text(stream_id_) ||
        !valid_text(publisher_service_id_) || !internal::valid_sha256(trust_root_bundle_id_) ||
        !internal::valid_sha256(current_trust_bundle_id_) || !internal::valid_sha256(root_publication_id_) ||
        !internal::valid_sha256(current_publication_id_) || !valid_text(timeline_id_) ||
        !valid_text(workspace_frame_id_) || !trust_history_ || !trust_history_->valid() ||
        trust_history_->root_bundle_id() != trust_root_bundle_id_ ||
        trust_history_->current_bundle_id() != current_trust_bundle_id_ || records_.empty() ||
        records_.size() != publications_.size() || records_.size() != payload_paths_.size() ||
        !valid_load_options(options_)) {
        return false;
    }
    std::size_t previous_trust_position = 0;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        const auto& record = records_[index];
        const auto& publication_value = publications_[index];
        const auto trust_position = trust_bundle_position(*trust_history_, publication_value.trust_bundle_id);
        if (!record.valid() || !publication_value.valid() || record.sequence != index + 1 ||
            publication_value.publisher_sequence != index + 1 ||
            record.publication_id != publication_value.id ||
            record.authentication_tag != publication_value.authentication_tag ||
            record.payload_digest != publication_value.payload_digest ||
            record.payload_bytes != publication_value.payload_bytes ||
            publication_value.stream_id != stream_id_ ||
            publication_value.publisher_service_id != publisher_service_id_ ||
            publication_value.timeline_id != timeline_id_ ||
            publication_value.workspace_frame_id != workspace_frame_id_ || payload_paths_[index].empty() ||
            trust_position == std::numeric_limits<std::size_t>::max() ||
            (index > 0 && trust_position < previous_trust_position)) {
            return false;
        }
        previous_trust_position = trust_position;
        if (index == 0) {
            if (!record.parent_record_id.empty() || !publication_value.parent_publication_id.empty() ||
                publication_value.id != root_publication_id_) {
                return false;
            }
        } else if (record.parent_record_id != records_[index - 1].id ||
                   !verify_occupancy_publication_successor(publications_[index - 1], publication_value)) {
            return false;
        }
    }
    return current_publication_id_ == publications_.back().id;
}

Result<RotatingOccupancyPublicationHistory> RotatingOccupancyPublicationHistory::create(
    const std::filesystem::path& directory, const OccupancyPublication& root_publication,
    const std::filesystem::path& root_payload_path, const ServiceTrustHistory& trust_history,
    std::string_view expected_stream_id, std::string_view expected_publisher_service_id,
    std::string_view expected_trust_root_bundle_id, std::string_view expected_trust_head_bundle_id,
    std::string_view expected_root_publication_id,
    const RotatingOccupancyPublicationHistoryLoadOptions& options) {
    if (directory.empty() || directory == directory.root_path() || root_payload_path.empty() ||
        !root_publication.valid() || !trust_history.valid() || !valid_text(expected_stream_id) ||
        !valid_text(expected_publisher_service_id) ||
        !internal::valid_sha256(std::string(expected_trust_root_bundle_id)) ||
        !internal::valid_sha256(std::string(expected_trust_head_bundle_id)) ||
        !internal::valid_sha256(std::string(expected_root_publication_id)) || !valid_load_options(options)) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::InvalidArgument, "rotating occupancy publication-history creation input is invalid");
    }
    if (cancelled(options)) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::Cancelled, "rotating occupancy publication-history creation was cancelled");
    }
    if (trust_history.root_bundle_id() != expected_trust_root_bundle_id ||
        trust_history.current_bundle_id() != expected_trust_head_bundle_id ||
        root_publication.publisher_sequence != 1 || !root_publication.parent_publication_id.empty() ||
        root_publication.stream_id != expected_stream_id ||
        root_publication.publisher_service_id != expected_publisher_service_id ||
        root_publication.trust_bundle_id != expected_trust_head_bundle_id ||
        root_publication.id != expected_root_publication_id) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IdentityMismatch,
            "rotating occupancy publication-history root does not match caller pins", root_publication.id);
    }
    auto current_trust = trust_history.current_bundle();
    if (!current_trust)
        return current_trust.error();
    auto payload = read_payload_file(root_payload_path, options);
    if (!payload)
        return payload.error();
    if (payload.value().size() > options.maximum_total_payload_bytes) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::ResourceLimit,
            "rotating occupancy publication-history root payload exceeds total limit");
    }

    std::error_code error;
    if (std::filesystem::exists(directory, error)) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IoError, "rotating occupancy publication-history destination already exists",
            directory.string());
    }
    if (error) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IoError, "failed to inspect rotating occupancy publication-history destination",
            directory.string());
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::IoError, "failed to create rotating occupancy publication-history parent",
                directory.parent_path().string());
        }
    }

    const auto temporary =
        directory.parent_path() /
        (".rh-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const bool temporary_created = std::filesystem::create_directory(temporary, error);
    if (error || !temporary_created) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IoError, "failed to create temporary rotating occupancy publication history");
    }
    const auto staged_trust =
        directory.parent_path() /
        (".rt-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
        std::filesystem::remove_all(staged_trust, ignored);
    };
    const bool records_created = std::filesystem::create_directory(temporary / "records", error);
    const bool publications_created =
        !error && std::filesystem::create_directory(temporary / "publications", error);
    const bool payloads_created = !error && std::filesystem::create_directory(temporary / "payloads", error);
    if (error || !records_created || !publications_created || !payloads_created) {
        cleanup();
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IoError, "failed to create rotating occupancy publication-history data directories");
    }
    auto cloned = clone_trust_history(trust_history, staged_trust, expected_trust_root_bundle_id,
                                      expected_trust_head_bundle_id, options.trust);
    if (!cloned) {
        cleanup();
        return cloned.error();
    }
    std::filesystem::rename(staged_trust, temporary / "trust-history", error);
    if (error) {
        cleanup();
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IoError, "failed to stage rotating occupancy trust history");
    }
    internal::BinaryWriter payload_writer;
    payload_writer.bytes(payload.value());
    const auto stored_payload = temporary / "payloads" / payload_filename(root_publication);
    auto saved_payload = payload_writer.save(stored_payload);
    if (!saved_payload) {
        cleanup();
        return saved_payload.error();
    }
    const auto stored_publication = temporary / "publications" / publication_filename(root_publication);
    const auto staged_publication =
        temporary / (".root-publication-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    auto saved_publication = root_publication.save(staged_publication);
    if (!saved_publication) {
        cleanup();
        return saved_publication.error();
    }
    std::filesystem::rename(staged_publication, stored_publication, error);
    if (error) {
        cleanup();
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IoError, "failed to stage rotating occupancy root publication");
    }
    auto verified = verify_publication_entry(stored_payload, root_publication, current_trust.value(),
                                             expected_stream_id, expected_publisher_service_id, "", options);
    if (!verified) {
        cleanup();
        return verified.error();
    }
    const auto root_record = make_record(root_publication, "");
    if (!root_record.valid()) {
        cleanup();
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::InternalError,
            "generated rotating occupancy publication-history root record is invalid");
    }
    auto record_written = internal::write_text_file(temporary / "records" / record_filename(root_record),
                                                    record_document(root_record).dump(true) + "\n");
    if (!record_written) {
        cleanup();
        return record_written.error();
    }
    auto manifest_written = internal::write_text_file(
        temporary / "manifest.json",
        manifest_document(root_publication.stream_id, root_publication.publisher_service_id,
                          expected_trust_root_bundle_id, root_publication.id, root_publication.timeline_id,
                          root_publication.workspace_frame_id)
                .dump(true) +
            "\n");
    if (!manifest_written) {
        cleanup();
        return manifest_written.error();
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        cleanup();
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IoError, "failed to publish rotating occupancy publication history",
            directory.string());
    }
    return RotatingOccupancyPublicationHistory::open(
        directory, expected_stream_id, expected_publisher_service_id, expected_trust_root_bundle_id,
        expected_trust_head_bundle_id, expected_root_publication_id, expected_root_publication_id, options);
}

Result<RotatingOccupancyPublicationHistory> RotatingOccupancyPublicationHistory::open(
    const std::filesystem::path& directory, std::string_view expected_stream_id,
    std::string_view expected_publisher_service_id, std::string_view expected_trust_root_bundle_id,
    std::string_view expected_trust_head_bundle_id, std::string_view expected_root_publication_id,
    std::string_view expected_head_publication_id,
    const RotatingOccupancyPublicationHistoryLoadOptions& options) {
    if (directory.empty() || !valid_text(expected_stream_id) || !valid_text(expected_publisher_service_id) ||
        !internal::valid_sha256(std::string(expected_trust_root_bundle_id)) ||
        !internal::valid_sha256(std::string(expected_trust_head_bundle_id)) ||
        !internal::valid_sha256(std::string(expected_root_publication_id)) ||
        !internal::valid_sha256(std::string(expected_head_publication_id)) || !valid_load_options(options)) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::InvalidArgument, "rotating occupancy publication-history open input is invalid");
    }
    if (cancelled(options)) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::Cancelled, "rotating occupancy publication-history load was cancelled");
    }
    std::error_code error;
    if (!real_directory(directory, error)) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            error ? StatusCode::IoError : StatusCode::CorruptData,
            "rotating occupancy publication-history directory is missing or indirect", directory.string());
    }
    auto manifest_document_value =
        read_bounded_json(directory / "manifest.json", options.maximum_manifest_bytes, "manifest");
    if (!manifest_document_value)
        return manifest_document_value.error();
    auto manifest = decode_manifest(manifest_document_value.value());
    if (!manifest)
        return manifest.error();
    if (manifest.value().stream_id != expected_stream_id ||
        manifest.value().publisher_service_id != expected_publisher_service_id ||
        manifest.value().trust_root_bundle_id != expected_trust_root_bundle_id ||
        manifest.value().root_publication_id != expected_root_publication_id) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IdentityMismatch,
            "rotating occupancy publication-history manifest does not match caller pins",
            manifest.value().root_publication_id);
    }
    for (const auto& subdirectory : {"records", "publications", "payloads", "trust-history"}) {
        if (!real_directory(directory / subdirectory, error)) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                error ? StatusCode::IoError : StatusCode::CorruptData,
                "rotating occupancy publication-history data directory is missing or indirect", subdirectory);
        }
    }
    auto trust_history =
        ServiceTrustHistory::open(directory / "trust-history", std::string(expected_trust_root_bundle_id),
                                  std::string(expected_trust_head_bundle_id), options.trust);
    if (!trust_history)
        return trust_history.error();

    std::vector<std::pair<std::string, OccupancyPublicationHistoryRecord>> decoded;
    std::size_t entries = 0;
    const auto records_directory = directory / "records";
    for (std::filesystem::directory_iterator iterator(records_directory, error), end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::IoError, "failed to enumerate rotating occupancy publication-history records");
        }
        if (cancelled(options)) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::Cancelled, "rotating occupancy publication-history load was cancelled");
        }
        if (entries >= options.maximum_publications) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::ResourceLimit,
                "rotating occupancy publication-history record-entry count exceeds limit");
        }
        ++entries;
        const auto filename = iterator->path().filename().string();
        const auto status = iterator->symlink_status(error);
        if (error) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::IoError, "failed to inspect rotating occupancy publication-history record");
        }
        const bool regular = std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status);
        if (regular && iterator->path().extension() == ".json") {
            auto document = read_bounded_json(iterator->path(), options.maximum_record_bytes, "record");
            if (!document)
                return document.error();
            auto record = decode_record(document.value());
            if (!record)
                return record.error();
            decoded.emplace_back(filename, std::move(record).value());
        } else if (filename.find(".tmp-") == std::string::npos) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::CorruptData, "unexpected rotating occupancy publication-history record entry",
                filename);
        }
    }
    if (error) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IoError, "failed to enumerate rotating occupancy publication-history records");
    }
    if (decoded.empty()) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::CorruptData, "rotating occupancy publication history contains no records");
    }
    std::sort(decoded.begin(), decoded.end(), [](const auto& left, const auto& right) {
        return std::tie(left.second.sequence, left.second.id) <
               std::tie(right.second.sequence, right.second.id);
    });

    RotatingOccupancyPublicationHistory result;
    result.directory_ = directory;
    result.storage_schema_ = kStorageSchema;
    result.stream_id_ = manifest.value().stream_id;
    result.publisher_service_id_ = manifest.value().publisher_service_id;
    result.trust_root_bundle_id_ = manifest.value().trust_root_bundle_id;
    result.current_trust_bundle_id_ = trust_history.value().current_bundle_id();
    result.root_publication_id_ = manifest.value().root_publication_id;
    result.timeline_id_ = manifest.value().timeline_id;
    result.workspace_frame_id_ = manifest.value().workspace_frame_id;
    result.trust_history_ = trust_history.value();
    result.options_ = options;
    std::uintmax_t total_payload_bytes = 0;
    std::size_t previous_trust_position = 0;
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        if (cancelled(options)) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::Cancelled, "rotating occupancy publication-history load was cancelled");
        }
        const auto& filename = decoded[index].first;
        const auto& record = decoded[index].second;
        if (record.sequence != index + 1 || filename != record_filename(record) ||
            (index == 0 &&
             (!record.parent_record_id.empty() || record.publication_id != result.root_publication_id_)) ||
            (index > 0 && record.parent_record_id != decoded[index - 1].second.id)) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::CorruptData,
                "rotating occupancy publication-history record chain is forked or corrupt", record.id);
        }
        const auto publication_path =
            directory / "publications" /
            (sequence_prefix(record.sequence) + '-' + record.publication_id + ".json");
        auto publication_value =
            OccupancyPublication::load(publication_path, options.maximum_publication_bytes);
        if (!publication_value)
            return publication_value.error();
        const auto& publication = publication_value.value();
        if (publication.publisher_sequence != record.sequence || publication.id != record.publication_id ||
            publication.authentication_tag != record.authentication_tag ||
            publication.payload_digest != record.payload_digest ||
            publication.payload_bytes != record.payload_bytes || publication.stream_id != result.stream_id_ ||
            publication.publisher_service_id != result.publisher_service_id_ ||
            publication.timeline_id != result.timeline_id_ ||
            publication.workspace_frame_id != result.workspace_frame_id_) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::IdentityMismatch,
                "rotating occupancy publication-history record does not match its publication", record.id);
        }
        const auto trust_position =
            trust_bundle_position(*result.trust_history_, publication.trust_bundle_id);
        if (trust_position == std::numeric_limits<std::size_t>::max() ||
            (index > 0 && trust_position < previous_trust_position)) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::IdentityMismatch,
                "rotating occupancy publication trust bundle is unknown or moved backward",
                publication.trust_bundle_id);
        }
        previous_trust_position = trust_position;
        auto trust_bundle = result.trust_history_->bundle(publication.trust_bundle_id);
        if (!trust_bundle)
            return trust_bundle.error();
        if (record.payload_bytes > options.maximum_total_payload_bytes - total_payload_bytes) {
            return Result<RotatingOccupancyPublicationHistory>::failure(
                StatusCode::ResourceLimit,
                "rotating occupancy publication-history total payload bytes exceed limit");
        }
        total_payload_bytes += static_cast<std::uintmax_t>(record.payload_bytes);
        const auto payload_path = directory / "payloads" /
                                  (sequence_prefix(record.sequence) + '-' + record.payload_digest + ".bin");
        const auto expected_parent =
            index == 0 ? std::string_view{} : std::string_view(result.publications_.back().id);
        auto verified =
            verify_publication_entry(payload_path, publication, trust_bundle.value(), result.stream_id_,
                                     result.publisher_service_id_, expected_parent, options);
        if (!verified)
            return verified.error();
        if (index > 0) {
            auto successor = verify_occupancy_publication_successor(result.publications_.back(), publication);
            if (!successor)
                return successor.error();
        }
        result.records_.push_back(record);
        result.publications_.push_back(publication);
        result.payload_paths_.push_back(payload_path);
    }
    result.current_publication_id_ = result.publications_.back().id;
    if (!result.valid()) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::CorruptData, "rotating occupancy publication-history replay is inconsistent");
    }
    if (result.current_publication_id_ != expected_head_publication_id) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::IdentityMismatch,
            "rotating occupancy publication-history head does not match caller expected head",
            result.current_publication_id_);
    }
    return result;
}

Result<RotatingOccupancyPublicationHistory> RotatingOccupancyPublicationHistory::open(
    const std::filesystem::path& directory, std::string_view expected_stream_id,
    std::string_view expected_publisher_service_id, std::string_view expected_trust_root_bundle_id,
    const ServiceTrustCheckpoint& checkpoint, std::string_view expected_checkpoint_id,
    std::string_view expected_root_publication_id, std::string_view expected_head_publication_id,
    const RotatingOccupancyPublicationHistoryLoadOptions& options) {
    if (!checkpoint.valid() || !internal::valid_sha256(std::string(expected_checkpoint_id))) {
        return Result<RotatingOccupancyPublicationHistory>::failure(
            StatusCode::InvalidArgument,
            "rotating occupancy publication-history checkpoint open input is invalid");
    }
    auto history = RotatingOccupancyPublicationHistory::open(
        directory, expected_stream_id, expected_publisher_service_id, expected_trust_root_bundle_id,
        checkpoint.head_bundle_id, expected_root_publication_id, expected_head_publication_id, options);
    if (!history)
        return history.error();
    auto verified = verify_service_trust_checkpoint(*history.value().trust_history_, checkpoint,
                                                    std::string(expected_checkpoint_id));
    if (!verified)
        return verified.error();
    return history;
}

Result<ServiceTrustHistory> RotatingOccupancyPublicationHistory::trust_history() const {
    if (!valid()) {
        return Result<ServiceTrustHistory>::failure(
            StatusCode::InvalidArgument, "rotating occupancy publication-history object is not initialized");
    }
    return *trust_history_;
}

Result<ServiceTrustBundle> RotatingOccupancyPublicationHistory::current_trust_bundle() const {
    if (!valid()) {
        return Result<ServiceTrustBundle>::failure(
            StatusCode::InvalidArgument, "rotating occupancy publication-history object is not initialized");
    }
    return trust_history_->current_bundle();
}

Result<OccupancyPublication> RotatingOccupancyPublicationHistory::current_publication() const {
    if (!valid()) {
        return Result<OccupancyPublication>::failure(
            StatusCode::InvalidArgument, "rotating occupancy publication-history object is not initialized");
    }
    return publications_.back();
}

Result<OccupancyPublication>
RotatingOccupancyPublicationHistory::publication(std::string_view publication_id) const {
    if (!valid() || !internal::valid_sha256(std::string(publication_id))) {
        return Result<OccupancyPublication>::failure(
            StatusCode::InvalidArgument, "rotating occupancy publication-history lookup input is invalid");
    }
    const auto found =
        std::find_if(publications_.begin(), publications_.end(),
                     [publication_id](const auto& candidate) { return candidate.id == publication_id; });
    if (found == publications_.end()) {
        return Result<OccupancyPublication>::failure(
            StatusCode::InvalidArgument, "occupancy publication is not registered in this rotating history",
            std::string(publication_id));
    }
    return *found;
}

Result<VerifiedOccupancyPublication>
RotatingOccupancyPublicationHistory::verify(std::string_view publication_id,
                                            std::uint64_t evaluation_tick) const {
    if (!valid() || !internal::valid_sha256(std::string(publication_id))) {
        return Result<VerifiedOccupancyPublication>::failure(
            StatusCode::InvalidArgument,
            "rotating occupancy publication-history verification input is invalid");
    }
    const auto found =
        std::find_if(publications_.begin(), publications_.end(),
                     [publication_id](const auto& candidate) { return candidate.id == publication_id; });
    if (found == publications_.end()) {
        return Result<VerifiedOccupancyPublication>::failure(
            StatusCode::InvalidArgument, "occupancy publication is not registered in this rotating history",
            std::string(publication_id));
    }
    const auto index = static_cast<std::size_t>(std::distance(publications_.begin(), found));
    const auto expected_parent =
        index == 0 ? std::string_view{} : std::string_view(publications_[index - 1].id);
    auto trust_bundle = trust_history_->bundle(found->trust_bundle_id);
    if (!trust_bundle)
        return trust_bundle.error();
    return verify_continuous_fleet_occupancy_publication(
        payload_paths_[index], *found, trust_bundle.value(), stream_id_, publisher_service_id_,
        found->trust_bundle_id, expected_parent, evaluation_tick, options_.occupancy);
}

Result<ServiceTrustRotationRecord> RotatingOccupancyPublicationHistory::rotate_trust(
    const ServiceTrustBundle& successor, const ServiceTrustBundleAuthorization& authorization,
    std::string_view expected_trust_head_bundle_id, std::size_t maximum_trust_bundles) {
    return rotate_trust_impl(successor, authorization, std::nullopt, expected_trust_head_bundle_id,
                             maximum_trust_bundles);
}

Result<ServiceTrustRotationRecord> RotatingOccupancyPublicationHistory::rotate_trust(
    const ServiceTrustBundle& successor, const ServiceTrustBundleAuthorizationSet& authorization_set,
    std::string_view expected_trust_head_bundle_id, std::size_t maximum_trust_bundles) {
    return rotate_trust_impl(successor, std::nullopt, authorization_set, expected_trust_head_bundle_id,
                             maximum_trust_bundles);
}

Result<ServiceTrustRotationRecord> RotatingOccupancyPublicationHistory::rotate_trust_impl(
    const ServiceTrustBundle& successor, std::optional<ServiceTrustBundleAuthorization> authorization,
    std::optional<ServiceTrustBundleAuthorizationSet> authorization_set,
    std::string_view expected_trust_head_bundle_id, std::size_t maximum_trust_bundles) {
    if (!valid() || !successor.valid() ||
        !internal::valid_sha256(std::string(expected_trust_head_bundle_id)) || maximum_trust_bundles == 0 ||
        (authorization.has_value() == authorization_set.has_value())) {
        return Result<ServiceTrustRotationRecord>::failure(
            StatusCode::InvalidArgument, "rotating occupancy trust publication input is invalid");
    }
    if (cancelled(options_)) {
        return Result<ServiceTrustRotationRecord>::failure(
            StatusCode::Cancelled, "rotating occupancy trust publication was cancelled");
    }
    auto lock = DirectoryLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto held_lock = std::move(lock).value();
    (void)held_lock;
    auto fresh = RotatingOccupancyPublicationHistory::open(
        directory_, stream_id_, publisher_service_id_, trust_root_bundle_id_, expected_trust_head_bundle_id,
        root_publication_id_, current_publication_id_, options_);
    if (!fresh)
        return fresh.error();
    const auto effective_limit = std::min(maximum_trust_bundles, options_.trust.maximum_bundles);
    Result<ServiceTrustRotationRecord> appended =
        authorization_set
            ? fresh.value().trust_history_->publish(
                  successor, *authorization_set, std::string(expected_trust_head_bundle_id), effective_limit)
            : fresh.value().trust_history_->publish(
                  successor, *authorization, std::string(expected_trust_head_bundle_id), effective_limit);
    if (!appended)
        return appended.error();
    auto committed = RotatingOccupancyPublicationHistory::open(
        directory_, stream_id_, publisher_service_id_, trust_root_bundle_id_, successor.id(),
        root_publication_id_, current_publication_id_, options_);
    if (!committed)
        return committed.error();
    *this = std::move(committed).value();
    return appended.value();
}

Result<OccupancyPublicationHistoryRecord> RotatingOccupancyPublicationHistory::publish(
    const OccupancyPublication& publication_value, const std::filesystem::path& payload_path,
    std::string_view expected_head_publication_id, std::string_view expected_trust_head_bundle_id,
    std::size_t maximum_publications) {
    if (!valid() || !publication_value.valid() || payload_path.empty() ||
        !internal::valid_sha256(std::string(expected_head_publication_id)) ||
        !internal::valid_sha256(std::string(expected_trust_head_bundle_id)) || maximum_publications == 0) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::InvalidArgument, "rotating occupancy publication-history publish input is invalid");
    }
    if (cancelled(options_)) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::Cancelled, "rotating occupancy publication-history publication was cancelled");
    }
    auto lock = DirectoryLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto held_lock = std::move(lock).value();
    (void)held_lock;
    auto fresh = RotatingOccupancyPublicationHistory::open(
        directory_, stream_id_, publisher_service_id_, trust_root_bundle_id_, expected_trust_head_bundle_id,
        root_publication_id_, expected_head_publication_id, options_);
    if (!fresh)
        return fresh.error();
    const auto effective_limit = std::min(maximum_publications, options_.maximum_publications);
    if (fresh.value().publications_.size() >= effective_limit) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::ResourceLimit, "rotating occupancy publication-history publication limit reached");
    }
    if (publication_value.stream_id != fresh.value().stream_id_ ||
        publication_value.publisher_service_id != fresh.value().publisher_service_id_ ||
        publication_value.trust_bundle_id != fresh.value().current_trust_bundle_id_ ||
        publication_value.timeline_id != fresh.value().timeline_id_ ||
        publication_value.workspace_frame_id != fresh.value().workspace_frame_id_) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::IdentityMismatch,
            "occupancy publication does not use the pinned rotating history head", publication_value.id);
    }
    auto successor =
        verify_occupancy_publication_successor(fresh.value().publications_.back(), publication_value);
    if (!successor)
        return successor.error();
    auto payload = read_payload_file(payload_path, options_);
    if (!payload)
        return payload.error();
    std::uintmax_t existing_payload_bytes = 0;
    for (const auto& record : fresh.value().records_) {
        if (record.payload_bytes > options_.maximum_total_payload_bytes - existing_payload_bytes) {
            return Result<OccupancyPublicationHistoryRecord>::failure(
                StatusCode::ResourceLimit,
                "rotating occupancy publication-history total payload bytes exceed limit");
        }
        existing_payload_bytes += static_cast<std::uintmax_t>(record.payload_bytes);
    }
    if (payload.value().size() > options_.maximum_total_payload_bytes - existing_payload_bytes) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::ResourceLimit,
            "rotating occupancy publication-history appended payload exceeds total byte limit");
    }
    auto current_trust = fresh.value().trust_history_->current_bundle();
    if (!current_trust)
        return current_trust.error();
    const auto staged_payload =
        directory_ /
        (".verify-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    internal::BinaryWriter staged_writer;
    staged_writer.bytes(payload.value());
    auto staged = staged_writer.save(staged_payload);
    if (!staged)
        return staged.error();
    auto remove_staged = [&]() {
        std::error_code ignored;
        std::filesystem::remove(staged_payload, ignored);
    };
    auto verified = verify_publication_entry(staged_payload, publication_value, current_trust.value(),
                                             fresh.value().stream_id_, fresh.value().publisher_service_id_,
                                             fresh.value().current_publication_id_, options_);
    if (!verified) {
        remove_staged();
        return verified.error();
    }
    remove_staged();

    const auto stored_payload = directory_ / "payloads" / payload_filename(publication_value);
    auto payload_published =
        publish_payload_bytes(stored_payload, payload.value(), options_.occupancy.maximum_payload_bytes);
    if (!payload_published)
        return payload_published.error();
    const auto stored_publication = directory_ / "publications" / publication_filename(publication_value);
    auto publication_published =
        publish_publication_file(stored_publication, publication_value, options_.maximum_publication_bytes);
    if (!publication_published)
        return publication_published.error();

    const auto record = make_record(publication_value, fresh.value().records_.back().id);
    if (!record.valid()) {
        return Result<OccupancyPublicationHistoryRecord>::failure(
            StatusCode::InternalError, "generated rotating occupancy publication-history record is invalid");
    }
    auto committed = write_immutable_text(directory_ / "records" / record_filename(record),
                                          record_document(record).dump(true) + "\n");
    if (!committed)
        return committed.error();
    auto committed_history = RotatingOccupancyPublicationHistory::open(
        directory_, stream_id_, publisher_service_id_, trust_root_bundle_id_, expected_trust_head_bundle_id,
        root_publication_id_, publication_value.id, options_);
    if (!committed_history)
        return committed_history.error();
    *this = std::move(committed_history).value();
    return record;
}

Result<RotatingOccupancyPublicationHistoryAudit>
audit_rotating_occupancy_publication_histories(const RotatingOccupancyPublicationHistory& first,
                                               const RotatingOccupancyPublicationHistory& second) {
    if (!first.valid() || !second.valid()) {
        return Result<RotatingOccupancyPublicationHistoryAudit>::failure(
            StatusCode::InvalidArgument, "rotating occupancy publication-history audit input is invalid");
    }
    if (first.stream_id_ != second.stream_id_ ||
        first.publisher_service_id_ != second.publisher_service_id_ ||
        first.trust_root_bundle_id_ != second.trust_root_bundle_id_ ||
        first.root_publication_id_ != second.root_publication_id_ ||
        first.timeline_id_ != second.timeline_id_ ||
        first.workspace_frame_id_ != second.workspace_frame_id_) {
        return Result<RotatingOccupancyPublicationHistoryAudit>::failure(
            StatusCode::IdentityMismatch,
            "rotating occupancy publication histories do not share the same roots and stream");
    }
    const auto& first_trust_records = first.trust_history_->records();
    const auto& second_trust_records = second.trust_history_->records();
    std::size_t common_trust = 0;
    const auto maximum_common_trust = std::min(first_trust_records.size(), second_trust_records.size());
    while (common_trust < maximum_common_trust &&
           first_trust_records[common_trust].id == second_trust_records[common_trust].id &&
           first_trust_records[common_trust].bundle_id == second_trust_records[common_trust].bundle_id) {
        ++common_trust;
    }
    std::size_t common_publications = 0;
    const auto maximum_common_publications = std::min(first.records_.size(), second.records_.size());
    while (common_publications < maximum_common_publications &&
           first.records_[common_publications].id == second.records_[common_publications].id &&
           first.publications_[common_publications].id == second.publications_[common_publications].id) {
        ++common_publications;
    }
    if (common_trust == 0 || common_publications == 0) {
        return Result<RotatingOccupancyPublicationHistoryAudit>::failure(
            StatusCode::IdentityMismatch,
            "rotating occupancy publication histories claim roots they do not share");
    }

    RotatingOccupancyPublicationHistoryAudit result;
    result.storage_schema = kStorageSchema;
    result.publication_relation =
        relation_for(first.records_.size(), second.records_.size(), common_publications);
    result.trust_relation =
        relation_for(first_trust_records.size(), second_trust_records.size(), common_trust);
    result.stream_id = first.stream_id_;
    result.publisher_service_id = first.publisher_service_id_;
    result.trust_root_bundle_id = first.trust_root_bundle_id_;
    result.root_publication_id = first.root_publication_id_;
    result.first_trust_head_bundle_id = first.current_trust_bundle_id_;
    result.second_trust_head_bundle_id = second.current_trust_bundle_id_;
    result.first_head_publication_id = first.current_publication_id_;
    result.second_head_publication_id = second.current_publication_id_;
    result.first_trust_bundle_count = static_cast<std::uint64_t>(first_trust_records.size());
    result.second_trust_bundle_count = static_cast<std::uint64_t>(second_trust_records.size());
    result.common_trust_prefix_count = static_cast<std::uint64_t>(common_trust);
    result.common_trust_bundle_id = first_trust_records[common_trust - 1].bundle_id;
    result.first_publication_count = static_cast<std::uint64_t>(first.records_.size());
    result.second_publication_count = static_cast<std::uint64_t>(second.records_.size());
    result.common_publication_prefix_count = static_cast<std::uint64_t>(common_publications);
    result.common_publication_id = first.publications_[common_publications - 1].id;
    result.id = internal::rotating_occupancy_publication_history_audit_identity(result);
    if (!result.valid()) {
        return Result<RotatingOccupancyPublicationHistoryAudit>::failure(
            StatusCode::InternalError,
            "rotating occupancy publication-history audit produced invalid output");
    }
    return result;
}

} // namespace rbfsafe
