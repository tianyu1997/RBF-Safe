#include <rbfsafe/modules/assurance.h>
#include <rbfsafe/modules/core.h>

#include "internal/binary.h"
#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

using internal::Json;

constexpr std::size_t kPublicationFields = 17;
constexpr std::uint32_t kStorageSchema = 1;

bool exact_object(const Json& value, std::size_t fields) {
    return value.is_object() && value.as_object().size() == fields;
}

Result<std::string> string_field(const Json& object, std::string_view key, std::size_t maximum_bytes,
                                 bool allow_empty = false) {
    const auto* value = object.is_object() ? object.find(key) : nullptr;
    if (value == nullptr || !value->is_string() || (!allow_empty && value->as_string().empty()) ||
        value->as_string().size() > maximum_bytes) {
        return Result<std::string>::failure(
            StatusCode::CorruptData, "occupancy publication string field is invalid", std::string(key));
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
            StatusCode::CorruptData, "occupancy publication decimal field is invalid", std::string(key));
    }
    return result;
}

Result<std::uint32_t> integer_field(const Json& object, std::string_view key, std::uint32_t maximum) {
    const auto* value = object.is_object() ? object.find(key) : nullptr;
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || value->as_number() > static_cast<double>(maximum) ||
        std::floor(value->as_number()) != value->as_number()) {
        return Result<std::uint32_t>::failure(
            StatusCode::CorruptData, "occupancy publication integer field is invalid", std::string(key));
    }
    return static_cast<std::uint32_t>(value->as_number());
}

Json publication_payload(const OccupancyPublication& publication) {
    return Json::Object{
        {"algorithm", static_cast<int>(publication.algorithm)},
        {"authentication_tag", publication.authentication_tag},
        {"id", publication.id},
        {"occupancy_bundle_id", publication.occupancy_bundle_id},
        {"parent_publication_id", publication.parent_publication_id},
        {"payload_bytes", std::to_string(publication.payload_bytes)},
        {"payload_digest", publication.payload_digest},
        {"publisher_key_id", publication.publisher_key_id},
        {"publisher_sequence", std::to_string(publication.publisher_sequence)},
        {"publisher_service_id", publication.publisher_service_id},
        {"storage_schema", static_cast<int>(publication.storage_schema)},
        {"stream_id", publication.stream_id},
        {"timeline_id", publication.timeline_id},
        {"trust_bundle_id", publication.trust_bundle_id},
        {"valid_from_tick", std::to_string(publication.valid_from_tick)},
        {"valid_through_tick", std::to_string(publication.valid_through_tick)},
        {"workspace_frame_id", publication.workspace_frame_id},
    };
}

Json publication_document(const OccupancyPublication& publication) {
    const auto payload = publication_payload(publication);
    return Json::Object{
        {"checksum", internal::sha256(payload.dump(false))},
        {"format", "rbfsafe-continuous-fleet-occupancy-publication"},
        {"library_version", kVersion},
        {"payload", payload},
        {"schema", static_cast<int>(kStorageSchema)},
    };
}

Result<OccupancyPublication> decode_publication(const Json& payload) {
    if (!exact_object(payload, kPublicationFields)) {
        return Result<OccupancyPublication>::failure(StatusCode::CorruptData,
                                                     "occupancy publication payload fields are invalid");
    }
    auto storage_schema = integer_field(payload, "storage_schema", std::numeric_limits<std::uint32_t>::max());
    auto id = string_field(payload, "id", 64);
    auto stream = string_field(payload, "stream_id", 256);
    auto sequence = decimal_field(payload, "publisher_sequence");
    auto parent = string_field(payload, "parent_publication_id", 64, true);
    auto service = string_field(payload, "publisher_service_id", 256);
    auto key = string_field(payload, "publisher_key_id", 64);
    auto trust = string_field(payload, "trust_bundle_id", 64);
    auto bundle = string_field(payload, "occupancy_bundle_id", 64);
    auto timeline = string_field(payload, "timeline_id", 256);
    auto frame = string_field(payload, "workspace_frame_id", 256);
    auto valid_from = decimal_field(payload, "valid_from_tick");
    auto valid_through = decimal_field(payload, "valid_through_tick");
    auto digest = string_field(payload, "payload_digest", 64);
    auto bytes = decimal_field(payload, "payload_bytes");
    auto algorithm = integer_field(payload, "algorithm", std::numeric_limits<std::uint32_t>::max());
    auto tag = string_field(payload, "authentication_tag", kEd25519SignatureBytes * 2);
    if (!storage_schema || !id || !stream || !sequence || !parent || !service || !key || !trust || !bundle ||
        !timeline || !frame || !valid_from || !valid_through || !digest || !bytes || !algorithm || !tag) {
        return Result<OccupancyPublication>::failure(StatusCode::CorruptData,
                                                     "occupancy publication payload is incomplete");
    }
    if (storage_schema.value() != kStorageSchema ||
        algorithm.value() != static_cast<std::uint32_t>(ArtifactAuthenticationAlgorithm::Ed25519)) {
        return Result<OccupancyPublication>::failure(
            StatusCode::IncompatibleFormat,
            "occupancy publication payload schema or algorithm is unsupported");
    }
    OccupancyPublication result;
    result.storage_schema = storage_schema.value();
    result.id = std::move(id).value();
    result.stream_id = std::move(stream).value();
    result.publisher_sequence = sequence.value();
    result.parent_publication_id = std::move(parent).value();
    result.publisher_service_id = std::move(service).value();
    result.publisher_key_id = std::move(key).value();
    result.trust_bundle_id = std::move(trust).value();
    result.occupancy_bundle_id = std::move(bundle).value();
    result.timeline_id = std::move(timeline).value();
    result.workspace_frame_id = std::move(frame).value();
    result.valid_from_tick = valid_from.value();
    result.valid_through_tick = valid_through.value();
    result.payload_digest = std::move(digest).value();
    result.payload_bytes = bytes.value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!result.valid()) {
        return Result<OccupancyPublication>::failure(
            StatusCode::IdentityMismatch, "occupancy publication identity or fields are invalid", result.id);
    }
    return result;
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
        if (error) {
            return Result<void>::failure(
                StatusCode::IoError, "failed to stage existing occupancy publication", destination.string());
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish occupancy publication",
                                     destination.string());
    }
    if (destination_exists) {
        std::filesystem::remove(backup, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError, "failed to remove staged occupancy publication",
                                         backup.string());
        }
    }
    return Result<void>::success();
}

} // namespace

Result<void> save_occupancy_publication(const OccupancyPublication& publication,
                                        const std::filesystem::path& path, const SaveOptions& options) {
    if (!publication.valid() || path.empty() || path == path.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "occupancy publication or destination is invalid");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to inspect occupancy publication destination", path.string());
    }
    if (destination_exists) {
        const auto status = std::filesystem::symlink_status(path, error);
        if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
            return Result<void>::failure(
                StatusCode::IoError, "occupancy publication destination is indirect or not a regular file",
                path.string());
        }
        if (!options.overwrite) {
            return Result<void>::failure(StatusCode::IoError,
                                         "occupancy publication destination already exists", path.string());
        }
    }
    const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    if (!std::filesystem::exists(parent, error) || !std::filesystem::is_directory(parent, error) || error) {
        return Result<void>::failure(
            StatusCode::IoError, "occupancy publication destination parent is unavailable", parent.string());
    }
    const auto temporary = unique_sibling(path, ".tmp-");
    const auto document = publication_document(publication).dump(true) + "\n";
    auto written = internal::write_text_file(temporary, document);
    if (!written) {
        std::filesystem::remove(temporary, error);
        return written.error();
    }
    auto loaded = load_occupancy_publication(temporary, std::max<std::uintmax_t>(document.size(), 1));
    if (!loaded || loaded.value().id != publication.id) {
        std::filesystem::remove(temporary, error);
        if (!loaded)
            return loaded.error();
        return Result<void>::failure(StatusCode::CorruptData, "staged occupancy publication identity changed",
                                     publication.id);
    }
    auto published = publish_file(temporary, path, destination_exists);
    if (!published)
        std::filesystem::remove(temporary, error);
    return published;
}

Result<OccupancyPublication> load_occupancy_publication(const std::filesystem::path& path,
                                                        std::uintmax_t maximum_payload_bytes) {
    if (path.empty() || maximum_payload_bytes == 0) {
        return Result<OccupancyPublication>::failure(StatusCode::InvalidArgument,
                                                     "occupancy publication path or byte limit is invalid");
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || status.type() == std::filesystem::file_type::not_found) {
        return Result<OccupancyPublication>::failure(
            StatusCode::IoError, "failed to inspect occupancy publication", path.string());
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<OccupancyPublication>::failure(
            StatusCode::CorruptData, "occupancy publication is indirect or not a regular file",
            path.string());
    }
    auto bytes = internal::read_binary_file(path, maximum_payload_bytes);
    if (!bytes)
        return bytes.error();
    const std::string_view text(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
    auto document = Json::parse(text);
    if (!document)
        return document.error();
    if (!exact_object(document.value(), 5)) {
        return Result<OccupancyPublication>::failure(StatusCode::CorruptData,
                                                     "occupancy publication document fields are invalid");
    }
    auto format = string_field(document.value(), "format", 128);
    auto schema = integer_field(document.value(), "schema", std::numeric_limits<std::uint32_t>::max());
    auto version = string_field(document.value(), "library_version", 128);
    auto checksum = string_field(document.value(), "checksum", 64);
    const auto* payload = document.value().find("payload");
    if (!format || !schema || !version || !checksum || payload == nullptr) {
        return Result<OccupancyPublication>::failure(StatusCode::CorruptData,
                                                     "occupancy publication document is incomplete");
    }
    if (format.value() != "rbfsafe-continuous-fleet-occupancy-publication" ||
        schema.value() != kStorageSchema) {
        return Result<OccupancyPublication>::failure(StatusCode::IncompatibleFormat,
                                                     "occupancy publication format or schema is unsupported");
    }
    if (!internal::valid_sha256(checksum.value()) ||
        checksum.value() != internal::sha256(payload->dump(false))) {
        return Result<OccupancyPublication>::failure(
            StatusCode::CorruptData, "occupancy publication checksum mismatch", path.string());
    }
    return decode_publication(*payload);
}

} // namespace rbfsafe
